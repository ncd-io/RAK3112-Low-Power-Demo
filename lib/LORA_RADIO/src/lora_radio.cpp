#include "lora_radio.h"

RTC_DATA_ATTR unsigned long transmissionCount = 1;

void LoraRadio::setResonantFrame(ResonantFrame* frame) {
    resonantFrame = frame;
}

bool LoraRadio::init(RadioEvents_t &RadioEvents) {

    rtc_gpio_hold_dis((gpio_num_t)LORA_NSS_PIN);

    if(lora_rak3112_init() != 0) {
        return false;
    }

    Radio.Init(&RadioEvents);
    Radio.SetPublicNetwork(true);

    Radio.Standby();
    Radio.SetChannel(RF_FREQUENCY);
    // Radio.SetTxConfig(MODEM_LORA, TX_OUTPUT_POWER, FREQUENCY_DEVIATION, LORA_BANDWIDTH,
    //                   LORA_SPREADING_FACTOR, LORA_CODINGRATE,
    //                   LORA_PREAMBLE_LENGTH, LORA_FIX_LENGTH_PAYLOAD_ON,
    //                   CRC_ON, FREQUENCY_HOPPING_ON, HOP_PERIOD, LORA_IQ_INVERSION_ON, TX_TIMEOUT_VALUE);

    // Radio.SetRxConfig(MODEM_LORA, LORA_BANDWIDTH, LORA_SPREADING_FACTOR, LORA_CODINGRATE, 0, LORA_PREAMBLE_LENGTH, 0, LORA_FIX_LENGTH_PAYLOAD_ON, 0, CRC_ON, FREQUENCY_HOPPING_ON, HOP_PERIOD, LORA_IQ_INVERSION_ON, true);
    // FSK parameters (instead of LoRa params)
    int FSK_DATARATE = 50000;      // 50 kbps
    int FSK_FDEV = 25000;          // 25 kHz deviation  
    int FSK_BANDWIDTH = 125000;    // 50 kHz (2×25k + 50k + margin)

    // Change SetTxConfig from MODEM_LORA to MODEM_FSK
    Radio.SetTxConfig(MODEM_FSK, TX_OUTPUT_POWER, FSK_FDEV, FSK_BANDWIDTH,
                    FSK_DATARATE, 0,  // coderate not used for FSK
                    LORA_PREAMBLE_LENGTH, false, CRC_ON, 
                    false, 0, false, TX_TIMEOUT_VALUE);

    // Change SetRxConfig similarly
    Radio.SetRxConfig(MODEM_FSK, FSK_BANDWIDTH, FSK_DATARATE, 0, 
                    FSK_FDEV, LORA_PREAMBLE_LENGTH, 0, false, 0, 
                    CRC_ON, false, 0, false, true);
    return true;
}

void LoraRadio::deepSleep() {
    Radio.Standby();
    Radio.Sleep();
    SPI.end();


    rtc_gpio_hold_en((gpio_num_t)LORA_NSS_PIN);pinMode(LORA_NSS_PIN, OUTPUT);
	digitalWrite(LORA_NSS_PIN, HIGH);
	rtc_gpio_hold_en((gpio_num_t)LORA_NSS_PIN);
}

void LoraRadio::lightSleep() {
    // Start waiting for data package
	Radio.Standby();
	SX126xSetDioIrqParams(IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,
						  IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,
						  IRQ_RADIO_NONE, IRQ_RADIO_NONE);
	// To get maximum power savings we use Radio.SetRxDutyCycle instead of Radio.Rx(0)
	// This function keeps the SX1261/2 chip most of the time in sleep and only wakes up short times
	// to catch incoming data packages
	Radio.SetRxDutyCycle(2 * 1024 * 1000 * 15.625, 10 * 1024 * 15.625);
	// Make sure the DIO1, RESET and NSS GPIOs are hold on required levels during deep sleep
	rtc_gpio_pulldown_en((gpio_num_t)LORA_DIO_1_PIN);
	rtc_gpio_pullup_en((gpio_num_t)LORA_RESET_PIN);
	rtc_gpio_pullup_en((gpio_num_t)LORA_NSS_PIN);
	// Setup deep sleep with wakeup by external source
	esp_sleep_enable_ext0_wakeup((gpio_num_t)LORA_DIO_1_PIN, RISING);
	// Finally set ESp32 into sleep
	esp_deep_sleep_start();
}

void LoraRadio::send(uint8_t *data, size_t size) {
    if(size > maxPacketSize) {
        Serial1.printf("Sending multi-packet demo in lora radio, size: %zu\n", size);
        // Multi-packet transmission
        multiPacketBuffer = new uint8_t[size];
        multiPacketBufferSize = size;
        multiPacketTotalPackets = (size + maxPacketSize - 1) / maxPacketSize;  // Round up
        multiPacketPacketIndex = 0;
        memcpy(multiPacketBuffer, data, size);
        transmissionInProgress = true;
        
        // Send first packet (wait for OnTxDone to send subsequent packets)
        sendNextMultiPacket();
    } else {
        Serial1.printf("Sending single-packet demo in lora radio, size: %zu\n", size);
        // Single packet transmission
        Radio.Send(data, size);
        transmissionCount++;
    }
}

void LoraRadio::loop() {
    Radio.IrqProcess();
    // Multi-packet sending is handled in OnTxDone callback
}

void LoraRadio::sendNextMultiPacket() {
    // Serial1.println("Send next multi-packet called in lora radio");
    if(multiPacketBuffer == nullptr || resonantFrame == nullptr) {
        Serial1.printf("transmission count: %d\n", transmissionCount);
        return;
    }
    
    // Calculate packet size (last packet may be smaller)
    size_t offset = multiPacketPacketIndex * maxPacketSize;
    size_t remaining = multiPacketBufferSize - offset;
    size_t packetDataSize = (remaining > maxPacketSize) ? maxPacketSize : remaining;
    
    // Build frame with correct packet info
    FrameData frame = resonantFrame->buildMultiPacketFrame(
        multiPacketBuffer + offset,
        packetDataSize,
        multiPacketDestinationID,
        multiPacketFrameAckRequired ? 1 : 0,
        multiPacketTotalPackets,
        multiPacketPacketIndex,
        multiPacketBufferSize  // Pass total data size
    );
    
    // Send the framed packet
    Radio.Send(frame.frame, frame.size);
    transmissionCount++;
    
    // Clean up frame memory (frame is sent, can delete now)
    delete[] frame.frame;
    
    // Note: packetIndex increment and cleanup happens in OnTxDone callback
    // after transmission completes successfully
}

void LoraRadio::startRx(unsigned long timeout) {
    Radio.Standby();  // Put radio in standby before switching to RX
    Radio.Rx(timeout);  // Start continuous RX (0 = no timeout)
}

bool LoraRadio::isTransmissionComplete() const {
    return !transmissionInProgress && multiPacketBuffer == nullptr;
}

bool LoraRadio::continueMultiPacketTransmission() {
    if(!transmissionInProgress || multiPacketBuffer == nullptr) {
        return false;  // Not in multi-packet mode
    }
    
    // Increment packet index after successful transmission
    multiPacketPacketIndex++;
    
    // Check if we've sent all packets
    if(multiPacketPacketIndex >= multiPacketTotalPackets) {
        // Save stats before cleanup
        lastMultiPacketDataSize = multiPacketBufferSize;
        lastMultiPacketCount = multiPacketTotalPackets;
        
        // All packets sent - clean up
        delete[] multiPacketBuffer;
        multiPacketBuffer = nullptr;
        multiPacketBufferSize = 0;
        multiPacketTotalPackets = 0;
        multiPacketPacketIndex = 0;
        transmissionInProgress = false;
        return false;  // Transmission complete
    }
    
    // Send next packet
    sendNextMultiPacket();
    return true;  // More packets to send
}

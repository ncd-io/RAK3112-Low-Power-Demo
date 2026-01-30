#include "main.h"

// ============================================================================
// RTC Memory Variable (survives deep sleep - must be defined in .cpp)
// ============================================================================
RTC_DATA_ATTR float energyBuffer = 0.0f;

// ============================================================================
// Setup (runs on Core 1)
// ============================================================================
void setup()
{
    Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);
    Serial1.println("\n========================================");
    Serial1.println("RAK3112 ResonantLRRadio Demo");
    Serial1.println("Core 0 Radio Execution");
    Serial1.println("========================================");

    // Create radio task on CORE 0 - radio init happens there
    xTaskCreatePinnedToCore(backgroundTasks, "RadioTask", 20000, NULL, 1, &backgroundTask, 0);
    //                                                                                      ^ Core 0!
    Serial1.println("Waiting for radio initialization on Core 0...");

    // Wait for radio to initialize on Core 0
	//This is dangerous, because it will hang the program if the radio does not initialize  Check millis() against wakeTimeout
    while (!resonantRadio.radioInitialized && millis() < wakeTimeout) {
        delay(10);
    }
	if(!resonantRadio.radioInitialized) {
		Serial1.println("Radio initialization failed on Core 0, going to sleep");
		goToSleep();
	}
    Serial1.println("Radio initialized successfully on Core 0");

    // Register callbacks (will be fired from backgroundTasks via loop())
    resonantRadio.onRxComplete(onDataReceived);
    resonantRadio.onTxComplete(onTxComplete);
    resonantRadio.onError(onRadioError);
    Serial1.println("Callbacks registered");

    // Print configuration
    RadioConfig currentConfig = resonantRadio.getConfig();
    Serial1.printf("Frequency: %.1f MHz\n", (double)(currentConfig.frequency / 1000000.0));
    if (currentConfig.modem == MODEM_LORA_MODE) {
        Serial1.printf("Mode: LoRa SF%d BW%d\n", 
            currentConfig.loraSpreadingFactor,
            currentConfig.loraBandwidth == 0 ? 125 : (currentConfig.loraBandwidth == 1 ? 250 : 500));
    } else {
        Serial1.printf("Mode: FSK %d bps\n", currentConfig.fskDatarate);
    }

    // Start timing
    txStartTime = millis();
    preTxTime = millis();

    // Run demo - send() queues the request, Core 0 executes it
    if (multiPacketDemo) {
        Serial1.println("\n--- Multi-Packet Demo ---");
        Serial1.printf("Sending %d bytes of data...\n", strlen(genesis));
        
        // Set destination to broadcast
        uint8_t broadcastID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        resonantRadio.send((uint8_t*)genesis, strlen(genesis), broadcastID, false);
    } else {
        Serial1.println("\n--- Single-Packet Demo ---");
        
        // Create test data
        uint8_t data[200];
        for (int i = 0; i < 200; i++) {
            data[i] = (uint8_t)i;
        }
        
        // Build telemetry frame
        uint8_t destinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        FrameData telemetryFrame = resonantFrame.buildTelemetryFrame(
            data, 200, destinationID, metricsAckRequired ? 1 : 0);
        
        Serial1.printf("Transmitting telemetry frame: %zu bytes\n", telemetryFrame.size);
        resonantRadio.send(telemetryFrame.frame, telemetryFrame.size);
        
        // Free frame memory
        delete[] telemetryFrame.frame;
    }

    Serial1.println("Transmission request queued...");
}

// ============================================================================
// Loop (runs on Core 1 - radio handling is on Core 0)
// ============================================================================
void loop()
{    
    // Check for wake timeout (single packet mode only)
    if (millis() > wakeTimeout && !multiPacketDemo) {
        Serial1.println("Wake timeout reached");
        shouldSleep = true;
    }

    // Check if we should sleep
    if (shouldSleep && resonantRadio.isTransmissionComplete()) {
        goToSleep();
    }
}

// ============================================================================
// Callback: Data Received (already validated by ResonantLRRadio)
// ============================================================================
void onDataReceived(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr)
{
    Serial1.println("\n=== Data Received ===");
    Serial1.printf("RSSI: %d dBm, SNR: %d dB\n", rssi, snr);
    Serial1.printf("Frame Type: 0x%02X, Options: 0x%02X\n", result.frameType, result.options);
    Serial1.printf("Data Length: %zu bytes\n", dataLength);
    
    // Frame is already validated by ResonantLRRadio!
    // Just handle application logic here
    
    if (result.frameType == resonantFrame.acknowledgementFrameType) {
        Serial1.println("ACK received!");
        ackTime = millis() - ackStartTime;
        shouldSleep = true;
    } else if (result.frameType == resonantFrame.multiPacketFrameType) {
        Serial1.println("Multi-packet data received (fully reassembled)");
        // Data is already reassembled - process it
        Serial1.printf("Total packets: %d\n", result.totalPackets);
    } else {
        Serial1.println("Other frame type received");
    }
    
    Serial1.println("=====================\n");
}

// ============================================================================
// Callback: Transmission Complete
// ============================================================================
void onTxComplete(bool success, size_t bytesSent, uint8_t packetCount)
{
    timeOnAir = millis() - txStartTime;
    ackStartTime = millis();
    
    Serial1.println("\n=== TX Complete ===");
    Serial1.printf("Success: %s\n", success ? "YES" : "NO");
    Serial1.printf("Bytes sent: %zu\n", bytesSent);
    Serial1.printf("Packets: %d\n", packetCount);
    Serial1.printf("Time on air: %lu ms\n", timeOnAir);
    
    if (packetCount > 1) {
        float throughput = (bytesSent * 1000.0f) / timeOnAir;
        Serial1.printf("Throughput: %.2f bytes/sec (%.2f KB/s)\n", throughput, throughput / 1024.0f);
    }
    Serial1.println("==================\n");

    transmissionComplete = true;

    if (metricsAckRequired && success) {
        Serial1.println("Waiting for ACK...");
        resonantRadio.startRx(3000);  // 3 second timeout
    } else {
        shouldSleep = true;
    }
}

// ============================================================================
// Callback: Radio Error
// ============================================================================
void onRadioError(uint8_t errorCode, const char* message)
{
    Serial1.printf("\n!!! Radio Error: [%d] %s !!!\n\n", errorCode, message);
    
    switch (errorCode) {
        case RADIO_ERROR_TX_TIMEOUT:
        case RADIO_ERROR_RX_TIMEOUT:
            ackTime = millis() - ackStartTime;
            shouldSleep = true;
            break;
        case RADIO_ERROR_RX_ACCUMULATION_TIMEOUT:
            Serial1.println("Multi-packet reception failed");
            break;
        default:
            break;
    }
}

// ============================================================================
// Go To Sleep
// ============================================================================
void goToSleep(void)
{
    Serial1.printf("\nGoing to sleep after %lu ms awake\n", millis());
    
    // Calculate energy usage
    float preTxEnergy = preTxCurrentDraw * (preTxTime / 3600000.0f);
    float txEnergy = txCurrentDraw * (timeOnAir / 3600000.0f);
    float ackEnergy = ackCurrentDraw * (ackTime / 3600000.0f);
    float totalEnergy = preTxEnergy + txEnergy + ackEnergy;
    
    Serial1.println("\n*********** Energy Usage ***********");
    const float SUPPLY_VOLTAGE = 3.3f;
    float totalEnergy_mWh = totalEnergy * SUPPLY_VOLTAGE;
    float totalEnergy_uWh = totalEnergy_mWh * 1000.0f;
    
    Serial1.printf("Total time logged: %lu ms\n", preTxTime + timeOnAir + ackTime);
    Serial1.printf("Total energy: %.4f mAh (%.2f uWh)\n", totalEnergy, totalEnergy_uWh);
    
    energyBuffer += totalEnergy_mWh;
    Serial1.printf("Energy buffer total: %.4f mWh\n", energyBuffer);
    Serial1.println("************************************\n");
    
    // Put radio to deep sleep
    resonantRadio.deepSleep();
    
    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_TO_S_FACTOR);
    
    // Enter deep sleep
    Serial1.println("Entering deep sleep...");
    Serial1.flush();
    esp_deep_sleep_start();
}

// ============================================================================
// Background Tasks (runs on Core 0 - handles ALL radio operations)
// ============================================================================
void backgroundTasks(void *arg)
{
    Serial1.println("Radio task started on Core 0");
    
    // Initialize radio ON CORE 0 - this is critical for thread safety
    RadioConfig config;
    if (multiPacketDemo) {
        // Use FSK for bulk transfer (faster for large data)
        config = ResonantLRRadio::getFskBulkPreset();
        Serial1.println("Using FSK Bulk Transfer preset");
    } else {
        // Use LoRa for telemetry (better range for small packets)
        config = ResonantLRRadio::getLoRaTelemetryPreset();
        Serial1.println("Using LoRa Telemetry preset");
    }

    if (!resonantRadio.init(&resonantFrame, config)) {
        Serial1.println("ERROR: Radio initialization failed on Core 0!");
        // radioInitialized stays false, setup() will hang
        vTaskDelete(NULL);
        return;
    }
    
    Serial1.println("Radio init complete, starting main radio loop");
    
    // Main radio loop - processes requests, IRQs, and fires callbacks
    while(true) {
        resonantRadio.loop();  // Processes requests + IRQs + fires callbacks
        vTaskDelay(1); // 1ms delay for responsive radio handling
    }
}
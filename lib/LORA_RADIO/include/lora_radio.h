#ifndef LORA_RADIO_H
#define LORA_RADIO_H

#include <Arduino.h>
#include <SPI.h>
#include <SX126x-Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include "resonant_frame.h"

extern unsigned long transmissionCount;

class LoraRadio {
    public:
    void setResonantFrame(ResonantFrame* frame);
    bool init(RadioEvents_t &RadioEvents);
    void deepSleep();
    void lightSleep();
    void send(uint8_t *data, size_t size);
    // void sendMultiPacket(uint8_t *data, size_t size);
    void loop();
    void startRx(unsigned long timeout = 0);
    bool isTransmissionComplete() const;
    void sendNextMultiPacket();  // Internal method for multi-packet transmission
    bool continueMultiPacketTransmission();  // Called from OnTxDone to continue multi-packet

    //User configurable parameters
    int RF_FREQUENCY = 915600000; // Hz
    int TX_OUTPUT_POWER = 22; // dBm
    int LORA_BANDWIDTH = 2; // [0: 125 kHz, 1: 250 kHz, 2: 500 kHz, 3: Reserved]
    int LORA_SPREADING_FACTOR = 7; // [SF7..SF12]
    int LORA_CODINGRATE = 1; // [1: 4/5, 2: 4/6, 3: 4/7, 4: 4/8]
    int LORA_PREAMBLE_LENGTH = 8; // Same for Tx and Rx
    bool LORA_FIX_LENGTH_PAYLOAD_ON = false; // true: Fixed length payload, false: Variable length payload
    bool LORA_IQ_INVERSION_ON = false; // true: Invert IQ, false: Normal IQ
    int TX_TIMEOUT_VALUE = 5000; // ms
    bool FREQUENCY_HOPPING_ON = true; // true: Frequency hopping, false: Fixed frequency
    bool CRC_ON = true; // true: CRC on, false: CRC off
    int HOP_PERIOD = 0; // ms
    int FREQUENCY_DEVIATION = 0; // Hz

    bool multiPacketFrameAckRequired = false;
    uint8_t multiPacketDestinationID[4] = {0xFF,0xFF,0xFF,0xFF};

    bool transmissionInProgress = false;
    
    // Stats from last multi-packet transmission
    size_t lastMultiPacketDataSize = 0;
    uint8_t lastMultiPacketCount = 0;

    private:

    // LoRa pin configuration(Connections between ESP32 and SX1262 inside the RAK3112 module)
    #define LORA_RESET_PIN 8
    #define LORA_DIO_1_PIN 47
    #define LORA_BUSY_PIN 48
    #define LORA_NSS_PIN 7
    #define LORA_SCLK_PIN 5
    #define LORA_MISO_PIN 3
    #define LORA_MOSI_PIN 6
    #define LORA_TXEN_PIN -1
    #define LORA_RXEN_PIN -1

    uint8_t* multiPacketBuffer = nullptr;
    size_t multiPacketBufferSize = 0;
    uint8_t multiPacketTotalPackets = 0;
    uint8_t multiPacketPacketIndex = 0;
    ResonantFrame* resonantFrame = nullptr;

    int maxPacketSize = 239;
    int packetsTransmitted = 0;
};

#endif

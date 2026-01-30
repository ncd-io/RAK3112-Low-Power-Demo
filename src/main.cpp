#include "main.h"

// ============================================================================
// Global Instances
// ============================================================================
ResonantLRRadio radio;
ResonantFrame resonantFrame;

// ============================================================================
// Application State
// ============================================================================
volatile bool shouldSleep = false;
volatile bool transmissionComplete = false;

bool metricsAckRequired = false;
bool multiPacketDemo = true;

// ============================================================================
// Timing & Energy Tracking
// ============================================================================
unsigned long wakeTimeout = 5000;
unsigned long preTxTime = 0;
unsigned long txStartTime = 0;
unsigned long timeOnAir = 0;
unsigned long ackStartTime = 0;
unsigned long ackTime = 0;

float preTxCurrentDraw = 40.7;
float txCurrentDraw = 153.0;
float ackCurrentDraw = 69.3;

RTC_DATA_ATTR float energyBuffer = 0.0f;

// ============================================================================
// Test Data (Genesis text for multi-packet demo)
// ============================================================================
const char* genesis = "\nIn the beginning God created the heaven and the earth.\nAnd the earth was without form, and void; and darkness was upon the face of the deep. And the Spirit of God moved upon the face of the waters.\nAnd God said, Let there be light: and there was light.\nAnd God saw the light, that it was good: and God divided the light from the darkness.\nAnd God called the light Day, and the darkness he called Night. And the evening and the morning were the first day.\nAnd God said, Let there be a firmament in the midst of the waters, and let it divide the waters from the waters.\nAnd God made the firmament, and divided the waters which were under the firmament from the waters which were above the firmament: and it was so.\nAnd God called the firmament Heaven. And the evening and the morning were the second day.\nAnd God said, Let the waters under the heaven be gathered together unto one place, and let the dry land appear: and it was so.\nAnd God called the dry land Earth; and the gathering together of the waters called he Seas: and God saw that it was good.\nAnd God said, Let the earth bring forth grass, the herb yielding seed, and the fruit tree yielding fruit after his kind, whose seed is in itself, upon the earth: and it was so.\nAnd the earth brought forth grass, and herb yielding seed after his kind, and the tree yielding fruit, whose seed was in itself, after his kind: and God saw that it was good.\nAnd the evening and the morning were the third day.\nAnd God said, Let there be lights in the firmament of the heaven to divide the day from the night; and let them be for signs, and for seasons, and for days, and years:\nAnd let them be for lights in the firmament of the heaven to give light upon the earth: and it was so.\nAnd God made two great lights; the greater light to rule the day, and the lesser light to rule the night: he made the stars also.\nAnd God set them in the firmament of the heaven to give light upon the earth,\nAnd to rule over the day and over the night, and to divide the light from the darkness: and God saw that it was good.\nAnd the evening and the morning were the fourth day.";

// ============================================================================
// Setup
// ============================================================================
void setup()
{
    Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);
    Serial1.println("\n========================================");
    Serial1.println("RAK3112 ResonantLRRadio Demo");
    Serial1.println("========================================");

    // Choose configuration based on demo mode
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

    // Initialize radio with ResonantFrame and config
    if (!radio.init(&resonantFrame, config)) {
        Serial1.println("ERROR: Radio initialization failed!");
        return;
    }
    Serial1.println("Radio initialized successfully");

    // Register callbacks
    radio.onRxComplete(onDataReceived);
    radio.onTxComplete(onTxComplete);
    radio.onError(onRadioError);
    Serial1.println("Callbacks registered");

    // Print configuration
    RadioConfig currentConfig = radio.getConfig();
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

    // Run demo
    if (multiPacketDemo) {
        Serial1.println("\n--- Multi-Packet Demo ---");
        Serial1.printf("Sending %d bytes of data...\n", strlen(genesis));
        
        // Set destination to broadcast
        uint8_t broadcastID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
        radio.send((uint8_t*)genesis, strlen(genesis), broadcastID, false);
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
        radio.send(telemetryFrame.frame, telemetryFrame.size);
        
        // Free frame memory
        delete[] telemetryFrame.frame;
    }

    Serial1.println("Transmission started...");
}

// ============================================================================
// Loop - Process radio IRQs here
// ============================================================================
void loop()
{
    // Process radio IRQs - must be called frequently!
    radio.loop();
    
    // Check for wake timeout (single packet mode only)
    if (millis() > wakeTimeout && !multiPacketDemo) {
        Serial1.println("Wake timeout reached");
        shouldSleep = true;
    }

    // Check if we should sleep
    if (shouldSleep && radio.isTransmissionComplete()) {
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
        radio.startRx(3000);  // 3 second timeout
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
    radio.deepSleep();
    
    // Configure timer wakeup
    esp_sleep_enable_timer_wakeup(SLEEP_SECONDS * uS_TO_S_FACTOR);
    
    // Enter deep sleep
    Serial1.println("Entering deep sleep...");
    Serial1.flush();
    esp_deep_sleep_start();
}

#include "main.h"

// ============================================================================
// Setup (runs on Core 1)
// ============================================================================
void setup()
{
    Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);

    DynamicJsonDocument doc(1024);
    uint64_t bigTest = 0xFF34567890ABCDEF;
    uint32_t test = 0xFF345678;
    doc["bit_int"] = test;
    doc["real_big_int"] = bigTest;
    serializeJson(doc, Serial1);
    Serial1.println();

    // Configure power manager// Configure power manager
    powerManager.setSleepDuration(5);
    powerManager.setWakeTimeout(5000);
    powerManager.setStorage(&storage);  // Link storage for persistent energy tracking
    powerManager.markPreTxStart();
    
    Serial1.println("\n========================================");
    Serial1.println("RAK3112 ResonantLRRadio Demo");
    Serial1.println("Core 0 Radio Execution");
    Serial1.println("========================================");

    // Initialize non-volatile storage (must be early to detect first boot)
    if (!storage.begin("resonant")) {
        Serial1.println("WARNING: Storage initialization failed!");
    }

    
    esp_reset_reason_t resetReason = esp_reset_reason();
    firstBoot = resetReason == ESP_RST_POWERON;
    Serial1.print("Reset reason: ");
    Serial1.println(resetReason);
    
    // Log first boot status
    if (firstBoot) {
        Serial1.println(">>> FIRST BOOT - Metrics packet required <<<");
    } else {
        Serial1.printf("Boot #%u - Normal operation\n", storage.getBootCount());
    }

    // Create radio task on CORE 0 - radio init happens there
    xTaskCreatePinnedToCore(backgroundTasks, "RadioTask", 20000, NULL, 1, &backgroundTask, 0);
    //                                                                                      ^ Core 0!
    Serial1.println("Waiting for radio initialization on Core 0...");

    // Wait for radio to initialize on Core 0
    // Check millis() against wakeTimeout to avoid hanging
    while (!resonantRadio.radioInitialized && !powerManager.checkWakeTimeout()) {
        delay(10);
    }
    if(!resonantRadio.radioInitialized || millis() > powerManager.getWakeTimeout()) {
        Serial1.println("Radio initialization failed on Core 0, going to sleep");
        resonantRadio.deepSleep();
        powerManager.goToSleep();
    }else{
        Serial1.println("Radio initialized successfully on Core 0");
    }
    

    // Register callbacks (will be fired from backgroundTasks via loop())
    resonantRadio.onRxComplete(onDataReceived);
    resonantRadio.onTxComplete(onTxComplete);
    resonantRadio.onError(onRadioError);
    resonantRadio.setPowerManager(&powerManager);  // Link power manager for accurate TX timing
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

    // Adoption-aware startup: check if device has a parent modem
    if (!storage.isAdopted()) {
        Serial1.println("\n--- Device NOT adopted - sending adoption advertise ---");
        sendAdoptionAdvertise();
    } else {
        uint8_t parentId[4];
        storage.getParentId(parentId);
        Serial1.printf("\n--- Device adopted by %02X:%02X:%02X:%02X ---\n",
            parentId[0], parentId[1], parentId[2], parentId[3]);
        
        if (multiPacketDemo) {
            Serial1.println("--- Multi-Packet Telemetry ---");
            Serial1.printf("Sending %d bytes of data...\n", strlen(genesis));
            currentTxContext = TxContext::TELEMETRY;
            resonantRadio.send((uint8_t*)genesis, strlen(genesis), parentId, telemetryAckRequired);
        } else {
            Serial1.println("--- Single-Packet Telemetry ---");
            uint8_t data[200];
            for (int i = 0; i < 200; i++) {
                data[i] = (uint8_t)i;
            }
            
            FrameData telemetryFrame = resonantFrame.buildTelemetryFrame(
                data, 200, parentId, telemetryAckRequired ? 1 : 0);
            
            Serial1.printf("Transmitting telemetry frame: %zu bytes\n", telemetryFrame.size);
            currentTxContext = TxContext::TELEMETRY;
            resonantRadio.send(telemetryFrame.frame, telemetryFrame.size);
            delete[] telemetryFrame.frame;
        }
        Serial1.println("Transmission request queued...");
    }
}

// ============================================================================
// Loop (runs on Core 1 - radio handling is on Core 0)
// ============================================================================
void loop()
{    
    // Check for wake timeout (single packet mode only)
    if (!multiPacketDemo && powerManager.checkWakeTimeout()) {
        Serial1.println("Wake timeout reached");
    }

    // Check if we should sleep
    if (powerManager.shouldSleep() && resonantRadio.isTransmissionComplete()) {
        // Clear metrics pending flag before sleep (if it was first boot)
        if (firstBoot) {
            storage.updateLastMetricsTime();
        }
        resonantRadio.deepSleep();
        powerManager.goToSleep();
    }
}

// ============================================================================
// Command Processing
// ============================================================================
void handleCommand(uint8_t commandId, uint8_t* params, size_t paramsLength, uint8_t sourceID[4])
{
    uint8_t responseCode = ResonantFrame::CMD_RESPONSE_SUCCESS;
    
    Serial1.printf("Processing command: 0x%02X\n", commandId);
    
    switch (commandId) {
        case ResonantFrame::CMD_RESET_ENERGY:
            // Reset stored energy in NVS
            storage.resetEnergy();
            // Also reset the RTC buffer (survives deep sleep)
            energyBuffer = 0.0f;
            Serial1.println("Command: Energy counter reset executed");
            break;
            
        case ResonantFrame::CMD_FACTORY_RESET:
            storage.factoryReset();
            energyBuffer = 0.0f;
            Serial1.println("Command: Factory reset executed");
            break;
            
        default:
            responseCode = ResonantFrame::CMD_RESPONSE_UNKNOWN_CMD;
            Serial1.printf("Unknown command: 0x%02X\n", commandId);
            break;
    }
    
    // Send response back to the sender
    // Wait for modem to switch from TX to RX mode before responding
    // Modem delays 100ms after TX, so we need to wait at least that long
    delay(150);
    
    uint8_t responseData[2] = {commandId, responseCode};
    FrameData response = resonantFrame.buildCommandResponseFrame(
        responseData, 2, sourceID, 0);
    currentTxContext = TxContext::COMMAND_RESPONSE;
    resonantRadio.send(response.frame, response.size);
    delete[] response.frame;
    
    Serial1.printf("Command response sent: cmd=0x%02X, result=0x%02X\n", commandId, responseCode);
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
        storage.resetAckFailCount();
        powerManager.markRxComplete();
        if(firstBoot) {
            Serial1.println("First boot - sending metrics frame...");
            sendMetricsFrame();
        }else{
            powerManager.requestSleep();
        }
        
    } else if (result.frameType == resonantFrame.commandFrameType) {
        Serial1.println("Command frame received");
        if (dataLength >= 1) {
            uint8_t commandId = data[0];
            handleCommand(commandId, data + 1, dataLength - 1, result.sourceID);
        } else {
            Serial1.println("Error: Command frame has no data");
        }
    } else if (result.frameType == resonantFrame.adoptionRequestFrameType) {
        Serial1.println("Adoption request received!");
        Serial1.printf("Modem ID: %02X:%02X:%02X:%02X\n",
            result.sourceID[0], result.sourceID[1], result.sourceID[2], result.sourceID[3]);
        
        storage.setParentId(result.sourceID);
        Serial1.println("Parent ID stored");
        
        if (dataLength > 0 && data != nullptr) {
            Serial1.printf("Network config received: %zu bytes\n", dataLength);
            // TODO: parse and apply network config from adoption request payload
        }
        
        powerManager.markRxComplete();
        delay(150);
        sendAdoptionAccept(result.sourceID);
    } else if (result.frameType == resonantFrame.multiPacketFrameType) {
        Serial1.println("Multi-packet data received (fully reassembled)");
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
    // Note: markTxComplete() is now called by ResonantLRRadio when TX actually finishes
    unsigned long timeOnAir = powerManager.getTimeOnAir();
    
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

    switch (currentTxContext) {
        case TxContext::TELEMETRY:
            Serial1.println("Telemetry transmission complete");
            if(telemetryAckRequired) {
                Serial1.println("Telemetry ACK required, waiting for ACK...");
                Serial1.println("Waiting for ACK...");
                powerManager.markRxStart();
                resonantRadio.startRx(3000);  // 3 second timeout
            } else {
                if (firstBoot) {
                    Serial1.println("First boot - sending metrics frame...");
                    sendMetricsFrame();
                }else{
                    Serial1.println("Telemetry ACK not required, going to sleep...");
                    powerManager.requestSleep();
                }
            }
            break;
        case TxContext::METRICS:
            //We just transmitted Metrics so we stay awake and listen for commands  
            Serial1.println("Metrics transmission complete, staying awake and listening for commands...");
            powerManager.markRxStart();  
            resonantRadio.startRx(2000);
            break;
        case TxContext::COMMAND_RESPONSE:
            //Go to sleep
            powerManager.requestSleep();
            break;
        case TxContext::ACK:
            powerManager.requestSleep();
            break;
        case TxContext::ADOPTION_ADVERTISE:
            Serial1.println("Adoption advertise sent, listening for adoption request...");
            powerManager.markRxStart();
            resonantRadio.startRx(storage.getWaitAfterTx());
            break;
        case TxContext::ADOPTION_ACCEPT:
            Serial1.println("Adoption accept sent, going to sleep");
            powerManager.requestSleep();
            break;
        default:
            powerManager.requestSleep();
            break;
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
            powerManager.markRxComplete();
            powerManager.requestSleep();
            break;
        case RADIO_ERROR_RX_TIMEOUT:
            powerManager.markRxComplete();
            if (currentTxContext == TxContext::TELEMETRY && storage.isAdopted()) {
                storage.incrementAckFailCount();
                if (storage.isConnectionLost()) {
                    Serial1.println("Connection lost - clearing parent, will re-adopt next wake");
                    storage.clearParentId();
                }
            }
            powerManager.requestSleep();
            break;
        case RADIO_ERROR_RX_ACCUMULATION_TIMEOUT:
            Serial1.println("Multi-packet reception failed");
            break;
        default:
            break;
    }
}

// ============================================================================
// Background Tasks (runs on Core 0 - handles ALL radio operations)
// ============================================================================
void backgroundTasks(void *arg)
{
    Serial1.println("Radio task started on Core 0");
    
    // Initialize radio ON CORE 0 - this is critical for thread safety
    RadioConfig config = ResonantLRRadio::getLoRaLongRangePreset();
    Serial1.println("Using LoRa Long Range preset (SF7/BW125)");

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

void sendMetricsFrame(void)
{
    //send a metrics frame
    uint8_t data[9] = {0};  // Initialize to zeros
    uint8_t destinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    //metrics frame data structure:
    //byte 0: firmware version
    //byte 1: hardware version
    //byte 2: sensor type
    //byte 3-4: mWh of energy used
    //byte 5-6: Battery voltage
    //byte 7: Average RSSI
    //byte 8: Average SNR
    //bytes 9-10 boot cycles

    data[0] = FIRMWARE_VERSION;
    data[1] = HARDWARE_VERSION;
    data[2] = SENSOR_TYPE;
    //Energy Buffer
    float energyBuffer = storage.getTotalEnergy();
    uint16_t energyBuffer_uint16 = (uint16_t)(energyBuffer * 100);
    data[3] = (uint8_t)(energyBuffer_uint16 >> 8);
    data[4] = (uint8_t)(energyBuffer_uint16 & 0xFF);
    //Battery Voltage
    float batteryVoltage = powerManager.getBatteryVoltage();
    uint16_t batteryVoltage_uint16 = (uint16_t)(batteryVoltage * 100);
    data[5] = (uint8_t)(batteryVoltage_uint16 >> 8);
    data[6] = (uint8_t)(batteryVoltage_uint16 & 0xFF);
    data[7] = 0x00;
    data[8] = 0x00;

    // data[7] = (uint8_t)(storage.getAverageRSSI() >> 8);
    // data[8] = (uint8_t)(storage.getAverageSNR() & 0xFF);

    FrameData metricsFrame = resonantFrame.buildMetricsFrame(data, 9, destinationID, 0);
    currentTxContext = TxContext::METRICS;
    resonantRadio.send(metricsFrame.frame, metricsFrame.size);
    delete[] metricsFrame.frame;
}

void sendAdoptionAdvertise(void)
{
    Serial1.println("Sending adoption advertise frame...");
    FrameData frame = resonantFrame.buildAdoptionAdvertiseFrame(
        SENSOR_TYPE, HARDWARE_VERSION, FIRMWARE_VERSION);
    currentTxContext = TxContext::ADOPTION_ADVERTISE;
    resonantRadio.send(frame.frame, frame.size);
    delete[] frame.frame;
}

void sendAdoptionAccept(uint8_t destinationID[4])
{
    Serial1.printf("Sending adoption accept to %02X:%02X:%02X:%02X\n",
        destinationID[0], destinationID[1], destinationID[2], destinationID[3]);
    FrameData frame = resonantFrame.buildAdoptionAcceptFrame(destinationID, 0);
    currentTxContext = TxContext::ADOPTION_ACCEPT;
    resonantRadio.send(frame.frame, frame.size);
    delete[] frame.frame;
}
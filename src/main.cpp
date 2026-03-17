#include "main.h"

// ============================================================================
// Setup (runs on Core 1)
// ============================================================================
void setup()
{
    //Initialize Serial1 for debug output
    Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);
    uint8_t bootError = 0;

    //Set power manager sleep duration and wake timeout
    powerManager.setSleepDuration(5);
    powerManager.setWakeTimeout(5000);
    powerManager.setStorage(&storage);
    powerManager.enablePeripheralCircuit();

    if (!tempSensor.begin(Wire, 0x48)) {
        bootError = bootError | BootError::SENSOR;
    }
    tempSensor.onDataReady(onSensorDataReady);

    powerManager.markPreTxStart();
    
    LOG_I("\n========================================");
    LOG_I("RAK3112 ResonantLRRadio Demo");
    LOG_I("Core 0 Radio Execution + AES-128-GCM");
    LOG_I("========================================");

    //Initialize storage
    if(!storage.begin("resonant")){ bootError = bootError | BootError::STORAGE; }

    // Clear adoption on any reset that isn't a deep-sleep wake.
    esp_reset_reason_t resetReason = esp_reset_reason();
    firstBoot = resetReason == ESP_RST_POWERON;

    //Clear adoption on reset for test purposes
    if (resetReason != ESP_RST_DEEPSLEEP && storage.isAdopted()) {
        storage.clearParentId();
    }

    //Initialize encryption Load Private Key and Device Certificate(This will be replaced with the real key and certificate from ATECC608B in the future)
    if (encryption.begin()){
        if (device_key_der_len > 0 && device_cert_der_len > 0) {
            if (encryption.loadDeviceCredentials(device_key_der, device_key_der_len, device_cert_der, device_cert_der_len)) {
                LOG_I("Provisioned device credentials loaded");
            }else{ bootError = bootError | BootError::ENCRYPTION; }
        }else{ bootError = bootError | BootError::ENCRYPTION; }
    }else{ bootError = bootError | BootError::ENCRYPTION; }

    //Load Root CA certificate used to validate the gateway certificate
    if (resonant_ca_cert_der_len > 0) {
        if (encryption.loadCACertBundle(resonant_ca_cert_der, resonant_ca_cert_der_len)) {
            LOG_I("Root CA certificate loaded");
        }else{ bootError = bootError | BootError::ENCRYPTION; }
    }else{ bootError = bootError | BootError::ENCRYPTION; }

    // For testing without a real adoption handshake, load a fixed session key
    if (!storage.isAdopted()) {
        uint8_t testKey[ResonantEncryption::AES128_KEY_SIZE] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
        };
        encryption.storeKey(testKey, sizeof(testKey),
                            ResonantEncryption::SLOT_SESSION_KEY);
        memset(testKey, 0, sizeof(testKey));
        LOG_I("Test session key loaded for unadopted testing");
    }    

    xTaskCreatePinnedToCore(backgroundTasks, "RadioTask", 20000, NULL, 1, &backgroundTask, 0);

    while (!resonantRadio.radioInitialized && !powerManager.checkWakeTimeout()) {
        delay(10);
    }
    if(!resonantRadio.radioInitialized || millis() > powerManager.getWakeTimeout()) {
        LOG_E("Radio initialization failed on Core 0, going to sleep");
        bootError = bootError | BootError::RADIO;
    }

    //Register callbacks for radio events
    resonantRadio.onRxComplete(onDataReceived);
    resonantRadio.onTxComplete(onTxComplete);
    resonantRadio.onError(onRadioError);
    resonantRadio.setPowerManager(&powerManager);
    adoptionHandler.init(&encryption, &storage, &resonantFrame, &resonantRadio, &powerManager);

    RadioConfig currentConfig = resonantRadio.getConfig();
    LOG_I("Frequency: %.1f MHz", (double)(currentConfig.frequency / 1000000.0));
    if (currentConfig.modem == MODEM_LORA_MODE) {
        LOG_I("Mode: LoRa SF%d BW%d", 
            currentConfig.loraSpreadingFactor,
            currentConfig.loraBandwidth == 0 ? 125 : (currentConfig.loraBandwidth == 1 ? 250 : 500));
    } else {
        LOG_I("Mode: FSK %d bps", currentConfig.fskDatarate);
    }

    if (!storage.isAdopted()) {
        LOG_I("\n--- Device NOT adopted - sending adoption advertise ---");
        adoptionHandler.sendAdoptionAdvertise(SENSOR_TYPE, HARDWARE_VERSION, FIRMWARE_VERSION,
                                              txSequenceNumber, currentTxContext);
    } else {
        uint8_t parentId[4];
        storage.getParentId(parentId);
        LOG_I("\n--- Device adopted by %02X:%02X:%02X:%02X ---",parentId[0], parentId[1], parentId[2], parentId[3]);
        tempSensor.requestReading();
    }
    if(bootError != 0){
        LOG_E("Boot error: 0x%02X", bootError);
        resonantRadio.deepSleep();
        powerManager.goToSleep();
    }
}

// ============================================================================
// Loop (runs on Core 1 - radio handling is on Core 0)
// ============================================================================
void loop()
{    
    tempSensor.loop();

    if (sensorDataReady && storage.isAdopted()) {
        sensorDataReady = false;
        uint8_t parentId[4];
        storage.getParentId(parentId);

        int16_t tempCenti = (int16_t)(lastTemperatureC * 100);
        uint8_t payload[2] = {
            (uint8_t)(tempCenti >> 8),
            (uint8_t)(tempCenti & 0xFF)
        };
        LOG_I("Temperature: %.2f C -> sending telemetry", lastTemperatureC);
        sendEncryptedTelemetry(payload, 2, parentId);
    }

    if (powerManager.checkWakeTimeout()) {
        LOG_I("Wake timeout reached");
    }

    if (powerManager.shouldSleep() && resonantRadio.isTransmissionComplete()) {
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
    
    LOG_I("Processing command: 0x%02X", commandId);
    
    switch (commandId) {
        case ResonantFrame::CMD_RESET_ENERGY:
            storage.resetEnergy();
            energyBuffer = 0.0f;
            LOG_I("Command: Energy counter reset executed");
            break;
            
        case ResonantFrame::CMD_FACTORY_RESET:
            storage.factoryReset();
            energyBuffer = 0.0f;
            LOG_I("Command: Factory reset executed");
            break;
            
        default:
            responseCode = ResonantFrame::CMD_RESPONSE_UNKNOWN_CMD;
            LOG_W("Unknown command: 0x%02X", commandId);
            break;
    }
    
    delay(150);
    
    uint8_t responseData[2] = {commandId, responseCode};
    uint8_t* encPayload = nullptr;
    size_t encLen = 0;

    uint8_t cmdOpts = ResonantFrame::buildOptionsV1(false);
    uint8_t cmdSensorId[4];
    getDeviceSensorId(cmdSensorId);
    if (encryption.isInitialized() &&
        encryption.encryptForWire(responseData, 2,
                       resonantFrame.commandResponseFrameType, cmdSensorId, txSequenceNumber,
                       &encPayload, &encLen)) {
        FrameData response = resonantFrame.buildCommandResponseFrame(
            encPayload, encLen, sourceID, cmdOpts, txSequenceNumber);
        txSequenceNumber++;
        currentTxContext = TxContext::COMMAND_RESPONSE;
        resonantRadio.send(response.frame, response.size);
        delete[] response.frame;
        delete[] encPayload;
    } else {
        FrameData response = resonantFrame.buildCommandResponseFrame(
            responseData, 2, sourceID, cmdOpts, txSequenceNumber);
        txSequenceNumber++;
        currentTxContext = TxContext::COMMAND_RESPONSE;
        resonantRadio.send(response.frame, response.size);
        delete[] response.frame;
    }
    
    LOG_I("Command response sent: cmd=0x%02X, result=0x%02X", commandId, responseCode);
}

// ============================================================================
// Callback: Data Received (already validated by ResonantLRRadio)
// ============================================================================
void onDataReceived(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr)
{
    LOG_I("\n=== Data Received ===");
    LOG_I("RSSI: %d dBm, SNR: %d dB", rssi, snr);
    LOG_D("Frame Type: 0x%02X, Options: 0x%02X", result.frameType, result.options);
    LOG_D("Data Length: %zu bytes", dataLength);
    
    if (result.frameType == resonantFrame.acknowledgementFrameType) {
        LOG_I("ACK received!");
        storage.resetAckFailCount();
        powerManager.markRxComplete();
        if(firstBoot) {
            LOG_I("First boot - sending metrics frame...");
            sendMetricsFrame();
        }else{
            powerManager.requestSleep();
        }
        
    } else if (result.frameType == resonantFrame.commandFrameType) {
        LOG_I("Command frame received");

        if (encryption.isInitialized() && dataLength > ENCRYPTION_OVERHEAD) {
            uint8_t plaintext[dataLength];
            size_t ptLen = 0;
            if (encryption.decryptFromWire(data, dataLength, result.frameType,
                               result.sourceID, result.sequenceNumber, plaintext, &ptLen)) {
                LOG_D("Decrypted command payload: %zu bytes", ptLen);
                if (ptLen >= 1) {
                    handleCommand(plaintext[0], plaintext + 1, ptLen - 1, result.sourceID);
                } else {
                    LOG_E("Decrypted command has no data");
                }
            } else {
                LOG_W("Decryption failed, treating as plaintext");
                if (dataLength >= 1) {
                    handleCommand(data[0], data + 1, dataLength - 1, result.sourceID);
                }
            }
        } else if (dataLength >= 1) {
            handleCommand(data[0], data + 1, dataLength - 1, result.sourceID);
        } else {
            LOG_E("Command frame has no data");
        }

    } else if (result.frameType == resonantFrame.adoptionRequestFrameType) {
        adoptionHandler.handleAdoptionRequest(data, dataLength, result.sourceID,
                                               txSequenceNumber, currentTxContext);

    } else if (result.frameType == resonantFrame.multiPacketFrameType) {
        LOG_I("Multi-packet data received (fully reassembled)");
        LOG_D("Total packets: %d", result.totalPackets);
    } else {
        LOG_D("Other frame type received");
    }
    
    LOG_I("=====================\n");
}

// ============================================================================
// Callback: Transmission Complete
// ============================================================================
void onTxComplete(bool success, size_t bytesSent, uint8_t packetCount)
{
    unsigned long timeOnAir = powerManager.getTimeOnAir();
    
    LOG_I("\n=== TX Complete ===");
    LOG_I("Success: %s", success ? "YES" : "NO");
    LOG_I("Bytes sent: %zu", bytesSent);
    LOG_I("Packets: %d", packetCount);
    LOG_I("Time on air: %lu ms", timeOnAir);
    
    if (packetCount > 1) {
        float throughput = (bytesSent * 1000.0f) / timeOnAir;
        LOG_D("Throughput: %.2f bytes/sec (%.2f KB/s)", throughput, throughput / 1024.0f);
    }
    LOG_I("==================\n");

    transmissionComplete = true;

    switch (currentTxContext) {
        case TxContext::TELEMETRY:
            LOG_I("Telemetry transmission complete");
            if(telemetryAckRequired) {
                LOG_I("Waiting for ACK...");
                powerManager.markRxStart();
                resonantRadio.startRx(3000);
            } else {
                if (firstBoot) {
                    LOG_I("First boot - sending metrics frame...");
                    sendMetricsFrame();
                }else{
                    LOG_I("Telemetry ACK not required, going to sleep...");
                    powerManager.requestSleep();
                }
            }
            break;
        case TxContext::METRICS:
            LOG_I("Metrics TX complete, listening for commands...");
            powerManager.markRxStart();  
            resonantRadio.startRx(storage.getWaitAfterTx());
            break;
        case TxContext::COMMAND_RESPONSE:
            powerManager.requestSleep();
            break;
        case TxContext::ACK:
            powerManager.requestSleep();
            break;
        case TxContext::ADOPTION_ADVERTISE:
            LOG_I("Adoption advertise sent, listening for adoption request...");
            powerManager.markRxStart();
            resonantRadio.startRx(storage.getWaitAfterTx());
            break;
        case TxContext::ADOPTION_ACCEPT:
            LOG_I("Adoption accept sent, going to sleep");
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
    LOG_E("Radio Error: [%d] %s", errorCode, message);
    
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
                    LOG_W("Connection lost - clearing parent, will re-adopt next wake");
                    storage.clearParentId();
                }
            }
            powerManager.requestSleep();
            break;
        case RADIO_ERROR_RX_ACCUMULATION_TIMEOUT:
            LOG_W("Multi-packet reception failed");
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
    LOG_I("Radio task started on Core 0");
    
    RadioConfig config = ResonantLRRadio::getLoRaLongRangePreset();
    LOG_I("Using LoRa Long Range preset (SF7/BW125)");

    if (!resonantRadio.init(&resonantFrame, config)) {
        LOG_E("Radio initialization failed on Core 0!");
        vTaskDelete(NULL);
        return;
    }
    
    LOG_I("Radio init complete, starting main radio loop");
    
    while(true) {
        resonantRadio.loop();
        vTaskDelay(1);
    }
}

// ============================================================================
// Callback: Sensor Data Ready
// ============================================================================
void onSensorDataReady(float temperatureC)
{
    lastTemperatureC = temperatureC;
    sensorDataReady = true;
}

// ============================================================================
// Telemetry Helper: encrypt-or-fallback, frame build, send
// ============================================================================
void sendEncryptedTelemetry(const uint8_t* payload, size_t payloadLen, uint8_t parentId[4])
{
    uint8_t sensorId[4];
    getDeviceSensorId(sensorId);

    uint8_t* encPayload = nullptr;
    size_t encLen = 0;
    bool encrypted = encryption.isInitialized() &&
                     encryption.encryptForWire(payload, payloadLen,
                         resonantFrame.telemetryFrameType, sensorId, txSequenceNumber,
                         &encPayload, &encLen);

    uint8_t* txData = encrypted ? encPayload : const_cast<uint8_t*>(payload);
    size_t txLen = encrypted ? encLen : payloadLen;

    if (encrypted) {
        LOG_I("Sending %zu encrypted bytes (%zu plaintext)", encLen, payloadLen);
    } else {
        LOG_W("Encryption unavailable, sending plaintext");
    }

    uint8_t opts = ResonantFrame::buildOptionsV1(telemetryAckRequired);
    FrameData frame = resonantFrame.buildTelemetryFrame(
        txData, txLen, parentId, opts, txSequenceNumber);
    txSequenceNumber++;
    currentTxContext = TxContext::TELEMETRY;
    resonantRadio.send(frame.frame, frame.size, parentId, telemetryAckRequired);
    delete[] frame.frame;
    if (encrypted) {
        delete[] encPayload;
    }
}

void sendMetricsFrame(void)
{
    uint8_t data[9] = {0};
    uint8_t destinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};

    data[0] = FIRMWARE_VERSION;
    data[1] = HARDWARE_VERSION;
    data[2] = SENSOR_TYPE;
    float energyBuf = storage.getTotalEnergy();
    uint16_t energyBuffer_uint16 = (uint16_t)(energyBuf * 100);
    data[3] = (uint8_t)(energyBuffer_uint16 >> 8);
    data[4] = (uint8_t)(energyBuffer_uint16 & 0xFF);
    float batteryVoltage = powerManager.getBatteryVoltage();
    uint16_t batteryVoltage_uint16 = (uint16_t)(batteryVoltage * 100);
    data[5] = (uint8_t)(batteryVoltage_uint16 >> 8);
    data[6] = (uint8_t)(batteryVoltage_uint16 & 0xFF);
    data[7] = 0x00;
    data[8] = 0x00;

    uint8_t* encPayload = nullptr;
    size_t encLen = 0;

    uint8_t metricsOpts = ResonantFrame::buildOptionsV1(false);
    uint8_t metricsSensorId[4];
    getDeviceSensorId(metricsSensorId);
    if (encryption.isInitialized() &&
        encryption.encryptForWire(data, 9, resonantFrame.metricsFrameType, metricsSensorId, txSequenceNumber,
                       &encPayload, &encLen)) {
        FrameData metricsFrame = resonantFrame.buildMetricsFrame(
            encPayload, encLen, destinationID, metricsOpts, txSequenceNumber);
        txSequenceNumber++;
        currentTxContext = TxContext::METRICS;
        resonantRadio.send(metricsFrame.frame, metricsFrame.size);
        delete[] metricsFrame.frame;
        delete[] encPayload;
        LOG_I("Encrypted metrics frame sent (%zu bytes)", encLen);
    } else {
        FrameData metricsFrame = resonantFrame.buildMetricsFrame(data, 9, destinationID, metricsOpts, txSequenceNumber);
        txSequenceNumber++;
        currentTxContext = TxContext::METRICS;
        resonantRadio.send(metricsFrame.frame, metricsFrame.size);
        delete[] metricsFrame.frame;
    }
}

// ============================================================================
// Device Identity Helper
// ============================================================================

void getDeviceSensorId(uint8_t* sensorId)
{
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    memcpy(sensorId, mac + 2, 4);
}

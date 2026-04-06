#include "main.h"

// ============================================================================
// Setup (runs on Core 1)
// ============================================================================
void setup()
{
    Serial1.begin(115200, SERIAL_8N1, PIN_SERIAL1_RX, PIN_SERIAL1_TX);
    uint16_t bootError = 0;

    // Safe defaults before FRAM is read
    powerManager.setSleepDuration(5);
    powerManager.setWakeTimeout(5000);
    powerManager.setWakeInterruptPin(2, HIGH);
    powerManager.setContactWakePin(14);
    powerManager.enablePeripheralCircuit();

    // --- FRAM Init ---
    if (fram.begin(framSPI, 12, 10, 11, 13)) {
        LOG_I("FRAM MB85RS64V detected");
        if (fram.selfTest()) {
            LOG_I("FRAM self-test PASSED");
        } else {
            LOG_W("FRAM self-test FAILED");
            bootError |= BootError::FRAM;
        }
    } else {
        LOG_E("FRAM MB85RS64V not detected");
        bootError |= BootError::FRAM;
    }

    // --- FRAM Storage Init ---
    if (!(bootError & BootError::FRAM)) {
        if (!framStorage.begin(fram, SENSOR_TYPE, FIRMWARE_VERSION, HARDWARE_VERSION)) {
            LOG_E("FRAM storage initialization failed");
            bootError |= BootError::STORAGE;
        }
    }

    // Configure power manager from FRAM settings (with sane minimums)
    if (framStorage.isInitialized()) {
        uint16_t sleepSec = framStorage.settings().telemetryInterval;
        uint16_t wakeMs   = framStorage.settings().telemetryMaxWake;
        powerManager.setSleepDuration(sleepSec > 0 ? sleepSec : 5);
        powerManager.setWakeTimeout(wakeMs >= 1000 ? wakeMs : 5000);
    }

    // --- Determine wake reason ---
    esp_reset_reason_t resetReason = esp_reset_reason();
    firstBoot = (resetReason == ESP_RST_POWERON);
    interruptWake = powerManager.wasWokenByInterrupt();
    contactWake = powerManager.wasWokenByContact();

    if (framStorage.isInitialized()) {
        if (firstBoot) {
            framStorage.setLastWakeReason(WakeReason::POWER_ON);
            framStorage.incrementBootCount();
        } else if (interruptWake) {
            framStorage.setLastWakeReason(WakeReason::BUTTON);
        } else if (contactWake) {
            framStorage.setLastWakeReason(WakeReason::CONTACT);
        } else {
            framStorage.setLastWakeReason(WakeReason::TIMER);
        }

        framStorage.clearCycleFlags();
        framStorage.incrementCycleCount();

        // --- Brownout Detection ---
        if (framStorage.scratchpad().lastTxStatus == TxStatus::TX_ATTEMPT) {
            uint8_t recoveryCount = framStorage.scratchpad().brownoutRecoveryCount;
            recoveryCount++;
            framStorage.setBrownoutRecoveryCount(recoveryCount);
            LOG_W("Brownout detected! Previous TX never completed. Recovery cycle %u", recoveryCount);

            uint32_t extendedSleep = framStorage.settings().telemetryInterval * (1 << recoveryCount);
            if (extendedSleep > 3600) extendedSleep = 3600;
            powerManager.setSleepDuration(extendedSleep);
            LOG_W("Extended sleep: %lu seconds for battery recovery", extendedSleep);
        } else {
            if (framStorage.scratchpad().brownoutRecoveryCount > 0) {
                framStorage.setBrownoutRecoveryCount(0);
            }
        }

        framStorage.setLastTxStatus(TxStatus::IDLE);

        // --- Battery Voltage Filtering ---
        updateBatteryVoltage();

        // Add sleep time from this sleep cycle (telemetryInterval approximation)
        if (resetReason == ESP_RST_DEEPSLEEP) {
            framStorage.addSleepTime(framStorage.settings().telemetryInterval);
        }
    }

    if (interruptWake) {
        LOG_I("*** Woken by user button (ext0/GPIO2) ***");
    } else if (contactWake) {
        LOG_I("*** Woken by contact sensor (ext1/GPIO14) ***");
    }

    // --- Sensor Init ---
    if (!tempSensor.begin(Wire, 0x48)) {
        bootError |= BootError::SENSOR;
    }
    tempSensor.setContactPin(14);
    tempSensor.onDataReady(onSensorDataReady);

    LOG_I("\n========================================");
    LOG_I("RAK3112 ResonantLRRadio");
    LOG_I("FRAM Storage v1");
    LOG_I("========================================");

    // Clear adoption on reset for test purposes
    if (resetReason != ESP_RST_DEEPSLEEP && framStorage.isAdopted()) {
        framStorage.clearParentID();
#ifdef ATECC_MOCK
        uint8_t zeroKey[ResonantEncryption::AES128_KEY_SIZE] = {0};
        framStorage.setMockSessionKey(zeroKey, sizeof(zeroKey));
        LOG_I("Mock session key cleared (adoption reset)");
#endif
    }

    // Start radio init on Core 0
    xTaskCreatePinnedToCore(backgroundTasks, "RadioTask", 20000, NULL, 1, &backgroundTask, 0);

    // --- Encryption Init ---
    if (encryption.begin()) {
        if (device_key_der_len > 0 && device_cert_der_len > 0) {
            if (encryption.loadDeviceCredentials(device_key_der, device_key_der_len, device_cert_der, device_cert_der_len)) {
                LOG_I("Provisioned device credentials loaded");
            } else { bootError |= BootError::ENCRYPTION; }
        } else { bootError |= BootError::ENCRYPTION; }
    } else { bootError |= BootError::ENCRYPTION; }

    if (resonant_ca_cert_der_len > 0) {
        if (encryption.loadCACertBundle(resonant_ca_cert_der, resonant_ca_cert_der_len)) {
            LOG_I("Root CA certificate loaded");
        } else { bootError |= BootError::ENCRYPTION; }
    } else { bootError |= BootError::ENCRYPTION; }

    if (!framStorage.isAdopted()) {
        uint8_t testKey[ResonantEncryption::AES128_KEY_SIZE] = {
            0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
            0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F
        };
        encryption.storeKey(testKey, sizeof(testKey),
                            ResonantEncryption::SLOT_SESSION_KEY);
        memset(testKey, 0, sizeof(testKey));
        LOG_I("Test session key loaded for unadopted testing");
    }
#ifdef ATECC_MOCK
    else if (framStorage.hasMockSessionKey()) {
        uint8_t savedKey[ResonantEncryption::AES128_KEY_SIZE];
        framStorage.getMockSessionKey(savedKey, sizeof(savedKey));
        encryption.storeKey(savedKey, sizeof(savedKey),
                            ResonantEncryption::SLOT_SESSION_KEY);
        memset(savedKey, 0, sizeof(savedKey));
        LOG_I("Session key restored from FRAM (mock)");
    }
#endif

    // Wait for radio init on Core 0 (fixed 3s ceiling, independent of wake timeout)
    unsigned long radioWaitStart = millis();
    while (!resonantRadio.radioInitialized && (millis() - radioWaitStart < 3000)) {
        delay(1);
    }
    if (!resonantRadio.radioInitialized) {
        LOG_E("Radio initialization failed on Core 0, going to sleep");
        bootError |= BootError::RADIO;
    }

    resonantRadio.onRxComplete(onDataReceived);
    resonantRadio.onTxComplete(onTxComplete);
    resonantRadio.onError(onRadioError);
    resonantRadio.setPowerManager(&powerManager);
    adoptionHandler.init(&encryption, &framStorage, &resonantFrame, &resonantRadio, &powerManager);

    RadioConfig currentConfig = resonantRadio.getConfig();
    LOG_I("Frequency: %.1f MHz", (double)(currentConfig.frequency / 1000000.0));
    if (currentConfig.modem == MODEM_LORA_MODE) {
        LOG_I("Mode: LoRa SF%d BW%d",
            currentConfig.loraSpreadingFactor,
            currentConfig.loraBandwidth == 0 ? 125 : (currentConfig.loraBandwidth == 1 ? 250 : 500));
    } else {
        LOG_I("Mode: FSK %d bps", currentConfig.fskDatarate);
    }

    if (!framStorage.isAdopted()) {
        LOG_I("\n--- Device NOT adopted - sending adoption advertise ---");
        uint32_t seq = framStorage.scratchpad().txSequenceNumber;
        adoptionHandler.sendAdoptionAdvertise(SENSOR_TYPE, HARDWARE_VERSION, FIRMWARE_VERSION,
                                              seq, currentTxContext);
    } else {
        uint8_t parentId[4];
        memcpy(parentId, framStorage.settings().parentID, 4);
        LOG_I("\n--- Device adopted by %02X:%02X:%02X:%02X ---",
              parentId[0], parentId[1], parentId[2], parentId[3]);
        tempSensor.requestReading();
    }

    if (bootError != 0) {
        LOG_E("Boot error: 0x%04X", bootError);
        if (framStorage.isInitialized()) {
            accumulateMetricsBeforeSleep();
            framStorage.flush();
        }
        resonantRadio.deepSleep();
        powerManager.goToSleep();
    }
}

// ============================================================================
// Loop (runs on Core 1)
// ============================================================================
void loop()
{
    tempSensor.loop();

    if (sensorDataReady && framStorage.isAdopted()) {
        sensorDataReady = false;
        uint8_t parentId[4];
        memcpy(parentId, framStorage.settings().parentID, 4);

        int16_t tempCenti = (int16_t)(lastTemperatureC * 100);
        uint8_t payload[3] = {
            (uint8_t)(tempCenti >> 8),
            (uint8_t)(tempCenti & 0xFF),
            lastContactClosed ? (uint8_t)0x01 : (uint8_t)0x00
        };
        LOG_I("Temperature: %.2f C, Contact: %s -> sending telemetry",
              lastTemperatureC, lastContactClosed ? "CLOSED" : "OPEN");

        // Mark TX attempt in scratchpad for brownout detection
        uint16_t vBat = (uint16_t)(powerManager.getBatteryVoltage() * 100);
        framStorage.setPreTxBatteryVoltage(vBat);
        framStorage.setLastTxStatus(TxStatus::TX_ATTEMPT);
        framStorage.flush();

        sendEncryptedTelemetry(payload, 3, parentId);
    }

    if (pendingSettingsReport && !resonantRadio.isBusy()) {
        pendingSettingsReport = false;
        LOG_I("Sending settings report");
        delay(50);
        sendSettingsFrame();
    }

    bool telemetryOnlyCycle = framStorage.isAdopted()
                           && !firstBoot
                           && !interruptWake
                           && currentTxContext != TxContext::ADOPTION_ADVERTISE
                           && currentTxContext != TxContext::ADOPTION_ACCEPT
                           && currentTxContext != TxContext::METRICS
                           && currentTxContext != TxContext::SETTINGS_REPORT
                           && currentTxContext != TxContext::COMMAND_RESPONSE;

    if (telemetryOnlyCycle && powerManager.checkWakeTimeout()) {
        LOG_I("Wake timeout reached (telemetry-only cycle)");
    }

    if (powerManager.shouldSleep() && !resonantRadio.isBusy()
        && resonantRadio.isTransmissionComplete()) {
        accumulateMetricsBeforeSleep();
        framStorage.flush();
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
        case ResonantFrame::CMD_RESET_ENERGY: {
            framStorage.setBatteryVoltage(0);
            MetricsMap& m = const_cast<MetricsMap&>(framStorage.metrics());
            m.totalEnergy = 0;
            m.totalTxTime = 0;
            m.totalRxTime = 0;
            m.totalActiveTime = 0;
            m.totalSleepTime = 0;
            LOG_I("Command: Energy/timing counters reset");
            break;
        }

        case ResonantFrame::CMD_FACTORY_RESET:
            framStorage.factoryReset();
            LOG_I("Command: Factory reset executed");
            break;

        case ResonantFrame::CMD_SLEEP_NOW:
            LOG_I("Command: Sleep now — skipping response to save power");
            powerManager.markRxComplete();
            accumulateMetricsBeforeSleep();
            framStorage.flush();
            resonantRadio.deepSleep();
            powerManager.goToSleep();
            return;

        case ResonantFrame::CMD_CONFIGURE_SETTINGS: {
            if (paramsLength != 207) {
                responseCode = ResonantFrame::CMD_RESPONSE_INVALID_PARAMS;
                LOG_W("Configure settings: expected 207 bytes, got %zu", paramsLength);
                break;
            }
            if (!framStorage.applySettingsFromWire(params, paramsLength)) {
                responseCode = ResonantFrame::CMD_RESPONSE_FAILED;
                break;
            }
            framStorage.flush();

            pendingRadioConfig = resonantRadio.getConfig();
            pendingRadioConfig.txPower = framStorage.settings().txPower;
            pendingRadioConfig.loraSpreadingFactor = framStorage.settings().spreadingFactor;
            pendingRadioConfig.loraBandwidth = framStorage.settings().bandwidth;
            pendingRadioConfig.frequency = framStorage.settings().frequency;
            pendingRadioConfig.loraCodingRate = framStorage.settings().codingRate;

            powerManager.setSleepDuration(framStorage.settings().telemetryInterval);
            powerManager.extendWakeTimeout(framStorage.settings().telemetryMaxWake);

            LOG_I("Settings applied from wire (radio config deferred until after TX)");
            pendingRadioConfigApply = true;
            pendingSettingsReport = true;
            return;
        }

        case ResonantFrame::CMD_REQUEST_SETTINGS:
            LOG_I("Command: Request settings");
            pendingSettingsReport = true;
            return;

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
    uint32_t seq = framStorage.getNextTxSequenceNumber();

    if (encryption.isInitialized() &&
        encryption.encryptForWire(responseData, 2,
                       resonantFrame.commandResponseFrameType, cmdSensorId, seq,
                       &encPayload, &encLen)) {
        FrameData response = resonantFrame.buildCommandResponseFrame(
            encPayload, encLen, sourceID, cmdOpts, seq);
        currentTxContext = TxContext::COMMAND_RESPONSE;
        resonantRadio.send(response.frame, response.size);
        delete[] response.frame;
        delete[] encPayload;
    } else {
        FrameData response = resonantFrame.buildCommandResponseFrame(
            responseData, 2, sourceID, cmdOpts, seq);
        currentTxContext = TxContext::COMMAND_RESPONSE;
        resonantRadio.send(response.frame, response.size);
        delete[] response.frame;
    }

    LOG_I("Command response sent: cmd=0x%02X, result=0x%02X", commandId, responseCode);
}

// ============================================================================
// Callback: Data Received
// ============================================================================
void onDataReceived(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr)
{
    LOG_I("\n=== Data Received ===");
    LOG_I("RSSI: %d dBm, SNR: %d dB", rssi, snr);
    LOG_I("Frame Type: 0x%02X, Options: 0x%02X", result.frameType, result.options);
    LOG_I("Data Length: %zu bytes", dataLength);
    LOG_I("Timestamp: %lu", millis());

    if (result.frameType == resonantFrame.acknowledgementFrameType) {
        LOG_I("ACK received!");
        framStorage.resetAckFailCount();
        framStorage.addCycleFlag(CycleFlag::ACK_RECEIVED);
        powerManager.markRxComplete();

        // After telemetry ACK, check if metrics are due
        if (framStorage.isMetricsDue() || firstBoot || interruptWake) {
            LOG_I("Sending metrics frame...");
            powerManager.clearSleepRequest();
            sendMetricsFrame();
        } else {
            powerManager.requestSleep();
        }

    } else if (result.frameType == resonantFrame.commandFrameType) {
        LOG_I("Command frame received");
        framStorage.addCycleFlag(CycleFlag::CMD_RECEIVED);
        powerManager.extendWakeTimeout(framStorage.settings().telemetryMaxWake);

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
        powerManager.clearSleepRequest();
        powerManager.setWakeTimeout(10000);
        uint32_t seq = framStorage.scratchpad().txSequenceNumber;
        adoptionHandler.handleAdoptionRequest(data, dataLength, result.sourceID,
                                               seq, currentTxContext);
        framStorage.setTxSequenceNumber(seq);

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
    unsigned long totalTxTime = powerManager.getTxTime();

    LOG_I("\n=== TX Complete ===");
    LOG_I("Success: %s", success ? "YES" : "NO");
    LOG_I("Bytes sent: %zu", bytesSent);
    LOG_I("Packets: %d", packetCount);
    LOG_I("Total TX time: %lu ms", totalTxTime);
    LOG_I("Timestamp: %lu", millis());

    if (packetCount > 1) {
        float throughput = (bytesSent * 1000.0f) / totalTxTime;
        LOG_D("Throughput: %.2f bytes/sec (%.2f KB/s)", throughput, throughput / 1024.0f);
    }
    LOG_I("==================\n");

    transmissionComplete = true;

    if (success) {
        framStorage.setLastTxStatus(TxStatus::TX_SUCCESS);
        framStorage.incrementTxCount();
    } else {
        framStorage.setLastTxStatus(TxStatus::TX_FAILED);
    }

    bool telemetryAckRequired = framStorage.settings().telemetryAckRequired != 0;

    switch (currentTxContext) {
        case TxContext::TELEMETRY:
            LOG_I("Telemetry transmission complete");
            framStorage.incrementTelemetrySinceMetrics();

            if (telemetryAckRequired) {
                LOG_I("Waiting for ACK...");
                powerManager.markRxStart();
                resonantRadio.startRx(3000);
            } else {
                if (framStorage.isMetricsDue() || firstBoot || interruptWake) {
                    LOG_I("Sending metrics frame...");
                    powerManager.clearSleepRequest();
                    sendMetricsFrame();
                } else {
                    powerManager.requestSleep();
                }
            }
            break;
        case TxContext::METRICS:
            LOG_I("Metrics TX complete, listening for commands...");
            framStorage.addCycleFlag(CycleFlag::METRICS_SENT);
            framStorage.resetTelemetrySinceMetrics();
            powerManager.markRxStart();
            resonantRadio.startRx(framStorage.getWaitAfterTx());
            break;
        case TxContext::SETTINGS_REPORT:
            LOG_I("Settings report TX complete");
            if (pendingRadioConfigApply) {
                pendingRadioConfigApply = false;
                resonantRadio.setConfig(pendingRadioConfig);
                resonantRadio.applyConfig();
                LOG_I("Deferred radio config applied after settings report");
            }
            powerManager.requestSleep();
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
            resonantRadio.startRx(framStorage.getWaitAfterTx());
            break;
        case TxContext::ADOPTION_ACCEPT:
            LOG_I("Adoption accept sent, sending initial metrics...");
            powerManager.clearSleepRequest();
            powerManager.setWakeTimeout(10000);
            sendMetricsFrame();
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
            framStorage.setLastTxStatus(TxStatus::TX_FAILED);
            powerManager.markRxComplete();
            powerManager.requestSleep();
            break;
        case RADIO_ERROR_RX_TIMEOUT:
            powerManager.markRxComplete();
            if (currentTxContext == TxContext::TELEMETRY && framStorage.isAdopted()) {
                framStorage.incrementAckFailCount();
                framStorage.incrementAckFailTotal();
                if (framStorage.isConnectionLost()) {
                    LOG_W("Connection lost — clearing parent, will re-adopt next wake");
                    framStorage.clearParentID();
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
// Background Tasks (runs on Core 0)
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

    while (true) {
        resonantRadio.loop();
        vTaskDelay(1);
    }
}

// ============================================================================
// Callback: Sensor Data Ready
// ============================================================================
void onSensorDataReady(float temperatureC, bool contactClosed)
{
    lastTemperatureC = temperatureC;
    lastContactClosed = contactClosed;
    sensorDataReady = true;
}

// ============================================================================
// Telemetry Helper
// ============================================================================
void sendEncryptedTelemetry(const uint8_t* payload, size_t payloadLen, uint8_t parentId[4])
{
    uint8_t sensorId[4];
    getDeviceSensorId(sensorId);

    uint32_t seq = framStorage.getNextTxSequenceNumber();

    uint8_t* encPayload = nullptr;
    size_t encLen = 0;
    bool encrypted = encryption.isInitialized() &&
                     encryption.encryptForWire(payload, payloadLen,
                         resonantFrame.telemetryFrameType, sensorId, seq,
                         &encPayload, &encLen);

    uint8_t* txData = encrypted ? encPayload : const_cast<uint8_t*>(payload);
    size_t txLen = encrypted ? encLen : payloadLen;

    if (encrypted) {
        LOG_I("Sending %zu encrypted bytes (%zu plaintext)", encLen, payloadLen);
    } else {
        LOG_W("Encryption unavailable, sending plaintext");
    }

    bool telemetryAckRequired = framStorage.settings().telemetryAckRequired != 0;
    uint8_t opts = ResonantFrame::buildOptionsV1(telemetryAckRequired);
    FrameData frame = resonantFrame.buildTelemetryFrame(
        txData, txLen, parentId, opts, seq);
    currentTxContext = TxContext::TELEMETRY;
    resonantRadio.send(frame.frame, frame.size, parentId, telemetryAckRequired);
    delete[] frame.frame;
    if (encrypted) {
        delete[] encPayload;
    }
}

// ============================================================================
// Metrics Frame — reads directly from FRAM metrics region
// ============================================================================
void sendMetricsFrame(void)
{
    framStorage.flush();
    framStorage.preparePayloads();

    const uint8_t* metricsData = framStorage.getMetricsPayload();
    size_t metricsLen = ResonantFRAMStorage::PAYLOAD_SIZE;

    uint8_t destinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t* encPayload = nullptr;
    size_t encLen = 0;

    uint8_t metricsOpts = ResonantFrame::buildOptionsV1(false);
    uint8_t metricsSensorId[4];
    getDeviceSensorId(metricsSensorId);
    uint32_t seq = framStorage.getNextTxSequenceNumber();

    if (encryption.isInitialized() &&
        encryption.encryptForWire(metricsData, metricsLen,
                       resonantFrame.metricsFrameType, metricsSensorId, seq,
                       &encPayload, &encLen)) {
        FrameData metricsFrame = resonantFrame.buildMetricsFrame(
            encPayload, encLen, destinationID, metricsOpts, seq);
        currentTxContext = TxContext::METRICS;
        resonantRadio.send(metricsFrame.frame, metricsFrame.size);
        delete[] metricsFrame.frame;
        delete[] encPayload;
        LOG_I("Encrypted metrics frame sent (%zu bytes)", encLen);
    } else {
        FrameData metricsFrame = resonantFrame.buildMetricsFrame(
            const_cast<uint8_t*>(metricsData), metricsLen,
            destinationID, metricsOpts, seq);
        currentTxContext = TxContext::METRICS;
        resonantRadio.send(metricsFrame.frame, metricsFrame.size);
        delete[] metricsFrame.frame;
    }
}

// ============================================================================
// Send Settings Report Frame
// ============================================================================
void sendSettingsFrame(void)
{
    framStorage.flush();
    framStorage.preparePayloads();

    const uint8_t* settingsData = framStorage.getSettingsPayload();
    size_t settingsLen = ResonantFRAMStorage::PAYLOAD_SIZE;

    uint8_t destinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};
    uint8_t* encPayload = nullptr;
    size_t encLen = 0;

    uint8_t opts = ResonantFrame::buildOptionsV1(false);
    uint8_t sensorId[4];
    getDeviceSensorId(sensorId);
    uint32_t seq = framStorage.getNextTxSequenceNumber();

    if (encryption.isInitialized() &&
        encryption.encryptForWire(settingsData, settingsLen,
                       resonantFrame.configAdvertisementFrameType, sensorId, seq,
                       &encPayload, &encLen)) {
        FrameData frame = resonantFrame.buildConfigAdvertisementFrame(
            encPayload, encLen, destinationID, opts, seq);
        currentTxContext = TxContext::SETTINGS_REPORT;
        resonantRadio.send(frame.frame, frame.size);
        delete[] frame.frame;
        delete[] encPayload;
        LOG_I("Encrypted settings report sent (%zu bytes)", encLen);
    } else {
        FrameData frame = resonantFrame.buildConfigAdvertisementFrame(
            const_cast<uint8_t*>(settingsData), settingsLen,
            destinationID, opts, seq);
        currentTxContext = TxContext::SETTINGS_REPORT;
        resonantRadio.send(frame.frame, frame.size);
        delete[] frame.frame;
    }
}

// ============================================================================
// Battery Voltage Filtering
// ============================================================================
void updateBatteryVoltage()
{
    uint16_t rawCV = (uint16_t)(powerManager.getBatteryVoltage() * 100);
    framStorage.setRawBatteryVoltage(rawCV);

    uint16_t storedCV = framStorage.metrics().batteryVoltage;

    if (storedCV == 0) {
        framStorage.setBatteryVoltage(rawCV);
    } else if (rawCV < storedCV) {
        framStorage.setBatteryVoltage(rawCV);
    } else if (rawCV > storedCV + BATTERY_SWAP_DELTA_CV) {
        framStorage.setBatteryVoltage(rawCV);
        LOG_I("Battery swap detected: %u → %u cV", storedCV, rawCV);
    }
}

// ============================================================================
// Pre-Sleep Metric Accumulation
// ============================================================================
void accumulateMetricsBeforeSleep()
{
    framStorage.addTxTime(powerManager.getTxTime());
    framStorage.addRxTime(powerManager.getRxTime());
    framStorage.addActiveTime(powerManager.getIdleTime());
    framStorage.addEnergy((uint32_t)powerManager.getTotalEnergy_uWh());

    powerManager.printEnergyReport();
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

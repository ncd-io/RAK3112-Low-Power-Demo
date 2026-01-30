#include "resonant_lr_radio.h"

// Global instance pointer for static callbacks and ISR
ResonantLRRadio* g_radioInstance = nullptr;
RTC_DATA_ATTR unsigned long transmissionCount = 1;

// ============================================================================
// ISR for DIO1 - notifies radio task
// ============================================================================
void IRAM_ATTR dio1ISR() {
    if (g_radioInstance && g_radioInstance->radioTaskHandle) {
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        vTaskNotifyGiveFromISR(g_radioInstance->radioTaskHandle, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// ============================================================================
// Configuration Presets
// ============================================================================

RadioConfig ResonantLRRadio::getLoRaTelemetryPreset() {
    RadioConfig config;
    config.modem = MODEM_LORA_MODE;
    config.frequency = 915600000;
    config.txPower = 22;
    config.loraBandwidth = 2;           // 500 kHz
    config.loraSpreadingFactor = 7;     // SF7 for speed
    config.loraCodingRate = 1;          // 4/5
    config.loraPreambleLength = 8;
    config.loraIqInversion = false;
    config.txTimeout = 5000;
    config.crcOn = true;
    return config;
}

RadioConfig ResonantLRRadio::getFskBulkPreset() {
    RadioConfig config;
    config.modem = MODEM_FSK_MODE;
    config.frequency = 915600000;
    config.txPower = 22;
    config.fskDatarate = 50000;         // 50 kbps
    config.fskDeviation = 25000;        // 25 kHz
    config.fskBandwidth = 125000;       // 125 kHz
    config.loraPreambleLength = 8;
    config.txTimeout = 5000;
    config.crcOn = true;
    return config;
}

// ============================================================================
// Initialization
// ============================================================================

bool ResonantLRRadio::init(ResonantFrame* frame) {
    return init(frame, getLoRaTelemetryPreset());
}

bool ResonantLRRadio::init(ResonantFrame* frame, const RadioConfig& config) {
    resonantFrame = frame;
    currentConfig = config;
    g_radioInstance = this;
    
    // Create mutexes
    configMutex = xSemaphoreCreateMutex();
    txMutex = xSemaphoreCreateMutex();
    
    if (configMutex == nullptr || txMutex == nullptr) {
        Serial1.println("Failed to create mutexes");
        return false;
    }
    
    // Release RTC GPIO hold from previous sleep cycle
    rtc_gpio_hold_dis((gpio_num_t)LORA_NSS_PIN);
    
    // Initialize hardware
    if (lora_rak3112_init() != 0) {
        Serial1.println("Error in hardware init");
        if (errorCallback) {
            errorCallback(RADIO_ERROR_INIT_FAILED, "Hardware init failed");
        }
        return false;
    }
    
    // Setup internal callbacks
    internalRadioEvents.TxDone = internalOnTxDone;
    internalRadioEvents.TxTimeout = internalOnTxTimeout;
    internalRadioEvents.RxDone = internalOnRxDone;
    internalRadioEvents.RxTimeout = internalOnRxTimeout;
    internalRadioEvents.RxError = internalOnRxError;
    
    Radio.Init(&internalRadioEvents);
    Radio.SetPublicNetwork(true);
    
    // Apply configuration
    applyConfig();
    
    // NOTE: Core 0 task disabled for now - using loop() from main instead
    // This is because SX126x library may not be thread-safe
    // TODO: Re-enable Core 0 task with proper synchronization
    
    /*
    // Create radio task on Core 0
    BaseType_t result = xTaskCreatePinnedToCore(
        radioTaskFunc,
        "RadioTask",
        RADIO_TASK_STACK_SIZE,
        this,
        RADIO_TASK_PRIORITY,
        &radioTaskHandle,
        0  // Core 0
    );
    
    if (result != pdPASS) {
        Serial1.println("Failed to create radio task");
        return false;
    }
    
    // Setup DIO1 interrupt (ESP32 uses pin number directly)
    pinMode(LORA_DIO_1_PIN, INPUT);
    attachInterrupt(LORA_DIO_1_PIN, dio1ISR, RISING);
    
    Serial1.println("ResonantLRRadio initialized on Core 0");
    */
    
    initialized = true;
    Serial1.println("ResonantLRRadio initialized (call loop() from main)");
    return true;
}

// ============================================================================
// Configuration
// ============================================================================

void ResonantLRRadio::setConfig(const RadioConfig& config) {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        currentConfig = config;
        xSemaphoreGive(configMutex);
        applyConfig();
    }
}

RadioConfig ResonantLRRadio::getConfig() {
    RadioConfig config;
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        config = currentConfig;
        xSemaphoreGive(configMutex);
    }
    return config;
}

void ResonantLRRadio::applyConfig() {
    if (xSemaphoreTake(configMutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
        Radio.Standby();
        Radio.SetChannel(currentConfig.frequency);
        
        if (currentConfig.modem == MODEM_LORA_MODE) {
            Radio.SetTxConfig(
                MODEM_LORA,
                currentConfig.txPower,
                0,  // frequency deviation (not used for LoRa)
                currentConfig.loraBandwidth,
                currentConfig.loraSpreadingFactor,
                currentConfig.loraCodingRate,
                currentConfig.loraPreambleLength,
                currentConfig.loraFixLengthPayload,
                currentConfig.crcOn,
                false,  // frequency hopping
                0,      // hop period
                currentConfig.loraIqInversion,
                currentConfig.txTimeout
            );
            
            Radio.SetRxConfig(
                MODEM_LORA,
                currentConfig.loraBandwidth,
                currentConfig.loraSpreadingFactor,
                currentConfig.loraCodingRate,
                0,  // AFC bandwidth (not used for LoRa)
                currentConfig.loraPreambleLength,
                0,  // symbol timeout
                currentConfig.loraFixLengthPayload,
                0,  // payload length (variable)
                currentConfig.crcOn,
                false,  // frequency hopping
                0,      // hop period
                currentConfig.loraIqInversion,
                true    // continuous RX
            );
            Serial1.printf("Config applied: LoRa SF%d BW%d\n", 
                currentConfig.loraSpreadingFactor,
                currentConfig.loraBandwidth == 0 ? 125 : (currentConfig.loraBandwidth == 1 ? 250 : 500));
        } else {
            // FSK mode
            Radio.SetTxConfig(
                MODEM_FSK,
                currentConfig.txPower,
                currentConfig.fskDeviation,
                currentConfig.fskBandwidth,
                currentConfig.fskDatarate,
                0,  // coderate (not used for FSK)
                currentConfig.loraPreambleLength,
                false,  // fixed length
                currentConfig.crcOn,
                false,  // frequency hopping
                0,      // hop period
                false,  // IQ inversion
                currentConfig.txTimeout
            );
            
            Radio.SetRxConfig(
                MODEM_FSK,
                currentConfig.fskBandwidth,
                currentConfig.fskDatarate,
                0,  // coderate
                currentConfig.fskDeviation,
                currentConfig.loraPreambleLength,
                0,  // symbol timeout
                false,  // fixed length
                0,  // payload length
                currentConfig.crcOn,
                false,  // frequency hopping
                0,      // hop period
                false,  // IQ inversion
                true    // continuous RX
            );
            Serial1.printf("Config applied: FSK %d bps\n", currentConfig.fskDatarate);
        }
        
        xSemaphoreGive(configMutex);
    }
}

// ============================================================================
// Callbacks Registration
// ============================================================================

void ResonantLRRadio::onRxComplete(RxCompleteCallback cb) {
    rxCompleteCallback = cb;
}

void ResonantLRRadio::onTxComplete(TxCompleteCallback cb) {
    txCompleteCallback = cb;
}

void ResonantLRRadio::onError(ErrorCallback cb) {
    errorCallback = cb;
}

// ============================================================================
// TX Operations
// ============================================================================

bool ResonantLRRadio::send(uint8_t* data, size_t size) {
    return send(data, size, multiPacketDestinationID, multiPacketFrameAckRequired);
}

bool ResonantLRRadio::send(uint8_t* data, size_t size, uint8_t destinationID[4], bool ackRequired) {
    if (!initialized || transmissionInProgress) {
        return false;
    }
    
    if (xSemaphoreTake(txMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
        return false;
    }
    
    // Copy destination ID
    memcpy(multiPacketDestinationID, destinationID, 4);
    multiPacketFrameAckRequired = ackRequired;
    
    if (size > maxPacketSize) {
        // Multi-packet transmission
        Serial1.printf("Starting multi-packet TX: %zu bytes\n", size);
        multiPacketTxBuffer = new uint8_t[size];
        if (multiPacketTxBuffer == nullptr) {
            xSemaphoreGive(txMutex);
            return false;
        }
        multiPacketTxBufferSize = size;
        multiPacketTxTotalPackets = (size + maxPacketSize - 1) / maxPacketSize;
        multiPacketTxPacketIndex = 0;
        memcpy(multiPacketTxBuffer, data, size);
        transmissionInProgress = true;
        
        sendNextMultiPacket();
    } else {
        // Single packet transmission
        Serial1.printf("Single packet TX: %zu bytes\n", size);
        transmissionInProgress = true;
        Radio.Send(data, size);
        transmissionCount++;
    }
    
    xSemaphoreGive(txMutex);
    return true;
}

void ResonantLRRadio::sendNextMultiPacket() {
    if (multiPacketTxBuffer == nullptr || resonantFrame == nullptr) {
        return;
    }
    
    size_t offset = multiPacketTxPacketIndex * maxPacketSize;
    size_t remaining = multiPacketTxBufferSize - offset;
    size_t packetDataSize = (remaining > maxPacketSize) ? maxPacketSize : remaining;
    
    FrameData frame = resonantFrame->buildMultiPacketFrame(
        multiPacketTxBuffer + offset,
        packetDataSize,
        multiPacketDestinationID,
        multiPacketFrameAckRequired ? 1 : 0,
        multiPacketTxTotalPackets,
        multiPacketTxPacketIndex,
        multiPacketTxBufferSize
    );
    
    Radio.Send(frame.frame, frame.size);
    transmissionCount++;
    
    delete[] frame.frame;
}

bool ResonantLRRadio::continueMultiPacketTransmission() {
    if (!transmissionInProgress || multiPacketTxBuffer == nullptr) {
        return false;
    }
    
    multiPacketTxPacketIndex++;
    
    if (multiPacketTxPacketIndex >= multiPacketTxTotalPackets) {
        // Save stats
        lastMultiPacketDataSize = multiPacketTxBufferSize;
        lastMultiPacketCount = multiPacketTxTotalPackets;
        
        // Cleanup
        delete[] multiPacketTxBuffer;
        multiPacketTxBuffer = nullptr;
        multiPacketTxBufferSize = 0;
        multiPacketTxTotalPackets = 0;
        multiPacketTxPacketIndex = 0;
        transmissionInProgress = false;
        
        return false;  // Complete
    }
    
    sendNextMultiPacket();
    return true;  // More to send
}

// ============================================================================
// RX Operations
// ============================================================================

void ResonantLRRadio::startRx(uint32_t timeout) {
    Radio.Standby();
    receiveInProgress = true;
    Radio.Rx(timeout);
}

void ResonantLRRadio::stopRx() {
    Radio.Standby();
    receiveInProgress = false;
}

// ============================================================================
// State
// ============================================================================

bool ResonantLRRadio::isBusy() {
    return transmissionInProgress || receiveInProgress;
}

bool ResonantLRRadio::isTransmitting() {
    return transmissionInProgress;
}

bool ResonantLRRadio::isReceiving() {
    return receiveInProgress;
}

bool ResonantLRRadio::isTransmissionComplete() const {
    return !transmissionInProgress && multiPacketTxBuffer == nullptr;
}

// ============================================================================
// Power Management
// ============================================================================

void ResonantLRRadio::sleep() {
    Radio.Standby();
    Radio.Sleep();
}

void ResonantLRRadio::wake() {
    Radio.Standby();
    applyConfig();
}

void ResonantLRRadio::deepSleep() {
    Radio.Standby();
    Radio.Sleep();
    SPI.end();
    
    rtc_gpio_hold_en((gpio_num_t)LORA_NSS_PIN);
    pinMode(LORA_NSS_PIN, OUTPUT);
    digitalWrite(LORA_NSS_PIN, HIGH);
    rtc_gpio_hold_en((gpio_num_t)LORA_NSS_PIN);
}

void ResonantLRRadio::lightSleep() {
    Radio.Standby();
    SX126xSetDioIrqParams(
        IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,
        IRQ_RX_DONE | IRQ_RX_TX_TIMEOUT,
        IRQ_RADIO_NONE, IRQ_RADIO_NONE
    );
    Radio.SetRxDutyCycle(2 * 1024 * 1000 * 15.625, 10 * 1024 * 15.625);
    
    rtc_gpio_pulldown_en((gpio_num_t)LORA_DIO_1_PIN);
    rtc_gpio_pullup_en((gpio_num_t)LORA_RESET_PIN);
    rtc_gpio_pullup_en((gpio_num_t)LORA_NSS_PIN);
    
    esp_sleep_enable_ext0_wakeup((gpio_num_t)LORA_DIO_1_PIN, RISING);
    esp_deep_sleep_start();
}

// ============================================================================
// Loop function (call from main loop)
// ============================================================================

void ResonantLRRadio::loop() {
    Radio.IrqProcess();
}

// ============================================================================
// Core 0 Task (currently disabled)
// ============================================================================

void ResonantLRRadio::radioTaskFunc(void* param) {
    ResonantLRRadio* radio = (ResonantLRRadio*)param;
    
    Serial1.println("Radio task started on Core 0");
    
    TickType_t lastAccumCheck = xTaskGetTickCount();
    
    while (true) {
        // Process radio IRQ - must be called frequently
        Radio.IrqProcess();
        
        // Check RX accumulation timeout every 100ms (not every iteration)
        if ((xTaskGetTickCount() - lastAccumCheck) >= pdMS_TO_TICKS(100)) {
            radio->checkRxAccumulationTimeout();
            lastAccumCheck = xTaskGetTickCount();
        }
        
        // Small delay to prevent watchdog and allow other tasks
        // But keep it short for responsive radio handling
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

// ============================================================================
// RX Multi-packet Accumulation
// ============================================================================

void ResonantLRRadio::accumulateMultiPacket(ValidateFrameResult& result, int16_t rssi, int8_t snr) {
    // First packet of a new session?
    if (rxAccumulationBuffer == nullptr) {
        // Estimate buffer size based on total packets and max size
        rxAccumulationBufferSize = result.totalPackets * maxPacketSize;
        rxAccumulationBuffer = new uint8_t[rxAccumulationBufferSize];
        if (rxAccumulationBuffer == nullptr) {
            Serial1.println("Failed to allocate RX accumulation buffer");
            return;
        }
        rxExpectedPackets = result.totalPackets;
        rxReceivedPacketsMask = 0;
        rxAccumulatedSize = 0;
        rxSessionStartTime = millis();
    }
    
    // Check if this packet belongs to current session
    if (result.totalPackets != rxExpectedPackets) {
        Serial1.println("Packet total mismatch, discarding");
        return;
    }
    
    // Check if packet index is valid
    if (result.packetIndex >= rxExpectedPackets) {
        Serial1.println("Invalid packet index");
        return;
    }
    
    // Check if already received this packet
    if (rxReceivedPacketsMask & (1 << result.packetIndex)) {
        Serial1.printf("Duplicate packet %d, ignoring\n", result.packetIndex);
        return;
    }
    
    // Copy data to correct position in buffer
    size_t offset = result.packetIndex * maxPacketSize;
    memcpy(rxAccumulationBuffer + offset, result.data, result.dataLength);
    
    // Mark packet as received
    rxReceivedPacketsMask |= (1 << result.packetIndex);
    
    // Track size for last packet (may be smaller)
    if (result.packetIndex == rxExpectedPackets - 1) {
        rxAccumulatedSize = offset + result.dataLength;
    } else if (rxAccumulatedSize == 0) {
        // Estimate total size
        rxAccumulatedSize = rxExpectedPackets * maxPacketSize;
    }
    
    // Update RSSI/SNR
    rxLastRssi = rssi;
    rxLastSnr = snr;
    
    Serial1.printf("Accumulated packet %d/%d\n", result.packetIndex + 1, rxExpectedPackets);
    
    // Check if all packets received
    uint8_t expectedMask = (1 << rxExpectedPackets) - 1;
    if (rxReceivedPacketsMask == expectedMask) {
        Serial1.println("All packets received, firing callback");
        
        // Create result for callback
        ValidateFrameResult completeResult;
        completeResult.validChecksum = true;
        completeResult.isIntendedDestination = true;
        completeResult.frameType = resonantFrame->multiPacketFrameType;
        completeResult.totalPackets = rxExpectedPackets;
        completeResult.dataLength = rxAccumulatedSize;
        completeResult.data = rxAccumulationBuffer;
        memcpy(completeResult.sourceID, result.sourceID, 4);
        memcpy(completeResult.destinationID, result.destinationID, 4);
        
        // Fire callback
        if (rxCompleteCallback) {
            rxCompleteCallback(completeResult, rxAccumulationBuffer, rxAccumulatedSize, rxLastRssi, rxLastSnr);
        }
        
        // Cleanup
        clearRxAccumulation();
    }
}

void ResonantLRRadio::checkRxAccumulationTimeout() {
    if (rxAccumulationBuffer != nullptr) {
        if (millis() - rxSessionStartTime > rxSessionTimeout) {
            Serial1.println("RX accumulation timeout");
            if (errorCallback) {
                errorCallback(RADIO_ERROR_RX_ACCUMULATION_TIMEOUT, "Multi-packet RX timeout");
            }
            clearRxAccumulation();
        }
    }
}

void ResonantLRRadio::clearRxAccumulation() {
    if (rxAccumulationBuffer != nullptr) {
        delete[] rxAccumulationBuffer;
        rxAccumulationBuffer = nullptr;
    }
    rxAccumulationBufferSize = 0;
    rxAccumulatedSize = 0;
    rxExpectedPackets = 0;
    rxReceivedPacketsMask = 0;
    rxSessionStartTime = 0;
}

// ============================================================================
// Internal Radio Event Handlers (static, called from radio library)
// ============================================================================

void ResonantLRRadio::internalOnTxDone() {
    if (g_radioInstance == nullptr) return;
    
    // Check for multi-packet continuation
    if (g_radioInstance->continueMultiPacketTransmission()) {
        // More packets to send
        return;
    }
    
    // Transmission complete
    g_radioInstance->transmissionInProgress = false;
    
    if (g_radioInstance->txCompleteCallback) {
        size_t bytesSent = g_radioInstance->lastMultiPacketDataSize > 0 
            ? g_radioInstance->lastMultiPacketDataSize 
            : 0;  // Single packet size not tracked currently
        uint8_t packetCount = g_radioInstance->lastMultiPacketCount > 0 
            ? g_radioInstance->lastMultiPacketCount 
            : 1;
        g_radioInstance->txCompleteCallback(true, bytesSent, packetCount);
    }
}

void ResonantLRRadio::internalOnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr) {
    if (g_radioInstance == nullptr || g_radioInstance->resonantFrame == nullptr) return;
    
    g_radioInstance->receiveInProgress = false;
    
    // Validate frame
    ValidateFrameResult result = g_radioInstance->resonantFrame->validateFrame(payload, size);
    
    if (!result.validChecksum || !result.isIntendedDestination) {
        Serial1.println("Frame validation failed, discarding");
        return;
    }
    
    // Check if multi-packet
    if (result.frameType == g_radioInstance->resonantFrame->multiPacketFrameType) {
        g_radioInstance->accumulateMultiPacket(result, rssi, snr);
    } else {
        // Single packet - fire callback immediately
        if (g_radioInstance->rxCompleteCallback) {
            g_radioInstance->rxCompleteCallback(result, result.data, result.dataLength, rssi, snr);
        }
    }
}

void ResonantLRRadio::internalOnTxTimeout() {
    if (g_radioInstance == nullptr) return;
    
    g_radioInstance->transmissionInProgress = false;
    
    // Cleanup multi-packet if in progress
    if (g_radioInstance->multiPacketTxBuffer != nullptr) {
        delete[] g_radioInstance->multiPacketTxBuffer;
        g_radioInstance->multiPacketTxBuffer = nullptr;
        g_radioInstance->multiPacketTxBufferSize = 0;
        g_radioInstance->multiPacketTxTotalPackets = 0;
        g_radioInstance->multiPacketTxPacketIndex = 0;
    }
    
    if (g_radioInstance->txCompleteCallback) {
        g_radioInstance->txCompleteCallback(false, 0, 0);
    }
    
    if (g_radioInstance->errorCallback) {
        g_radioInstance->errorCallback(RADIO_ERROR_TX_TIMEOUT, "TX timeout");
    }
}

void ResonantLRRadio::internalOnRxTimeout() {
    if (g_radioInstance == nullptr) return;
    
    g_radioInstance->receiveInProgress = false;
    
    if (g_radioInstance->errorCallback) {
        g_radioInstance->errorCallback(RADIO_ERROR_RX_TIMEOUT, "RX timeout");
    }
}

void ResonantLRRadio::internalOnRxError() {
    if (g_radioInstance == nullptr) return;
    
    g_radioInstance->receiveInProgress = false;
    
    if (g_radioInstance->errorCallback) {
        g_radioInstance->errorCallback(RADIO_ERROR_RX_ERROR, "RX error");
    }
}

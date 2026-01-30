#ifndef RESONANT_LR_RADIO_H
#define RESONANT_LR_RADIO_H

#include <Arduino.h>
#include <SPI.h>
#include <SX126x-Arduino.h>
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include "resonant_frame.h"

// Forward declaration
class ResonantLRRadio;

// Global instance pointer for ISR access
extern ResonantLRRadio* g_radioInstance;
extern unsigned long transmissionCount;

// ============================================================================
// Radio Configuration
// ============================================================================

enum RadioModem {
    MODEM_LORA_MODE,
    MODEM_FSK_MODE
};

struct RadioConfig {
    RadioModem modem = MODEM_LORA_MODE;
    uint32_t frequency = 915600000;
    int8_t txPower = 22;
    
    // LoRa specific
    uint8_t loraBandwidth = 2;      // 0:125k, 1:250k, 2:500k
    uint8_t loraSpreadingFactor = 7;
    uint8_t loraCodingRate = 1;
    uint16_t loraPreambleLength = 8;
    bool loraFixLengthPayload = false;
    bool loraIqInversion = false;
    
    // FSK specific
    uint32_t fskDatarate = 50000;
    uint32_t fskDeviation = 25000;
    uint32_t fskBandwidth = 125000;
    
    // Common
    uint16_t txTimeout = 5000;
    bool crcOn = true;
};

// ============================================================================
// Callback Types
// ============================================================================

// Callback when complete RX data is ready (after validation)
typedef void (*RxCompleteCallback)(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr);

// Callback when TX completes (single or multi-packet)
typedef void (*TxCompleteCallback)(bool success, size_t bytesSent, uint8_t packetCount);

// Callback for errors
typedef void (*ErrorCallback)(uint8_t errorCode, const char* message);

// Error codes
#define RADIO_ERROR_INIT_FAILED     1
#define RADIO_ERROR_TX_TIMEOUT      2
#define RADIO_ERROR_RX_TIMEOUT      3
#define RADIO_ERROR_RX_ERROR        4
#define RADIO_ERROR_RX_ACCUMULATION_TIMEOUT 5

// Forward declaration for ISR
void IRAM_ATTR dio1ISR();

// ============================================================================
// ResonantLRRadio Class
// ============================================================================

class ResonantLRRadio {
    // Allow ISR to access private members
    friend void dio1ISR();
    
public:
    // ========================================================================
    // Initialization
    // ========================================================================
    bool init(ResonantFrame* frame);
    bool init(ResonantFrame* frame, const RadioConfig& config);
    
    // ========================================================================
    // Configuration (thread-safe, can be called at runtime)
    // ========================================================================
    void setConfig(const RadioConfig& config);
    RadioConfig getConfig();
    void applyConfig();  // Re-applies current config to radio hardware
    
    // Configuration presets
    static RadioConfig getLoRaTelemetryPreset();  // Optimized for small packets
    static RadioConfig getFskBulkPreset();        // Optimized for large transfers
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    void onRxComplete(RxCompleteCallback cb);
    void onTxComplete(TxCompleteCallback cb);
    void onError(ErrorCallback cb);
    
    // ========================================================================
    // TX Operations (called from main)
    // ========================================================================
    bool send(uint8_t* data, size_t size);
    bool send(uint8_t* data, size_t size, uint8_t destinationID[4], bool ackRequired);
    
    // ========================================================================
    // RX Operations
    // ========================================================================
    void startRx(uint32_t timeout = 0);
    void stopRx();
    
    // ========================================================================
    // State
    // ========================================================================
    bool isBusy();
    bool isTransmitting();
    bool isReceiving();
    bool isTransmissionComplete() const;
    
    // ========================================================================
    // Power management
    // ========================================================================
    void sleep();
    void wake();
    void deepSleep();
    void lightSleep();
    
    // ========================================================================
    // Loop function (call from main loop for IRQ processing)
    // ========================================================================
    void loop();
    
    // ========================================================================
    // Multi-packet TX (internal, but needs to be called from callbacks)
    // ========================================================================
    bool continueMultiPacketTransmission();
    
    // Stats from last multi-packet transmission
    size_t lastMultiPacketDataSize = 0;
    uint8_t lastMultiPacketCount = 0;
    
    // Multi-packet TX settings (set before calling send())
    bool multiPacketFrameAckRequired = false;
    uint8_t multiPacketDestinationID[4] = {0xFF, 0xFF, 0xFF, 0xFF};

private:
    // ========================================================================
    // Pin Configuration (RAK3112)
    // ========================================================================
    static constexpr int LORA_RESET_PIN = 8;
    static constexpr int LORA_DIO_1_PIN = 47;
    static constexpr int LORA_BUSY_PIN = 48;
    static constexpr int LORA_NSS_PIN = 7;
    static constexpr int LORA_SCLK_PIN = 5;
    static constexpr int LORA_MISO_PIN = 3;
    static constexpr int LORA_MOSI_PIN = 6;
    static constexpr int LORA_TXEN_PIN = -1;
    static constexpr int LORA_RXEN_PIN = -1;
    
    // ========================================================================
    // Core 0 Task
    // ========================================================================
    static void radioTaskFunc(void* param);
    TaskHandle_t radioTaskHandle = nullptr;
    static constexpr int RADIO_TASK_STACK_SIZE = 4096;
    static constexpr int RADIO_TASK_PRIORITY = 1;
    
    // ========================================================================
    // Thread Safety
    // ========================================================================
    SemaphoreHandle_t configMutex = nullptr;
    SemaphoreHandle_t txMutex = nullptr;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    RadioConfig currentConfig;
    ResonantFrame* resonantFrame = nullptr;
    
    // ========================================================================
    // Callbacks
    // ========================================================================
    RxCompleteCallback rxCompleteCallback = nullptr;
    TxCompleteCallback txCompleteCallback = nullptr;
    ErrorCallback errorCallback = nullptr;
    
    // ========================================================================
    // State
    // ========================================================================
    volatile bool transmissionInProgress = false;
    volatile bool receiveInProgress = false;
    volatile bool initialized = false;
    
    // ========================================================================
    // TX Multi-packet
    // ========================================================================
    uint8_t* multiPacketTxBuffer = nullptr;
    size_t multiPacketTxBufferSize = 0;
    uint8_t multiPacketTxTotalPackets = 0;
    uint8_t multiPacketTxPacketIndex = 0;
    int maxPacketSize = 239;
    
    void sendNextMultiPacket();
    
    // ========================================================================
    // RX Accumulation for multi-packet
    // ========================================================================
    uint8_t* rxAccumulationBuffer = nullptr;
    size_t rxAccumulationBufferSize = 0;
    size_t rxAccumulatedSize = 0;
    uint8_t rxExpectedPackets = 0;
    uint8_t rxReceivedPacketsMask = 0;  // Bitmask of received packets (up to 8)
    uint32_t rxSessionStartTime = 0;
    uint32_t rxSessionTimeout = 5000;
    int16_t rxLastRssi = 0;
    int8_t rxLastSnr = 0;
    
    void accumulateMultiPacket(ValidateFrameResult& result, int16_t rssi, int8_t snr);
    void checkRxAccumulationTimeout();
    void clearRxAccumulation();
    
    // ========================================================================
    // Internal Radio Event Handlers
    // ========================================================================
    static void internalOnTxDone();
    static void internalOnRxDone(uint8_t* payload, uint16_t size, int16_t rssi, int8_t snr);
    static void internalOnTxTimeout();
    static void internalOnRxTimeout();
    static void internalOnRxError();
    
    // Internal RadioEvents structure
    RadioEvents_t internalRadioEvents;
};

#endif // RESONANT_LR_RADIO_H

#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <SPI.h>
#include "resonant_lr_radio.h"
#include "resonant_frame.h"
#include "resonant_power_manager.h"
#include "resonant_storage.h"
#include "resonant_encryption.h"
#include "resonant_log.h"
#include "adoption_handler.h"
#include "Sensor.h"
#include "certs/resonant_ca_cert.h"
#include "certs/device_credentials.h"
#include "ArduinoJson.h"

volatile TxContext currentTxContext = TxContext::NONE;

constexpr uint8_t FIRMWARE_VERSION = 1;
constexpr uint8_t HARDWARE_VERSION = 1;
constexpr uint8_t SENSOR_TYPE = 0x01;

// GCM adds 12-byte IV + 16-byte tag to every encrypted payload
constexpr size_t ENCRYPTION_OVERHEAD = ResonantEncryption::WIRE_OVERHEAD;

// ============================================================================
// Global Instances
// ============================================================================
inline ResonantLRRadio resonantRadio;
inline ResonantFrame resonantFrame;
inline ResonantPowerManager powerManager;
inline ResonantStorage storage;
inline ResonantEncryption encryption;
inline DeviceAdoptionHandler adoptionHandler;
inline TMP112Sensor tempSensor;

// ============================================================================
// Application State
// ============================================================================
inline volatile bool transmissionComplete = false;
inline volatile bool sensorDataReady = false;
inline float lastTemperatureC = 0.0f;
inline bool lastContactClosed = false;

inline bool telemetryAckRequired = false;
inline bool firstBoot = true;
inline bool interruptWake = false;
inline bool contactWake = false;
inline uint32_t txSequenceNumber = 1;
inline uint32_t rxLastSequenceNumber = 0;

// ============================================================================
// Background Tasks
// ============================================================================
inline TaskHandle_t backgroundTask;
void backgroundTasks(void *arg);

// ============================================================================
// Callback Function Declarations
// ============================================================================
void onDataReceived(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr);
void onTxComplete(bool success, size_t bytesSent, uint8_t packetCount);
void onRadioError(uint8_t errorCode, const char* message);
void onSensorDataReady(float temperatureC, bool contactClosed);
void sendEncryptedTelemetry(const uint8_t* payload, size_t payloadLen, uint8_t parentId[4]);
void sendMetricsFrame(void);

// ============================================================================
// Command Processing
// ============================================================================
void handleCommand(uint8_t commandId, uint8_t* params, size_t paramsLength, uint8_t sourceID[4]);

// ============================================================================
// Device Identity Helper
// ============================================================================
void getDeviceSensorId(uint8_t* sensorId);

namespace BootError {
    constexpr uint8_t STORAGE    = (1 << 0);
    constexpr uint8_t ENCRYPTION = (1 << 1);
    constexpr uint8_t RADIO      = (1 << 2);
    constexpr uint8_t ADOPTION   = (1 << 3);
    constexpr uint8_t METRICS    = (1 << 4);
    constexpr uint8_t TELEMETRY  = (1 << 5);
    constexpr uint8_t COMMAND    = (1 << 6);
    constexpr uint8_t SENSOR     = (1 << 7);
}

#endif // MAIN_H

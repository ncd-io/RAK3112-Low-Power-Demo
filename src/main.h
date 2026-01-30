#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include "resonant_lr_radio.h"
#include "resonant_frame.h"

// ============================================================================
// Global Instances
// ============================================================================
extern ResonantLRRadio radio;
extern ResonantFrame resonantFrame;

// ============================================================================
// Configuration
// ============================================================================
#define SLEEP_SECONDS 5
#define uS_TO_S_FACTOR 1000000

// ============================================================================
// Application State
// ============================================================================
extern volatile bool shouldSleep;
extern volatile bool transmissionComplete;

extern bool metricsAckRequired;
extern bool multiPacketDemo;

// ============================================================================
// Timing & Energy Tracking
// ============================================================================
extern unsigned long wakeTimeout;
extern unsigned long preTxTime;
extern unsigned long txStartTime;
extern unsigned long timeOnAir;
extern unsigned long ackStartTime;
extern unsigned long ackTime;

extern float preTxCurrentDraw;
extern float txCurrentDraw;
extern float ackCurrentDraw;

// ============================================================================
// Callback Function Declarations
// ============================================================================
void onDataReceived(ValidateFrameResult& result, uint8_t* data, size_t dataLength, int16_t rssi, int8_t snr);
void onTxComplete(bool success, size_t bytesSent, uint8_t packetCount);
void onRadioError(uint8_t errorCode, const char* message);

// ============================================================================
// Application Function Declarations
// ============================================================================
void goToSleep(void);

// ============================================================================
// Test Data
// ============================================================================
extern const char* genesis;

#endif // MAIN_H

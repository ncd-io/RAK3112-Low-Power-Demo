#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>
#include <SPI.h>
#include <esp_sleep.h>
#include "resonant_lr_radio.h"
#include "resonant_frame.h"

// ============================================================================
// Configuration
// ============================================================================
#define SLEEP_SECONDS 5
#define uS_TO_S_FACTOR 1000000

// ============================================================================
// Global Instances
// ============================================================================
inline ResonantLRRadio resonantRadio;
inline ResonantFrame resonantFrame;

// ============================================================================
// Application State
// ============================================================================
inline volatile bool shouldSleep = false;
inline volatile bool transmissionComplete = false;

inline bool metricsAckRequired = false;
inline bool multiPacketDemo = true;

// ============================================================================
// Timing & Energy Tracking
// ============================================================================
inline unsigned long wakeTimeout = 5000;
inline unsigned long preTxTime = 0;
inline unsigned long txStartTime = 0;
inline unsigned long timeOnAir = 0;
inline unsigned long ackStartTime = 0;
inline unsigned long ackTime = 0;

inline float preTxCurrentDraw = 40.7;
inline float txCurrentDraw = 212.0;
inline float ackCurrentDraw = 69.3;

// Energy buffer - stored in RTC memory (survives deep sleep)
// Defined in main.cpp due to RTC_DATA_ATTR
extern RTC_DATA_ATTR float energyBuffer;

// ============================================================================
// Test Data (Genesis text for multi-packet demo)
// ============================================================================
inline const char* genesis = "\nIn the beginning God created the heaven and the earth.\nAnd the earth was without form, and void; and darkness was upon the face of the deep. And the Spirit of God moved upon the face of the waters.\nAnd God said, Let there be light: and there was light.\nAnd God saw the light, that it was good: and God divided the light from the darkness.\nAnd God called the light Day, and the darkness he called Night. And the evening and the morning were the first day.\nAnd God said, Let there be a firmament in the midst of the waters, and let it divide the waters from the waters.\nAnd God made the firmament, and divided the waters which were under the firmament from the waters which were above the firmament: and it was so.\nAnd God called the firmament Heaven. And the evening and the morning were the second day.\nAnd God said, Let the waters under the heaven be gathered together unto one place, and let the dry land appear: and it was so.\nAnd God called the dry land Earth; and the gathering together of the waters called he Seas: and God saw that it was good.\nAnd God said, Let the earth bring forth grass, the herb yielding seed, and the fruit tree yielding fruit after his kind, whose seed is in itself, upon the earth: and it was so.\nAnd the earth brought forth grass, and herb yielding seed after his kind, and the tree yielding fruit, whose seed was in itself, after his kind: and God saw that it was good.\nAnd the evening and the morning were the third day.\nAnd God said, Let there be lights in the firmament of the heaven to divide the day from the night; and let them be for signs, and for seasons, and for days, and years:\nAnd let them be for lights in the firmament of the heaven to give light upon the earth: and it was so.\nAnd God made two great lights; the greater light to rule the day, and the lesser light to rule the night: he made the stars also.\nAnd God set them in the firmament of the heaven to give light upon the earth,\nAnd to rule over the day and over the night, and to divide the light from the darkness: and God saw that it was good.\nAnd the evening and the morning were the fourth day.";

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

// ============================================================================
// Application Function Declarations
// ============================================================================
void goToSleep(void);

#endif // MAIN_H

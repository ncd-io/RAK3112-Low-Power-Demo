#ifndef RESONANT_LOG_H
#define RESONANT_LOG_H

#include <Arduino.h>

// Log levels: 0=off, 1=error, 2=warn, 3=info, 4=debug
// Override via -D RESONANT_LOG_LEVEL=N in platformio.ini build_flags
#ifndef RESONANT_LOG_LEVEL
#define RESONANT_LOG_LEVEL 3
#endif

// Serial port used for debug output (Serial1 = external UART on ESP32-S3)
#ifndef RESONANT_LOG_SERIAL
#define RESONANT_LOG_SERIAL Serial1
#endif

#if RESONANT_LOG_LEVEL >= 1
#define LOG_E(fmt, ...) RESONANT_LOG_SERIAL.printf("[E] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_E(fmt, ...) ((void)0)
#endif

#if RESONANT_LOG_LEVEL >= 2
#define LOG_W(fmt, ...) RESONANT_LOG_SERIAL.printf("[W] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_W(fmt, ...) ((void)0)
#endif

#if RESONANT_LOG_LEVEL >= 3
#define LOG_I(fmt, ...) RESONANT_LOG_SERIAL.printf(fmt "\n", ##__VA_ARGS__)
#else
#define LOG_I(fmt, ...) ((void)0)
#endif

#if RESONANT_LOG_LEVEL >= 4
#define LOG_D(fmt, ...) RESONANT_LOG_SERIAL.printf("[D] " fmt "\n", ##__VA_ARGS__)
#else
#define LOG_D(fmt, ...) ((void)0)
#endif

#endif // RESONANT_LOG_H

# RAK3112 Low Power Deep Sleep Test

This project tests the low power capabilities of the RAK3112 WisDuo LPWAN+BLE+Wi-Fi Module. The module wakes up for 10 seconds, then enters deep sleep for 5 seconds in a continuous loop.

## Project Goals

- **Test Cycle**: Wake for 10 seconds, sleep for 5 seconds (continuous loop)
- **Target**: Minimize deep sleep current consumption
- **Future Application**: 10-minute sleep, <5-second wake for data transmission
- **Battery Life Goal**: 5 years with 9000mAh @ 3.3V battery

## Power Consumption Targets

For a 5-year battery life with 9000mAh @ 3.3V:
- **Maximum Average Current**: ~205 µA
  - Calculation: 9000mAh / (5 years × 365 days × 24 hours) = 205.5 µA

For the final application (10 min sleep / <5 sec wake):
- **Deep Sleep Current**: < 10 µA (typical ESP32-S3 deep sleep)
- **Wake Current**: Must be minimized during data transmission
- **Average Current**: (sleep_current × sleep_time + wake_current × wake_time) / total_time

## Hardware Setup

1. Connect RAK3112 module to power supply (3.3V)
2. Connect antennas (LoRa and Wi-Fi/BLE) - **REQUIRED** to prevent RF damage
3. For power measurement: Connect multimeter in series with power supply

## Software Setup

### Build and Upload

```bash
# Build the project
pio run -e rak3112-lowpower

# Upload to device
pio run -e rak3112-lowpower -t upload

# Monitor serial output
pio device monitor -e rak3112-lowpower
```

### Configuration

The test uses the `rak3112-lowpower` environment which is optimized for minimal power consumption:
- No unnecessary libraries
- Optimized compiler flags (-Os, -ffunction-sections, etc.)
- Minimal build configuration

## Power Measurement Instructions

### Equipment Needed
- Digital multimeter with µA range capability
- Power supply (3.3V) or battery
- Current measurement setup (multimeter in series)

### Measurement Steps

1. **Setup Current Measurement**
   - Connect multimeter in series with power supply
   - Set multimeter to µA range (if available) or mA range
   - Ensure proper polarity

2. **Measure Wake Current**
   - During the 10-second wake period, observe current draw
   - Typical values: 50-150 mA (depending on CPU frequency and peripherals)
   - Note the peak and average values

3. **Measure Deep Sleep Current**
   - During the 5-second sleep period, observe current draw
   - Target: < 10 µA (typical ESP32-S3 deep sleep)
   - If higher, check for:
     - LEDs still on
     - Peripherals not disabled
     - GPIO pins not properly configured
     - External components drawing current

4. **Calculate Average Current**
   ```
   Average Current = (wake_current × wake_time + sleep_current × sleep_time) / total_time
   
   Example:
   - Wake: 100 mA for 10 seconds
   - Sleep: 5 µA for 5 seconds
   - Average = (100mA × 10s + 0.005mA × 5s) / 15s = 66.7 mA
   ```

### Expected Results

**Deep Sleep Current (ESP32-S3 typical)**:
- Minimum: 5-10 µA (with all peripherals disabled)
- Acceptable: < 20 µA
- If > 50 µA: Check for issues (see troubleshooting)

**Wake Current**:
- Depends on CPU frequency and active peripherals
- With optimizations: 50-100 mA @ 80 MHz
- Without optimizations: 100-200 mA @ 240 MHz

## Code Features

### Power Optimizations Implemented

1. **WiFi Disabled**: WiFi.mode(WIFI_OFF), esp_wifi_stop(), esp_wifi_deinit()
2. **Bluetooth Disabled**: esp_bt_controller_disable(), esp_bt_controller_deinit()
3. **LEDs Disabled**: Set to INPUT mode, pulled LOW
4. **CPU Frequency**: Reduced to 80 MHz (minimum for ESP32-S3)
5. **ADC Disabled**: Automatically disabled in deep sleep
6. **Dynamic Power Management**: Configured for lowest power mode
7. **RTC GPIO Hold**: Released before sleep to prevent current leakage

### Timing Configuration

Edit these constants in `src/main.cpp`:
```cpp
#define WAKE_DURATION_SECONDS 10    // Time to stay awake
#define SLEEP_DURATION_SECONDS 5    // Time to sleep
```

## Troubleshooting

### High Sleep Current (> 50 µA)

1. **Check LEDs**: Ensure LEDs are set to INPUT mode
2. **Check GPIO Pins**: All unused GPIOs should be set to INPUT with pull-up/down as needed
3. **Check External Components**: Disconnect any external sensors/modules
4. **Check USB**: Disconnect USB cable (USB can draw current even when not in use)
5. **Check Antennas**: Ensure antennas are connected (required for RF safety)

### Serial Monitor Not Working After Sleep

- This is normal - USB CDC may disconnect during deep sleep
- Press reset button or reconnect USB to restore serial output
- For production, remove Serial.begin() and all Serial.print() statements

### Power Management Configuration Errors

- If you see "Power management config failed", the ESP32-S3 variant may not support all power management features
- The code will continue without power management optimization
- Deep sleep should still work correctly

## Future Optimizations for Production

For the final application (10 min sleep / <5 sec wake):

1. **Remove Serial Debugging**: Remove all Serial.print() statements
2. **Minimize Wake Time**: Optimize code to complete in < 5 seconds
3. **LoRa Transmission**: Add LoRaWAN transmission during wake period
4. **Sensor Reading**: Add sensor reading code (if needed)
5. **RTC Wake Source**: Consider using RTC timer instead of deep sleep timer for more precise timing
6. **External Wake Sources**: Configure GPIO wake sources if needed (button press, sensor interrupt, etc.)

## References

- [RAK3112 Quick Start Guide](https://docs.rakwireless.com/product-categories/wisduo/rak3112-module/quickstart/)
- [RAK Wireless WisBlock Examples](https://github.com/RAKWireless/WisBlock/tree/master/examples/RAK3112)
- [ESP32-S3 Deep Sleep Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s3/api-reference/system/sleep_modes.html)

## License

This project is for testing and development purposes.


#ifndef TMP112_SENSOR_H
#define TMP112_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

class TMP112Sensor {
public:
    using DataReadyCallback = void (*)(float temperatureC);

    bool begin(TwoWire& wire = Wire, uint8_t addr = 0x48);
    void loop();
    void requestReading();
    void onDataReady(DataReadyCallback cb);

    bool sensorOperational = false;

private:
    TwoWire* _wire = nullptr;
    uint8_t _addr = 0x48;
    DataReadyCallback _callback = nullptr;
    bool _readingRequested = false;

    static constexpr uint8_t REG_TEMPERATURE = 0x00;
    static constexpr uint8_t REG_CONFIG      = 0x01;

    float readTemperature();
};

#endif // TMP112_SENSOR_H

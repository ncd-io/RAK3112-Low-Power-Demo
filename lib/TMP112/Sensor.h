#ifndef TMP112_SENSOR_H
#define TMP112_SENSOR_H

#include <Arduino.h>
#include <Wire.h>

class TMP112Sensor {
public:
    using DataReadyCallback = void (*)(float temperatureC, bool contactClosed);

    bool begin(TwoWire& wire = Wire, uint8_t addr = 0x48);
    void setContactPin(uint8_t pin);
    void loop();
    void requestReading();
    void onDataReady(DataReadyCallback cb);

    bool sensorOperational = false;
    bool contactClosed = false;

private:
    TwoWire* _wire = nullptr;
    uint8_t _addr = 0x48;
    DataReadyCallback _callback = nullptr;
    bool _readingRequested = false;

    int8_t _contactPin = -1;

    static constexpr uint8_t REG_TEMPERATURE = 0x00;
    static constexpr uint8_t REG_CONFIG      = 0x01;

    float readTemperature();
    bool readContact();
};

#endif // TMP112_SENSOR_H

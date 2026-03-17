#include "Sensor.h"

bool TMP112Sensor::begin(TwoWire& wire, uint8_t addr) {
    _wire = &wire;
    _addr = addr;

    _wire->begin(SDA, SCL);

    _wire->beginTransmission(_addr);
    uint8_t error = _wire->endTransmission();

    sensorOperational = (error == 0);
    return sensorOperational;
}

void TMP112Sensor::setContactPin(uint8_t pin) {
    _contactPin = (int8_t)pin;
    pinMode(pin, INPUT_PULLUP);
}

void TMP112Sensor::onDataReady(DataReadyCallback cb) {
    _callback = cb;
}

void TMP112Sensor::requestReading() {
    _readingRequested = true;
}

void TMP112Sensor::loop() {
    if (!_readingRequested || !sensorOperational) {
        return;
    }
    _readingRequested = false;

    float tempC = readTemperature();
    contactClosed = readContact();

    if (_callback != nullptr) {
        _callback(tempC, contactClosed);
    }
}

float TMP112Sensor::readTemperature() {
    _wire->beginTransmission(_addr);
    _wire->write(REG_TEMPERATURE);
    _wire->endTransmission(false);

    _wire->requestFrom(_addr, (uint8_t)2);
    if (_wire->available() < 2) {
        return -999.0f;
    }

    uint8_t msb = _wire->read();
    uint8_t lsb = _wire->read();

    int16_t raw = (msb << 4) | (lsb >> 4);
    if (raw & 0x800) {
        raw |= 0xF000;
    }

    return raw * 0.0625f;
}

bool TMP112Sensor::readContact() {
    if (_contactPin < 0) {
        return false;
    }
    return digitalRead(_contactPin) == LOW;
}

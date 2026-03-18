#include "MB85RS64V.h"

bool MB85RS64V::begin(SPIClass& spi, uint8_t csPin,
                      int8_t misoPin, int8_t mosiPin,
                      int8_t sckPin, uint32_t spiFreq)
{
    _spi = &spi;
    _csPin = csPin;
    _spiFreq = spiFreq;

    pinMode(_csPin, OUTPUT);
    csDeselect();

    if (misoPin >= 0 && mosiPin >= 0 && sckPin >= 0) {
        _spi->begin(sckPin, misoPin, mosiPin, -1);
    } else {
        _spi->begin();
    }

    delayMicroseconds(500);

    deviceOperational = verifyDeviceId();
    return deviceOperational;
}

bool MB85RS64V::verifyDeviceId()
{
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_RDID);
    uint8_t mfgId  = _spi->transfer(0x00);
    uint8_t cont   = _spi->transfer(0x00);
    uint8_t prod1  = _spi->transfer(0x00);
    uint8_t prod2  = _spi->transfer(0x00);
    csDeselect();
    _spi->endTransaction();

    return (mfgId == EXPECTED_MFG_ID &&
            cont  == EXPECTED_CONT   &&
            prod1 == EXPECTED_PROD_1 &&
            prod2 == EXPECTED_PROD_2);
}

uint8_t MB85RS64V::readByte(uint16_t addr)
{
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_READ);
    _spi->transfer((addr >> 8) & 0xFF);
    _spi->transfer(addr & 0xFF);
    uint8_t val = _spi->transfer(0x00);
    csDeselect();
    _spi->endTransaction();
    return val;
}

void MB85RS64V::writeByte(uint16_t addr, uint8_t data)
{
    enableWrite();
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_WRITE);
    _spi->transfer((addr >> 8) & 0xFF);
    _spi->transfer(addr & 0xFF);
    _spi->transfer(data);
    csDeselect();
    _spi->endTransaction();
}

void MB85RS64V::read(uint16_t addr, uint8_t* buf, size_t len)
{
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_READ);
    _spi->transfer((addr >> 8) & 0xFF);
    _spi->transfer(addr & 0xFF);
    for (size_t i = 0; i < len; i++) {
        buf[i] = _spi->transfer(0x00);
    }
    csDeselect();
    _spi->endTransaction();
}

void MB85RS64V::write(uint16_t addr, const uint8_t* buf, size_t len)
{
    enableWrite();
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_WRITE);
    _spi->transfer((addr >> 8) & 0xFF);
    _spi->transfer(addr & 0xFF);
    for (size_t i = 0; i < len; i++) {
        _spi->transfer(buf[i]);
    }
    csDeselect();
    _spi->endTransaction();
}

uint8_t MB85RS64V::readStatus()
{
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_RDSR);
    uint8_t sr = _spi->transfer(0x00);
    csDeselect();
    _spi->endTransaction();
    return sr;
}

void MB85RS64V::writeStatus(uint8_t status)
{
    enableWrite();
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_WRSR);
    _spi->transfer(status);
    csDeselect();
    _spi->endTransaction();
}

bool MB85RS64V::selfTest()
{
    if (!deviceOperational) return false;

    static constexpr uint16_t TEST_ADDR = 0x0000;
    static constexpr size_t   TEST_LEN  = 8;

    uint8_t original[TEST_LEN];
    read(TEST_ADDR, original, TEST_LEN);

    uint8_t pattern[TEST_LEN];
    for (size_t i = 0; i < TEST_LEN; i++) {
        pattern[i] = (uint8_t)(0xA5 ^ i);
    }
    write(TEST_ADDR, pattern, TEST_LEN);

    uint8_t readback[TEST_LEN];
    read(TEST_ADDR, readback, TEST_LEN);

    write(TEST_ADDR, original, TEST_LEN);

    for (size_t i = 0; i < TEST_LEN; i++) {
        if (readback[i] != pattern[i]) return false;
    }
    return true;
}

void MB85RS64V::csSelect()
{
    digitalWrite(_csPin, LOW);
}

void MB85RS64V::csDeselect()
{
    digitalWrite(_csPin, HIGH);
}

void MB85RS64V::enableWrite()
{
    _spi->beginTransaction(SPISettings(_spiFreq, MSBFIRST, SPI_MODE0));
    csSelect();
    _spi->transfer(OP_WREN);
    csDeselect();
    _spi->endTransaction();
}

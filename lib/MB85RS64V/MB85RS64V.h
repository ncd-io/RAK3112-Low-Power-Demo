#ifndef MB85RS64V_H
#define MB85RS64V_H

#include <Arduino.h>
#include <SPI.h>

class MB85RS64V {
public:
    bool begin(SPIClass& spi, uint8_t csPin,
               int8_t misoPin = -1, int8_t mosiPin = -1,
               int8_t sckPin = -1, uint32_t spiFreq = 8000000);

    bool verifyDeviceId();

    uint8_t readByte(uint16_t addr);
    void    writeByte(uint16_t addr, uint8_t data);

    void read(uint16_t addr, uint8_t* buf, size_t len);
    void write(uint16_t addr, const uint8_t* buf, size_t len);

    uint8_t readStatus();
    void    writeStatus(uint8_t status);

    bool selfTest();

    bool deviceOperational = false;

    static constexpr uint16_t MEMORY_SIZE = 8192;

private:
    SPIClass* _spi = nullptr;
    uint8_t   _csPin = 0;
    uint32_t  _spiFreq = 8000000;

    static constexpr uint8_t OP_WREN  = 0x06;
    static constexpr uint8_t OP_WRDI  = 0x04;
    static constexpr uint8_t OP_RDSR  = 0x05;
    static constexpr uint8_t OP_WRSR  = 0x01;
    static constexpr uint8_t OP_READ  = 0x03;
    static constexpr uint8_t OP_WRITE = 0x02;
    static constexpr uint8_t OP_RDID  = 0x9F;

    static constexpr uint8_t EXPECTED_MFG_ID  = 0x04;
    static constexpr uint8_t EXPECTED_CONT    = 0x7F;
    static constexpr uint8_t EXPECTED_PROD_1  = 0x03;
    static constexpr uint8_t EXPECTED_PROD_2  = 0x02;

    void    csSelect();
    void    csDeselect();
    void    enableWrite();
};

#endif // MB85RS64V_H

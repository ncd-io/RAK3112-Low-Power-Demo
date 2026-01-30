#ifndef RESONANT_FRAME_H
#define RESONANT_FRAME_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <Arduino.h>
#include <esp_mac.h>
// Structure to hold frame data and its size
struct FrameData {
    uint8_t* frame;
    size_t size;
    
    FrameData() : frame(nullptr), size(0) {}
    FrameData(uint8_t* f, size_t s) : frame(f), size(s) {}
};

//Structure to hold frame used as return to function validateFrame.  Should return type of frame, destination id, source id, data, and data length.
struct ValidateFrameResult {
    bool validChecksum;
    bool isIntendedDestination;
    uint8_t frameType;
    uint8_t options;
    uint8_t destinationID[4];
    uint8_t sourceID[4];
    uint8_t* data = nullptr;
    size_t dataLength = 0;
    uint8_t totalPackets = 1;
    uint8_t packetIndex = 0;
};

class ResonantFrame {
    public:
    uint8_t discoveryAdvertisementFrameType = 0x00;
    uint8_t telemetryFrameType = 0x01;
    uint8_t metricsFrameType = 0x02;
    uint8_t commandResponseFrameType = 0x03;
    uint8_t configAdvertisementFrameType = 0x04;
    uint8_t acknowledgementFrameType = 0x05;
    uint8_t multiPacketFrameType = 0x06;
    uint8_t multiPacketAcknowledgementFrameType = 0x07;
    FrameData buildDiscoveryFrame(uint16_t sensorType, uint8_t hardwareVersion, uint8_t firmwareVersion);
    FrameData buildTelemetryFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options);
    FrameData buildMetricsFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options);
    FrameData buildCommandResponseFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options);
    FrameData buildConfigAdvertisementFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options);
    FrameData buildAcknowledgementFrame(uint8_t destinationID[4], uint8_t options);
    FrameData buildMultiPacketFrame(uint8_t* data, size_t dataLength, uint8_t destinationID[4], uint8_t options, uint8_t totalPackets = 1, uint8_t packetIndex = 0, size_t totalDataSize = 0);
    FrameData buildMultiPacketAcknowledgementFrame(uint8_t* data, size_t dataLength, uint8_t destinationID[4], uint8_t options);
    ValidateFrameResult validateFrame(uint8_t* frame, size_t frameSize);
    
    private:
    size_t frameOverhead = 16;
    uint8_t headerByte = 0x85;
    uint8_t destinationBroadcastID[4] = {0xff, 0xff, 0xff, 0xff};
    void frameConstructor(uint8_t* frame, uint8_t frameType, uint8_t* data, uint8_t* destinationID, uint8_t dataLength, uint8_t options, uint8_t totalPackets=1, uint8_t packetIndex=0, size_t totalDataSize=0);
    size_t calculateFrameSize(uint8_t dataLength);
    bool validateChecksum(uint8_t* frame, size_t frameSize);

};
#endif

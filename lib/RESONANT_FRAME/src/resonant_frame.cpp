#include "resonant_frame.h"
#include <esp_efuse.h>

size_t ResonantFrame::calculateFrameSize(uint8_t dataLength) {
    return frameOverhead + dataLength;
}

FrameData ResonantFrame::buildDiscoveryFrame(uint16_t sensorType, uint8_t hardwareVersion, uint8_t firmwareVersion) {
    Serial1.println("Build discovery frame called");
    // Discovery frame data: 4 bytes (sensorType is 2 bytes, hardwareVersion 1, firmwareVersion 1)
    const uint8_t discoveryDataLength = 4;
    uint8_t data[discoveryDataLength];
    data[0] = (sensorType >> 8) & 0xFF;
    data[1] = sensorType & 0xFF;
    data[2] = hardwareVersion;
    data[3] = firmwareVersion;

    uint8_t destinationID[4] = {0xff, 0xff, 0xff, 0xff};
    uint8_t options = 0;
    
    size_t frameSize = calculateFrameSize(discoveryDataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, discoveryAdvertisementFrameType, data, destinationID, discoveryDataLength, options);
    
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildTelemetryFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build telemetry frame called");
    size_t frameSize = calculateFrameSize(dataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, telemetryFrameType, data, destinationID, dataLength, options);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildMetricsFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build metrics frame called");
    size_t frameSize = calculateFrameSize(dataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, metricsFrameType, data, destinationID, dataLength, options);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildCommandResponseFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build command response frame called");
    size_t frameSize = calculateFrameSize(dataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, commandResponseFrameType, data, destinationID, dataLength, options);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildConfigAdvertisementFrame(uint8_t* data, uint8_t dataLength, uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build config advertisement frame called");
    size_t frameSize = calculateFrameSize(dataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, configAdvertisementFrameType, data, destinationID, dataLength, options);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildAcknowledgementFrame(uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build acknowledgement frame called");
    uint8_t acknowledgementData[1] = {170};
    size_t frameSize = calculateFrameSize(1);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, acknowledgementFrameType, acknowledgementData, destinationID, 1, options);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildMultiPacketFrame(uint8_t* data, size_t dataLength, uint8_t destinationID[4], uint8_t options, uint8_t totalPackets, uint8_t packetIndex, size_t totalDataSize) {
    // Serial1.println("Build multi-packet frame called");
    size_t frameSize = calculateFrameSize(dataLength); // 3 bytes for total packets and packet index and additional data length byte
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, multiPacketFrameType, data, destinationID, dataLength, options, totalPackets, packetIndex, totalDataSize);
    return FrameData(frame, frameSize);
}

FrameData ResonantFrame::buildMultiPacketAcknowledgementFrame(uint8_t* data, size_t dataLength, uint8_t destinationID[4], uint8_t options) {
    Serial1.println("Build multi-packet acknowledgement frame called");
    size_t frameSize = calculateFrameSize(dataLength);
    uint8_t* frame = new uint8_t[frameSize];
    frameConstructor(frame, multiPacketAcknowledgementFrameType, data, destinationID, dataLength, options);
    return FrameData(frame, frameSize);
}

void ResonantFrame::frameConstructor(uint8_t* frame, uint8_t frameType, uint8_t* data, uint8_t* destinationID, uint8_t dataLength, uint8_t options, uint8_t totalPackets, uint8_t packetIndex, size_t totalDataSize){
    // Serial1.println("Frame constructor called");
    
    // Packet length includes all bytes after header byte and length field (2 bytes)
    // For multi-packet: sourceID(4) + destID(4) + frameType(1) + options(1) + totalDataSize(2) + totalPackets(1) + packetIndex(1) + data(dataLength) + checksum(1)
    // For regular: sourceID(4) + destID(4) + frameType(1) + options(1) + dataLength(1) + data(dataLength) + checksum(1)
    uint16_t packetLength;
    packetLength = dataLength + frameOverhead; // 15 + dataLength
    // Serial1.printf("packet length: %d\n", packetLength);
    frame[0] = headerByte;
    frame[1] = (packetLength >> 8) & 0xFF;
    frame[2] = packetLength & 0xFF;

    //get mac of esp32 module and use last 4 bytes as source ID
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    memcpy(frame + 3, mac + 2, 4);
    //copy over 4 bytes of destination ID
    memcpy(frame + 7, destinationID, 4);
    frame[11] = frameType;
    frame[12] = options;
    frame[13] = totalPackets;
    frame[14] = packetIndex;
    memcpy(frame + 15, data, dataLength);
    //calculate checksum: add all bytes not including header byte, packet length bytes, and checksum byte
    // Checksum covers: sourceID(4) + frameType(1) + destinationID(4) + data(dataLength)
    uint32_t checksum = 0;
    for(int i = 3; i < packetLength-1; i++) {
        checksum += frame[i];
    }
    uint8_t checksumByte = checksum & 0xFF;
    // Store checksum at the end (after all data)
    frame[packetLength - 1] = checksumByte;
    // Serial1.printf("frame constructor: frame type: 0x%02X, packet length: %d, checksum: 0x%02X\n", frameType, packetLength, checksumByte);
    // Serial1.printf("frame: ");
    // for(int i = 0; i < packetLength; i++) {
    //     Serial1.printf("0x%02X ", frame[i]);
    // }
    // Serial1.println();
}

ValidateFrameResult ResonantFrame::validateFrame(uint8_t* frame, size_t frameSize) {
    Serial1.println("Validate frame called");
    ValidateFrameResult result;
    result.validChecksum = false;
    result.isIntendedDestination = false;
    
    // Basic frame size check (minimum frame size: header + length + sourceID + destID + frameType + options + data(1) + checksum = 1+2+4+4+1+1+1+1 = 15)
    if(frameSize < 15) {
        Serial1.println("Frame size too small");
        return result;
    }
    Serial1.println("Calling validate checksum");
    bool validChecksum = validateChecksum(frame, frameSize);
    Serial1.printf("Valid checksum: %d\n", validChecksum);
    result.validChecksum = validChecksum;
    memcpy(result.sourceID, frame + 3, 4);
    memcpy(result.destinationID, frame + 7, 4);
    result.frameType = frame[11];
    result.options = frame[12];
    result.totalPackets = frame[13];
    result.packetIndex = frame[14];
    result.dataLength = frameSize - frameOverhead;
    result.data = frame + frameOverhead-1;

    // Get this device's source ID (last 4 bytes of MAC address)
    uint8_t mac[6];
    uint8_t deviceSourceID[4];
    esp_efuse_mac_get_default(mac);
    memcpy(deviceSourceID, mac + 2, 4);
    
    //Check destinationID.  Only valid if destinationID is 0xff, 0xff, 0xff, 0xff (broadcast) or this device's source ID
    if(memcmp(result.destinationID, destinationBroadcastID, 4) != 0 && memcmp(result.destinationID, deviceSourceID, 4) != 0) {
        result.isIntendedDestination = false;
    }else{
        result.isIntendedDestination = true;
    }
    Serial1.printf("Result: valid checksum: %d, is intended destination: %d\n", result.validChecksum, result.isIntendedDestination);
    return result;
}

bool ResonantFrame::validateChecksum(uint8_t* frame, size_t frameSize) {
    uint32_t checksum = 0;
    for(int i = 3; i < frameSize - 1; i++) {
        checksum += frame[i];
    }
    uint8_t checksumByte = checksum & 0xFF;
    return checksumByte == frame[frameSize - 1];
}

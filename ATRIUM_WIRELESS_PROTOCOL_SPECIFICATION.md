# Atrium Wireless Protocol Specification v1.0

## Document Purpose

This specification defines the wireless communication protocol for Atrium sensor and gateway devices. It serves as:

1. **Engineering Reference**: Complete technical specification for developing Atrium-compatible wireless devices
2. **Implementation Guide**: Detailed requirements for firmware development
3. **AI Agent Context**: Reference document for AI-assisted development in Cursor IDE

All Atrium wireless devices MUST comply with this specification to ensure interoperability, security, and reliable operation.

---

## Table of Contents

1. [Physical Layer](#1-physical-layer)
2. [Network Topology](#2-network-topology)
3. [MAC Layer Protocol](#3-mac-layer-protocol)
4. [Security Layer](#4-security-layer)
5. [Application Layer](#5-application-layer)
6. [OTA Firmware Update Protocol](#6-ota-firmware-update-protocol)
7. [Power Management](#7-power-management)
8. [Error Handling & Reliability](#8-error-handling--reliability)
9. [Gateway Protocol](#9-gateway-protocol)
10. [Implementation Requirements](#10-implementation-requirements)
11. [Compliance & Standards](#11-compliance--standards)

---

## 1. Physical Layer

### 1.1 Radio Configuration

- **Chip**: SX1262 (RAK3112 module)
- **Frequency Band**: 
  - **US**: 902-928 MHz (915 MHz center frequency)
  - **EU**: 863-870 MHz (868 MHz center frequency)
  - **AS**: 915-928 MHz (varies by country)
- **Modulation**: LoRa (Chirp Spread Spectrum)

### 1.2 PHY Profiles

Three physical layer profiles are defined for different use cases:

#### Profile A - Normal Operation (Sensor Telemetry)
- **Spreading Factor**: 7
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/5
- **Preamble**: 8 bytes
- **Data Rate**: ~5.5 kbps
- **Range**: Up to 5 km (urban), 15 km (rural)
- **Use Case**: Standard sensor telemetry, configuration, commands

#### Profile B - OTA Updates (High Throughput)
- **Spreading Factor**: 7
- **Bandwidth**: 250 kHz
- **Coding Rate**: 4/5
- **Preamble**: 8 bytes
- **Data Rate**: ~11 kbps
- **Range**: Up to 2 km (urban), 5 km (rural)
- **Use Case**: Firmware updates, large data transfers

#### Profile C - Long Range (Emergency/Discovery)
- **Spreading Factor**: 12
- **Bandwidth**: 125 kHz
- **Coding Rate**: 4/8
- **Preamble**: 12 bytes
- **Data Rate**: ~250 bps
- **Range**: Up to 15 km (urban), 40 km (rural)
- **Use Case**: Device discovery, emergency communications, initial registration

### 1.3 Power Levels

- **TX Power**: 2-22 dBm (configurable, region-dependent)
- **Duty Cycle**: Must comply with regional regulations
  - **US915**: 0.1% duty cycle (or frequency hopping)
  - **EU868**: 1% duty cycle (or frequency hopping)
  - **AS923**: Varies by country

### 1.4 Frequency Hopping (Optional)

- Use frequency hopping to increase duty cycle limits
- 50+ channels available in 915 MHz band
- Pseudo-random sequence synchronized between gateway and sensors
- Channel selection based on device ID and time

---

## 2. Network Topology

### 2.1 Star Network

- **Gateway**: Central coordinator, always-on receiver
- **Sensors**: End devices, sleep/wake cycle
- **Communication**: 
  - **Uplink**: Sensors → Gateway
  - **Downlink**: Gateway → Sensors

### 2.2 Device Addressing

#### Device ID Format (32-bit)

```
Bits [31:24] - Device Type
  0x01 = Sensor
  0x02 = Gateway
  0x03 = Output Device
  0x04-0xFF = Reserved

Bits [23:16] - Product Line
  0x01 = Atrium
  0x02-0xFF = Reserved

Bits [15:0]  - Unique Serial Number
  Assigned during manufacturing
```

#### Network ID (16-bit)

- Assigned by gateway during registration
- Allows multiple gateways in same area
- **Broadcast Address**: 0xFFFF
- **Gateway Address**: 0x0000 (reserved)

### 2.3 Device Roles

**Gateway**:
- Always-on receiver (continuous listening)
- Central coordinator for all sensors
- Time reference for network
- Data aggregation point
- OTA update coordinator

**Sensor**:
- Sleep/wake cycle operation
- Low power consumption
- Scheduled telemetry transmission
- Responds to gateway commands

---

## 3. MAC Layer Protocol

### 3.1 Frame Structure

```
┌─────────────┬──────────────┬─────────────┬─────────────┬─────────────┬─────────────┐
│   Preamble  │    Header    │   Payload   │    MIC      │    CRC      │
│   (8 bytes) │  (16 bytes)  │  (0-242)    │  (4 bytes)  │  (2 bytes)  │
└─────────────┴──────────────┴─────────────┴─────────────┴─────────────┴─────────────┘
```

**Total Frame Size**: 8 + 16 + payload + 4 + 2 = 30 + payload bytes
**Maximum Payload**: 242 bytes (LoRa packet limit)

### 3.2 Frame Header (16 bytes)

```
Byte 0-1:   Frame Control (see below)
Byte 2-3:   Sequence Number (16-bit, wraps at 65535)
Byte 4-7:   Destination Device ID (32-bit)
Byte 8-11:  Source Device ID (32-bit)
Byte 12-13: Payload Length (0-242, 16-bit)
Byte 14-15: Reserved/Future Use (must be 0x0000)
```

#### Frame Control (16 bits)

```
Bit 15:     Direction
            0 = Uplink (Sensor → Gateway)
            1 = Downlink (Gateway → Sensor)

Bit 14:     Acknowledgment Required
            0 = No acknowledgment needed
            1 = Acknowledgment required

Bit 13:     Retransmission
            0 = Original transmission
            1 = Retransmission

Bit 12-8:   Message Type (5 bits, see Message Types table)
            0x00 = Reserved
            0x01 = DATA
            0x02 = ACK
            0x03 = REGISTER
            0x04 = REGISTER_RESP
            0x05 = TIME_SYNC_REQUEST
            0x06 = TIME_SYNC_RESPONSE
            0x07 = TIME_SYNC_BROADCAST
            0x10 = OTA_START
            0x11 = OTA_CHUNK
            0x12 = OTA_ACK
            0x13 = OTA_COMPLETE
            0x14 = OTA_ABORT
            0x20 = PING
            0x21 = PING_RESP
            0x30 = CONFIG
            0x31 = CONFIG_RESP
            0xFF = ERROR

Bit 7-4:    PHY Profile (4 bits)
            0 = Profile A (Normal Operation)
            1 = Profile B (OTA Updates)
            2 = Profile C (Long Range)
            3-15 = Reserved

Bit 3-0:    Reserved (must be 0)
```

### 3.3 Message Types

| Type | Value | Direction | ACK Required | Description |
|------|-------|-----------|-------------|-------------|
| DATA | 0x01 | Uplink/Downlink | Yes | Sensor telemetry or gateway data |
| ACK | 0x02 | Both | No | Acknowledgment |
| REGISTER | 0x03 | Uplink | Yes | Device registration request |
| REGISTER_RESP | 0x04 | Downlink | No | Registration response |
| TIME_SYNC_REQUEST | 0x05 | Uplink | Yes | Time synchronization request |
| TIME_SYNC_RESPONSE | 0x06 | Downlink | No | Time sync response |
| TIME_SYNC_BROADCAST | 0x07 | Downlink | No | Unsolicited time broadcast |
| OTA_START | 0x10 | Downlink | Yes | OTA update initiation |
| OTA_CHUNK | 0x11 | Downlink | Yes | OTA firmware chunk |
| OTA_ACK | 0x12 | Uplink | No | OTA chunk acknowledgment |
| OTA_COMPLETE | 0x13 | Downlink | Yes | OTA update completion |
| OTA_ABORT | 0x14 | Both | Yes | OTA update abort |
| PING | 0x20 | Both | Yes | Keep-alive/heartbeat |
| PING_RESP | 0x21 | Both | No | Ping response |
| CONFIG | 0x30 | Downlink | Yes | Configuration update |
| CONFIG_RESP | 0x31 | Uplink | No | Configuration response |
| ERROR | 0xFF | Both | No | Error message |

### 3.4 Sequence Numbers

- Each device maintains independent sequence counter
- Increments on each transmission (except ACK)
- Used for duplicate detection and ordering
- Wraps at 65535
- Gateway tracks last sequence from each sensor

### 3.5 Acknowledgment Mechanism

- Messages with ACK_REQUIRED flag must be acknowledged
- ACK must be sent within 2 seconds
- ACK contains:
  - Original message sequence number
  - Status (SUCCESS/ERROR)
- Retry logic:
  - Max 3 retries
  - Exponential backoff: 1s, 2s, 4s

---

## 4. Security Layer

### 4.1 ATECC608A Integration

**REQUIRED**: All Atrium devices MUST include ATECC608A secure element.

#### Encryption
- **Algorithm**: AES-128-CCM (Counter with CBC-MAC)
- **Key Derivation**: ECDH using ATECC608A
- **Session Keys**: Derived per session, rotated every 24 hours
- **Key Length**: 128 bits

#### Authentication
- **Device Certificate**: Stored in ATECC608A (unique per device)
- **Gateway Certificate**: Verified by sensors
- **Message Authentication**: 4-byte MIC (Message Integrity Code)
- **Certificate Authority**: Atrium Root CA

#### Key Management
- **Root CA**: Atrium Certificate Authority
- **Device Keys**: Unique per device, stored in ATECC608A (never exposed)
- **Session Keys**: Derived from device keys + nonce
- **Key Rotation**: Automatic every 24 hours or on security event

### 4.2 Security Flow

#### Registration (First-Time Pairing)

```
1. Sensor → Gateway: REGISTER
   - Device ID
   - Device Certificate (from ATECC608A)
   - Device Public Key
   - Current sensor time (if available)

2. Gateway:
   - Verifies device certificate via ATECC608A
   - Validates certificate chain
   - Generates session key material
   - Assigns Network ID

3. Gateway → Sensor: REGISTER_RESP
   - Network ID
   - Gateway Certificate
   - Session Key Material (encrypted with device public key)
   - Gateway time (for initial sync)
   - Security parameters

4. Sensor:
   - Verifies gateway certificate
   - Derives session key from material
   - Stores session key securely
   - Sets system time
```

#### Message Encryption

All messages (except REGISTER) are encrypted:

1. **Encrypt Payload**: AES-128-CCM encryption
2. **Calculate MIC**: Over header + encrypted payload
3. **Add MIC**: Append 4-byte MIC to frame
4. **Sequence Number**: Prevents replay attacks

#### Session Key Rotation

- Automatic rotation every 24 hours
- Initiated by gateway
- New session key derived from device keys + new nonce
- Old session key invalidated after rotation

### 4.3 Security Requirements

**MUST**:
- Encrypt all payloads (except REGISTER)
- Authenticate all messages (MIC)
- Verify certificates on registration
- Rotate session keys periodically
- Never expose private keys
- Use secure random number generation

**MUST NOT**:
- Transmit unencrypted sensitive data
- Reuse sequence numbers
- Accept messages with invalid MIC
- Store keys in plaintext

---

## 5. Application Layer

### 5.1 Sensor Telemetry Format

#### DATA Message Payload

```
Byte 0-3:   Timestamp (Unix epoch, 32-bit seconds)
Byte 4:     Sensor Type
            0x01 = Temperature
            0x02 = Humidity
            0x03 = Pressure
            0x04 = Light
            0x05 = Motion
            0x06 = Air Quality
            0x07 = Custom Sensor 1
            0x08 = Custom Sensor 2
            0x09-0xFF = Reserved

Byte 5:     Data Format
            0x01 = Float32 (IEEE 754)
            0x02 = Int16 (signed)
            0x03 = Int32 (signed)
            0x04 = UInt16 (unsigned)
            0x05 = UInt32 (unsigned)
            0x06 = Boolean
            0x07-0xFF = Reserved

Byte 6-9:   Sensor Value (format-dependent, 4 bytes)
Byte 10-N:  Additional sensor data (optional, variable length)
            - Multiple sensors in one packet
            - Metadata (battery level, signal strength, etc.)
```

#### Example: Temperature Sensor

```
Timestamp:     0x5F8A3C00 (1609459200 = 2021-01-01 00:00:00 UTC)
Sensor Type:   0x01 (Temperature)
Data Format:   0x01 (Float32)
Sensor Value:  0x420C0000 (35.0°C as IEEE 754 float)
```

### 5.2 Time Synchronization

**REQUIRED**: All sensors MUST implement time synchronization using Enhanced NTP approach.

#### Time Sync Protocol (Enhanced NTP)

**Implementation**: Option 2 - Enhanced NTP with multiple round-trip measurements for higher accuracy.

#### TIME_SYNC_REQUEST (Sensor → Gateway)

**Payload** (8 bytes):
```
Byte 0-3:   Sensor Current Time (Unix timestamp, seconds)
Byte 4-7:   Sensor Send Timestamp (millis() when sent, for RTT calculation)
```

#### TIME_SYNC_RESPONSE (Gateway → Sensor)

**Payload** (16 bytes):
```
Byte 0-3:   Gateway Time When Received (Unix timestamp, seconds)
Byte 4-7:   Gateway Time When Sending (Unix timestamp, seconds)
Byte 8-11:  Sensor Original Timestamp (echoed back, seconds)
Byte 12-13: Timezone Offset (signed int16, minutes from UTC)
Byte 14-15: Next Sync Time (Unix timestamp, seconds, when to sync next)
```

#### Time Sync Calculation

```cpp
// Sensor receives TIME_SYNC_RESPONSE
void processTimeSyncResponse(TimeSyncResponse* resp) {
    uint32_t now = millis();
    uint32_t rtt = now - resp->sensorOriginalTimestamp;  // Round-trip time
    
    // Calculate clock offset
    // offset = ((T2 - T1) + (T3 - T4)) / 2
    // Where:
    //   T1 = Sensor send time (from request)
    //   T2 = Gateway receive time (from response)
    //   T3 = Gateway send time (from response)
    //   T4 = Sensor receive time (now)
    
    int32_t offset = ((resp->gatewayTimeReceived - requestTime) + 
                      (resp->gatewayTimeSending - now)) / 2;
    
    // Adjust sensor time
    time_t adjustedTime = resp->gatewayTimeSending + offset;
    setSystemTime(adjustedTime, resp->timezoneOffset);
    
    // Schedule next sync
    time_t nextSyncTime = resp->nextSyncTime;
    scheduleTimeSync(nextSyncTime);
}
```

#### Time Sync Schedule

- **Initial Sync**: During device registration
- **Periodic Sync**: Every 24 hours (as specified in TIME_SYNC_RESPONSE)
- **Next Sync Time**: Gateway specifies exact Unix timestamp for next sync
- **Drift Compensation**: Sensor tracks clock drift and adjusts accordingly

#### Clock Maintenance

**REQUIRED**: Use ESP32-S3 built-in RTC (no external crystal required).

**Implementation**:
```cpp
// Before deep sleep
struct tm timeinfo;
getLocalTime(&timeinfo);
time_t sleepUntil = mktime(&timeinfo) + sleepSeconds;

// Set RTC alarm for wake
esp_sleep_enable_timer_wakeup((sleepUntil - time(NULL)) * 1000000ULL);

// After wake
time_t currentTime = time(NULL);  // RTC maintained time during sleep
```

**Clock Drift**:
- ESP32-S3 RTC drift: ~20-50 ppm (typical)
- Daily drift: ~1.7-4.3 seconds per day
- Re-sync every 24 hours compensates for drift
- Gateway may adjust sync interval based on observed drift

#### Scheduled Transmissions

With absolute time, sensors can schedule exact transmission times:

```cpp
// Schedule transmission for exact time (e.g., 14:24:00)
struct tm nextTx;
getLocalTime(&nextTx);
nextTx.tm_min += 1;
nextTx.tm_sec = 0;
time_t nextTxTime = mktime(&nextTx);

// Calculate sleep duration
time_t now = time(NULL);
uint32_t sleepSeconds = nextTxTime - now;

// Sleep until exact time
esp_sleep_enable_timer_wakeup(sleepSeconds * 1000000ULL);
esp_deep_sleep_start();
```

#### Gateway Time Management

- Gateway maintains accurate time (NTP sync, GPS, or manual)
- Gateway responds to time sync requests immediately
- Gateway may broadcast time during sensor wake windows (optional)
- Gateway tracks sensor clock drift and adjusts sync intervals

### 5.3 Device Registration

#### REGISTER Message (Sensor → Gateway)

**Payload** (variable length):
```
Byte 0-3:   Device ID
Byte 4-67:  Device Certificate (64 bytes, from ATECC608A)
Byte 68-99: Device Public Key (32 bytes)
Byte 100-103: Current Sensor Time (Unix timestamp, 0 if unknown)
Byte 104-107: Firmware Version (32-bit)
Byte 108-N:  Device Capabilities (optional, variable)
```

#### REGISTER_RESP Message (Gateway → Sensor)

**Payload** (variable length):
```
Byte 0-1:   Network ID (assigned)
Byte 2-5:   Gateway Time (Unix timestamp, for initial sync)
Byte 6-69:  Gateway Certificate (64 bytes)
Byte 70-101: Session Key Material (32 bytes, encrypted)
Byte 102-105: Security Parameters
Byte 106-109: Next Time Sync Time (Unix timestamp)
Byte 110-N:  Gateway Configuration (optional)
```

### 5.4 Configuration Management

#### CONFIG Message (Gateway → Sensor)

**Payload** (variable length):
```
Byte 0:     Config Parameter ID
Byte 1-4:   Config Value (format depends on parameter)
Byte 5-N:   Additional parameters (optional)
```

#### CONFIG_RESP Message (Sensor → Gateway)

**Payload** (4 bytes):
```
Byte 0:     Config Parameter ID
Byte 1:     Status (0x00=Success, 0x01=Error, 0x02=Invalid)
Byte 2-3:   Error Code (if status != Success)
```

---

## 6. OTA Firmware Update Protocol

### 6.1 OTA Overview

**Challenge**: LoRa data rates are slow (5-11 kbps)
**Solution**: 
- Use Profile B (250 kHz, SF7) for higher throughput (~11 kbps)
- Chunk firmware into small packets (200 bytes max)
- Resume capability for interrupted updates
- Delta updates when possible (future enhancement)

### 6.2 OTA Update Flow

#### Phase 1: Initiation

```
Gateway → Sensor: OTA_START
  Payload:
    Byte 0-3:   Firmware Version (32-bit)
    Byte 4-7:   Total Size (bytes)
    Byte 8-11:  Chunk Count
    Byte 12-43: Firmware Checksum (SHA-256, 32 bytes)
    Byte 44-47: Update Window Start (Unix timestamp)
    Byte 48-51: Update Window End (Unix timestamp)
    Byte 52:    Update Type (0x01=Full, 0x02=Delta)
    Byte 53-N:  Additional metadata (optional)

Sensor → Gateway: OTA_ACK
  Payload:
    Byte 0:     Status (0x00=Accept, 0x01=Reject, 0x02=Busy)
    Byte 1-4:   Last Received Chunk (if resuming)
    Byte 5-8:   Available Buffer Size
```

#### Phase 2: Transfer

```
Gateway → Sensor: OTA_CHUNK
  Payload:
    Byte 0-3:   Chunk Index (0-based, 32-bit)
    Byte 4-5:   Chunk Size (bytes, 16-bit, max 200)
    Byte 6-7:   Chunk Checksum (CRC16)
    Byte 8-N:   Chunk Data (variable, max 200 bytes)

Sensor → Gateway: OTA_ACK
  Payload:
    Byte 0-3:   Chunk Index
    Byte 4:     Status (0x00=OK, 0x01=ERROR, 0x02=RETRY)
    Byte 5-6:   Error Code (if status != OK)
    Byte 7-10:  Next Expected Chunk (for resume)
```

#### Phase 3: Completion

```
Gateway → Sensor: OTA_COMPLETE
  Payload:
    Byte 0-3:   Final Checksum Verification
    Byte 4:     Reboot Command (0x00=No, 0x01=Yes)

Sensor → Gateway: OTA_ACK
  Payload:
    Byte 0:     Status (0x00=SUCCESS, 0x01=FAILED)
    Byte 1-4:   Verification Result
```

### 6.3 OTA Optimization Strategies

1. **Chunk Size**: 200 bytes (fits in single LoRa packet with overhead)
2. **Windowed Transfer**: Extended listen windows during OTA (5-10 seconds)
3. **Resume Capability**: Sensor tracks received chunks, requests retransmission
4. **Delta Updates**: Only transfer changed blocks (requires bootloader support, future)
5. **Compression**: Compress firmware binary before transfer (future)
6. **Scheduled Updates**: Perform during low-activity periods

### 6.4 OTA Timing Example

**Assumptions**:
- Firmware size: 256 KB
- Chunk size: 200 bytes
- Total chunks: 1,280
- Data rate: 11 kbps (Profile B)
- Effective payload rate: ~8 kbps (with overhead)

**Calculation**:
- Time per chunk: ~200ms (200 bytes @ 8 kbps)
- Total transfer time: ~256 seconds (~4.3 minutes)
- With 50% retry rate: ~6-8 minutes

**Strategy**:
- Schedule during maintenance window
- Use extended listen mode (sensor stays awake)
- Implement chunk buffering in sensor
- Verify chunks as received

### 6.5 Bootloader Requirements

**REQUIRED**: All sensors MUST implement bootloader with OTA support.

**Bootloader Design**:
```
┌─────────────────┐
│   Bootloader    │  ← Handles OTA, firmware verification
│   (16-32 KB)    │
├─────────────────┤
│   Application   │  ← Sensor firmware
│   (256 KB+)     │
└─────────────────┘
```

**Bootloader Functions**:
1. Receive OTA chunks
2. Write to secondary flash partition
3. Verify complete firmware (SHA-256)
4. Swap partitions on next boot
5. Fall back to previous firmware on failure
6. Verify firmware signature (ATECC608A)

---

## 7. Power Management

### 7.1 Sensor Sleep Modes

#### Deep Sleep
- **Radio**: Off
- **MCU**: Deep sleep mode
- **Wake Sources**: Timer, external interrupt
- **Power Consumption**: <10 µA
- **Use Case**: Long sleep periods (hours)

#### Light Sleep
- **Radio**: Off
- **MCU**: Light sleep mode
- **Wake Sources**: Timer
- **Power Consumption**: <100 µA
- **Use Case**: Short sleep periods (seconds/minutes)

#### Active Receive
- **Radio**: Listening
- **MCU**: Active
- **Power Consumption**: ~15-20 mA
- **Use Case**: Waiting for gateway messages

#### Active Transmit
- **Radio**: Transmitting
- **MCU**: Active
- **Power Consumption**: ~120-150 mA (at 22 dBm)
- **Use Case**: Sending telemetry or responses

### 7.2 Wake Schedule

- Sensors wake at scheduled intervals (based on absolute time)
- **Normal Listen Window**: 500ms
- **OTA Listen Window**: 5-10 seconds
- Gateway maintains wake schedule for each sensor
- Time synchronization ensures predictable wake times

### 7.3 Power Optimization

**Strategies**:
- Minimize active time
- Use lowest SF that provides required range
- Batch multiple sensor readings in one transmission
- Use scheduled wake times (not polling)
- Gateway-initiated downlink only during wake windows

---

## 8. Error Handling & Reliability

### 8.1 Acknowledgment Mechanism

- Messages with ACK_REQUIRED flag must be acknowledged
- Gateway ACKs all uplink messages
- Sensors ACK critical downlink messages
- **Timeout**: 2 seconds
- **Max Retries**: 3
- **Backoff**: Exponential (1s, 2s, 4s)

### 8.2 Error Codes

| Code | Value | Description |
|------|-------|-------------|
| SUCCESS | 0x00 | Operation successful |
| INVALID_FRAME | 0x01 | Frame format invalid |
| AUTH_FAILED | 0x02 | Authentication failed |
| DECRYPT_FAILED | 0x03 | Decryption failed |
| INVALID_SEQUENCE | 0x04 | Sequence number invalid |
| BUFFER_FULL | 0x05 | Receive buffer full |
| NOT_REGISTERED | 0x06 | Device not registered |
| OTA_ERROR | 0x07 | OTA update error |
| TIME_SYNC_ERROR | 0x08 | Time sync failed |
| CONFIG_ERROR | 0x09 | Configuration error |
| UNKNOWN_ERROR | 0xFF | Unknown error |

### 8.3 Error Message Format

**ERROR Message Payload** (4 bytes):
```
Byte 0:     Error Code (see table above)
Byte 1-3:   Additional Error Information (context-dependent)
```

### 8.4 Retry Logic

**Uplink (Sensor → Gateway)**:
- Sensor retries on timeout or error
- Max 3 retries
- Exponential backoff
- Gateway deduplicates by sequence number

**Downlink (Gateway → Sensor)**:
- Gateway retries on timeout
- Max 3 retries
- Sensor deduplicates by sequence number
- Gateway queues message for next wake window

---

## 9. Gateway Protocol

### 9.1 Gateway Responsibilities

1. **Always-On Receiver**: Continuous listening on configured frequency
2. **Device Registration**: Authenticate and register new sensors
3. **Time Synchronization**: Maintain network time, sync sensors every 24 hours
4. **Data Aggregation**: Collect telemetry, store in SQLite database
5. **OTA Management**: Coordinate firmware updates
6. **Web Interface**: Serve ReactJS dashboard via NodeJS backend
7. **Cloud Integration**: Relay data to cloud service (optional)

### 9.2 Gateway Message Queue

- Maintain queue of downlink messages per sensor
- Send during sensor wake windows
- **Priority Order**:
  1. OTA messages
  2. CONFIG messages
  3. TIME_SYNC messages
  4. DATA requests
  5. PING messages

### 9.3 Gateway Time Management

- Gateway maintains accurate time (NTP sync, GPS, or manual)
- Gateway responds to time sync requests immediately
- Gateway may broadcast time during sensor wake windows (optional)
- Gateway tracks sensor clock drift and adjusts sync intervals
- Gateway specifies next sync time in TIME_SYNC_RESPONSE (24 hours from current sync)

### 9.4 Gateway Database Schema

**Sensors Table**:
- Device ID
- Network ID
- Registration Time
- Last Seen Time
- Firmware Version
- Current Interval
- Next Wake Time
- Clock Drift Estimate

**Telemetry Table**:
- Device ID
- Timestamp
- Sensor Type
- Sensor Value
- Signal Strength (RSSI)
- Signal Quality (SNR)

---

## 10. Implementation Requirements

### 10.1 Required Hardware

**All Atrium Devices MUST Include**:
- SX1262 radio (RAK3112 module)
- ATECC608A secure element
- ESP32-S3 MCU (or compatible)
- Sufficient flash for bootloader + application + OTA partition

### 10.2 Required Software Components

**Bootloader**:
- OTA update support
- Firmware verification (SHA-256)
- Partition management
- Fallback on failure

**Application**:
- Protocol stack implementation
- Time synchronization
- Power management
- Sensor data collection
- ATECC608A integration

**Libraries**:
- SX126x-Arduino (or equivalent)
- ATECC608A library
- Crypto libraries (AES-128-CCM)
- Time/Date libraries

### 10.3 Code Structure Requirements

**MUST Implement**:
- Frame encoding/decoding
- Encryption/decryption
- Message authentication
- Sequence number management
- Acknowledgment handling
- Time synchronization
- Power management
- OTA update handling

**MUST NOT**:
- Expose private keys
- Store keys in plaintext
- Skip security checks
- Ignore sequence numbers
- Transmit unencrypted data (except REGISTER)

### 10.4 Testing Requirements

**MUST Test**:
- Registration flow
- Time synchronization accuracy
- Message encryption/decryption
- OTA update process
- Power consumption
- Range performance
- Error handling
- Retry logic

---

## 11. Compliance & Standards

### 11.1 Regional Compliance

**US (FCC Part 15)**:
- Frequency: 902-928 MHz
- Duty cycle: 0.1% or frequency hopping
- Power limit: 30 dBm EIRP

**EU (ETSI EN 300 220)**:
- Frequency: 863-870 MHz
- Duty cycle: 1% or frequency hopping
- Power limit: 14 dBm EIRP

**AS (Country-specific)**:
- Varies by country
- Check local regulations

### 11.2 Frequency Hopping

- Use frequency hopping to increase duty cycle limits
- 50+ channels available in 915 MHz band
- Pseudo-random sequence
- Synchronized between gateway and sensors

### 11.3 Certification

- All devices must be certified for target region
- FCC (US), CE (EU), etc.
- Radio compliance testing required

---

## Appendix A: Message Format Examples

### A.1 DATA Message (Temperature Sensor)

```
Frame Control:    0x0101 (Uplink, ACK required, DATA, Profile A)
Sequence:         0x0042
Destination:      0x00000000 (Gateway)
Source:           0x01010001 (Sensor, Serial 1)
Payload Length:   10
Payload:
  0x5F8A3C00  // Timestamp: 1609459200
  0x01        // Sensor Type: Temperature
  0x01        // Format: Float32
  0x420C0000  // Value: 35.0°C
MIC:          0x12345678
CRC:          0xABCD
```

### A.2 TIME_SYNC_RESPONSE

```
Frame Control:    0x8106 (Downlink, TIME_SYNC_RESPONSE, Profile A)
Sequence:         0x00A1
Destination:      0x01010001 (Sensor)
Source:           0x00000000 (Gateway)
Payload Length:   16
Payload:
  0x5F8A3C00  // Gateway time received
  0x5F8A3C01  // Gateway time sending
  0x5F8A3BFE  // Sensor original timestamp
  0xFFD8      // Timezone: -40 minutes (signed)
  0x5F8B0000  // Next sync: 24 hours later
MIC:          0x9ABCDEF0
CRC:          0x1234
```

---

## Appendix B: Implementation Checklist

### For Sensor Developers

- [ ] Implement frame encoding/decoding
- [ ] Integrate ATECC608A for security
- [ ] Implement time synchronization (Enhanced NTP)
- [ ] Implement power management
- [ ] Implement OTA bootloader
- [ ] Implement registration flow
- [ ] Implement telemetry transmission
- [ ] Test range and reliability
- [ ] Verify power consumption
- [ ] Test OTA update process
- [ ] Verify time sync accuracy
- [ ] Test error handling
- [ ] Regional compliance testing

### For Gateway Developers

- [ ] Implement always-on receiver
- [ ] Implement device registration
- [ ] Implement time synchronization server
- [ ] Implement message queue
- [ ] Implement SQLite database
- [ ] Implement ReactJS dashboard
- [ ] Implement NodeJS backend
- [ ] Implement CloudFlare tunnel
- [ ] Implement cloud integration (optional)
- [ ] Test with multiple sensors
- [ ] Test OTA coordination
- [ ] Performance testing

---

## Document Version History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2024-01-XX | Initial specification |

---

## Contact & Support

For questions or clarifications regarding this specification, contact the Atrium development team.

**This document is the authoritative source for Atrium wireless protocol implementation.**


# V1 Resonant Sensor - Wire Format Specification

**Firmware Version**: 1  
**Hardware Version**: 1  
**Protocol Version**: 1 (Wire Protocol v1)  
**Sensor Type**: 0x01  
**Encryption**: AES-128-GCM (ECDH-derived session key, ATECC_MOCK / mbedTLS)

---

## Table of Contents

1. [Common Frame Structure](#1-common-frame-structure)
2. [Options Byte](#2-options-byte)
3. [Encryption Layer](#3-encryption-layer)
4. [Telemetry Frame (0x01)](#4-telemetry-frame-0x01)
5. [Metrics Frame (0x02)](#5-metrics-frame-0x02)
6. [Settings Report Frame (0x04)](#6-settings-report-frame-0x04)
7. [Transmission Behavior](#7-transmission-behavior)
8. [Decoding Examples](#8-decoding-examples)

---

## 1. Common Frame Structure

All frames share a 20-byte overhead structure defined by `RESONANT_FRAME`. The data payload is appended after the fixed header, followed by a 1-byte checksum.

```
 Offset   Length   Field
 ──────   ──────   ─────────────────────────────────────────
 0        1        Header byte (fixed: 0x85)
 1-2      2        Packet length (big-endian, bytes from offset 3 to end inclusive)
 3-6      4        Source ID (last 4 bytes of device MAC)
 7-10     4        Destination ID (parent ID from adoption, or FF:FF:FF:FF broadcast)
 11       1        Frame type
 12       1        Options byte (see Section 2)
 13-16    4        Sequence number (big-endian, incrementing counter)
 17       1        Total packets (0x01 for single-packet frames)
 18       1        Packet index (0x00 for single-packet frames)
 19..N    variable Data payload (plaintext or encrypted, see Section 3)
 N+1      1        Checksum (sum of bytes [3..N] & 0xFF)
```

**Frame overhead**: 20 bytes (19 header + 1 checksum)  
**Total frame size**: 20 + payload length  
**Packet length field**: total_frame_size - 3  
**Max application payload (encrypted)**: 207 bytes (255 − 20 frame − 28 GCM)

---

## 2. Options Byte

```
 Bit 7-5:  Protocol version (currently 0b001 = v1)
 Bit 4-1:  Reserved (0)
 Bit 0:    ACK requested (1 = yes, 0 = no)
```

| Options Value | Meaning |
|---------------|---------|
| `0x20` | Protocol v1, no ACK |
| `0x21` | Protocol v1, ACK requested |

---

## 3. Encryption Layer

When encryption is active (adopted device with valid session key), the application payload is encrypted using AES-128-GCM before being placed into the frame's data payload field.

### Wire Overhead

| Constant | Value | Description |
|----------|-------|-------------|
| GCM_IV_SIZE | 12 bytes | Initialization vector / nonce |
| GCM_TAG_SIZE | 16 bytes | Authentication tag |
| WIRE_OVERHEAD | 28 bytes | IV + Tag appended to every encrypted payload |

### Encrypted Payload Layout

```
 Offset   Length       Field
 ──────   ──────       ─────────────────────────
 0        12           GCM IV (random nonce)
 12       N            Ciphertext (same length as plaintext)
 12+N     16           GCM Authentication Tag
```

**Encrypted payload size** = plaintext_size + 28

### Additional Authenticated Data (AAD)

AAD is computed but **not transmitted**. Both sender and receiver construct it identically from frame header fields. It binds the ciphertext to the frame context, preventing replay or frame-type substitution attacks.

```
 Offset   Length   Field
 ──────   ──────   ─────────────────────────
 0        1        Frame type (e.g. 0x01)
 1-4      4        Sensor ID (source ID)
 5-8      4        Sequence number (big-endian)
```

**AAD size**: 9 bytes (fixed)

### Plaintext Fallback

If encryption is unavailable (not adopted, key derivation failed), the application payload is sent unencrypted. The frame structure is identical; only the data payload content differs.

---

## 4. Telemetry Frame (0x01)

Sent every wake cycle. Contains sensor readings.

### Application Payload (3 bytes plaintext)

```
 Byte   Field              Type       Description
 ────   ─────              ────       ───────────────────────────────────────
 0      Temperature MSB    int16_t    Signed, temperature_C * 100, high byte
 1      Temperature LSB    int16_t    Signed, temperature_C * 100, low byte
 2      Contact Status     uint8_t    0x01 = closed (GPIO LOW), 0x00 = open (GPIO HIGH)
```

**Temperature encoding**: Multiply degrees Celsius by 100, cast to signed 16-bit integer, store big-endian.

| Example Temp | Encoded (hex) |
|-------------|---------------|
| 25.31 C | `0x09E3` |
| -5.50 C | `0xFDDE` |
| 0.00 C | `0x0000` |

**Contact status**: Active-low input with internal pull-up. GPIO LOW (grounded) = `0x01` (closed). GPIO HIGH (floating/open) = `0x00` (open).

### Encrypted Telemetry Payload (31 bytes)

```
 Offset   Length   Field
 ──────   ──────   ─────────────────────────
 0-11     12       GCM IV
 12-14    3        Ciphertext (encrypted temperature + contact)
 15-30    16       GCM Authentication Tag
```

### Complete Telemetry Frame (Encrypted)

**Total size**: 51 bytes

```
 Offset   Length   Field                 Example Value
 ──────   ──────   ─────                 ─────────────
 0        1        Header                0x85
 1-2      2        Packet length         0x0030 (48)
 3-6      4        Source ID             [MAC bytes 2-5]
 7-10     4        Destination ID        [Parent ID]
 11       1        Frame type            0x01
 12       1        Options               0x20 (no ACK) or 0x21 (ACK)
 13-16    4        Sequence number       [big-endian counter]
 17       1        Total packets         0x01
 18       1        Packet index          0x00
 19-30    12       GCM IV                [random]
 31-33    3        Ciphertext            [encrypted payload]
 34-49    16       GCM Tag               [auth tag]
 50       1        Checksum              [sum bytes 3-49 & 0xFF]
```

### Complete Telemetry Frame (Plaintext Fallback)

**Total size**: 23 bytes

```
 Offset   Length   Field                 Example Value
 ──────   ──────   ─────                 ─────────────
 0        1        Header                0x85
 1-2      2        Packet length         0x0014 (20)
 3-6      4        Source ID             [MAC bytes 2-5]
 7-10     4        Destination ID        [Parent ID]
 11       1        Frame type            0x01
 12       1        Options               0x20
 13-16    4        Sequence number       [big-endian counter]
 17       1        Total packets         0x01
 18       1        Packet index          0x00
 19       1        Temperature MSB       e.g. 0x09
 20       1        Temperature LSB       e.g. 0xE3
 21       1        Contact status        0x00 or 0x01
 22       1        Checksum              [sum bytes 3-21 & 0xFF]
```

---

## 5. Metrics Frame (0x02)

Sent every N telemetry cycles (configurable via `metricsReportInterval` setting), on first boot, and on user-button wake. Contains the full 207-byte FRAM metrics region, which includes device identity, cumulative timing accumulators, battery voltage, and energy tracking.

The metrics frame payload is a direct read of the FRAM Metrics region (see `FRAM_MEMORY_MAP.md`). All multi-byte fields are big-endian.

### Application Payload (207 bytes plaintext)

```
 Byte    Field                  Type       Description
 ────    ─────                  ────       ───────────────────────────────────────
 0       metricsVersion         uint8_t    Metrics layout version (0x01)
 1       firmwareVersion        uint8_t    Firmware version
 2       hardwareVersion        uint8_t    Hardware version
 3       sensorType             uint8_t    Sensor type identifier
 4-5     batteryVoltage         uint16_t   Filtered lowest-known voltage (centivolts)
 6-9     totalTxTime            uint32_t   Cumulative TX time (ms)
 10-13   totalRxTime            uint32_t   Cumulative RX time (ms)
 14-17   totalActiveTime        uint32_t   Cumulative awake-not-TX/RX time (ms)
 18-21   totalSleepTime         uint32_t   Cumulative sleep time (seconds)
 22-25   cycleCount             uint32_t   Total wake cycles since first boot
 26-29   txCount                uint32_t   Total successful transmissions
 30      ackFailCount           uint8_t    Consecutive ACK failures
 31-32   ackFailTotal           uint16_t   Lifetime ACK failures
 33-34   telemetrySinceMetrics  uint16_t   Telemetry cycles since last metrics report
 35-36   bootCount              uint16_t   Cold boot count (non-deep-sleep resets)
 37-40   totalEnergy            uint32_t   Cumulative energy (microwatt-hours)
 41-50   reserved               —          Reserved for future universal metrics
 51-206  sensorSpecificMetrics  —          Sensor-type-specific metrics
```

**Battery voltage encoding**: Voltage in volts × 100, stored as unsigned 16-bit big-endian. Filtered to track lowest-known voltage; jumps >0.30V indicate battery replacement.

| Example Voltage | Encoded (hex) |
|----------------|---------------|
| 3.72 V | `0x0174` (372) |
| 4.20 V | `0x01A4` (420) |
| 2.80 V | `0x0118` (280) |

**Energy encoding**: Cumulative energy in microwatt-hours as unsigned 32-bit big-endian.

### Encrypted Metrics Payload (235 bytes)

```
 Offset    Length   Field
 ──────    ──────   ─────────────────────────
 0-11      12       GCM IV
 12-218    207      Ciphertext (encrypted metrics)
 219-234   16       GCM Authentication Tag
```

### Complete Metrics Frame (Encrypted)

**Total size**: 255 bytes (maximum single LoRa packet)

```
 Offset    Length   Field                 Example Value
 ──────    ──────   ─────                 ─────────────
 0         1        Header                0x85
 1-2       2        Packet length         0x00FC (252)
 3-6       4        Source ID             [MAC bytes 2-5]
 7-10      4        Destination ID        0xFFFFFFFF (broadcast)
 11        1        Frame type            0x02
 12        1        Options               0x20 (no ACK)
 13-16     4        Sequence number       [big-endian counter]
 17        1        Total packets         0x01
 18        1        Packet index          0x00
 19-30     12       GCM IV                [random]
 31-237    207      Ciphertext            [encrypted metrics]
 238-253   16       GCM Tag               [auth tag]
 254       1        Checksum              [sum bytes 3-253 & 0xFF]
```

---

## 6. Settings Report Frame (0x04)

Sent on gateway command request. Contains the full 207-byte FRAM Settings region, which includes all configurable device parameters. This allows the gateway to read the complete device configuration.

The settings frame payload is a direct read of the FRAM Active Settings region (see `FRAM_MEMORY_MAP.md`). All multi-byte fields are big-endian.

### Application Payload (207 bytes plaintext)

```
 Byte    Field                    Type       Description
 ────    ─────                    ────       ───────────────────────────────────────
 0       settingsVersion          uint8_t    Settings map version (0x01)
 1-2     telemetryInterval        uint16_t   Telemetry report interval (seconds)
 3-4     telemetryMaxWake         uint16_t   Max awake time for telemetry cycle (ms)
 5       txPower                  uint8_t    TX power dBm (2-22)
 6       spreadingFactor          uint8_t    LoRa spreading factor (7-12)
 7       bandwidth                uint8_t    0=125kHz, 1=250kHz, 2=500kHz
 8-11    frequency                uint32_t   Radio center frequency in Hz
 12      codingRate               uint8_t    LoRa coding rate
 13-14   waitAfterTx              uint16_t   RX listen after metrics TX (ms)
 15      ackFailThreshold         uint8_t    Consecutive ACK failures to orphan
 16      telemetryAckRequired     uint8_t    Require ACK on telemetry (0=no, 1=yes)
 17-18   metricsReportInterval    uint16_t   Metrics every N telemetry cycles
 19-22   parentID                 uint32_t   Adopted gateway ID
 23-32   reserved                 —          Reserved for future universal settings
 33      sensorType               uint8_t    Sensor type identifier
 34      firmwareVersion          uint8_t    Firmware version
 35      hardwareVersion          uint8_t    Hardware version
 36-206  sensorSpecificSettings   —          Sensor-type-specific settings
```

### Complete Settings Frame (Encrypted)

**Total size**: 255 bytes (maximum single LoRa packet)

Frame structure is identical to the Metrics Frame but with frame type `0x04`.

---

## 7. Transmission Behavior

### Wake Sources and Frame Sequences

| Wake Source | GPIO | Mechanism | Frames Sent | Description |
|-------------|------|-----------|-------------|-------------|
| Timer | -- | `esp_sleep_enable_timer_wakeup` | Telemetry, then Metrics (if due) | Periodic interval reporting |
| User button | GPIO2 | ext0 (level, HIGH) | Telemetry, then Metrics | Manual trigger, full report |
| Contact sensor | GPIO14 | ext1 (dynamic polarity) | Telemetry, then Metrics (if due) | State-change reporting |
| Power-on | -- | `ESP_RST_POWERON` | Telemetry, then Metrics | Initial boot, full report |

### Metrics Reporting Interval

Metrics are sent based on a configurable multiplier of the telemetry interval:

- `metricsReportInterval` = N means metrics are sent every N telemetry transmissions
- Example: telemetryInterval=600s, metricsReportInterval=6 → metrics every hour
- Metrics are always sent on first boot and user-button wake regardless of the counter
- The `telemetrySinceMetrics` counter in the Metrics region tracks progress

### Transmission Order

```
 1. Sensor wakes
 2. FRAM scratchpad checked for brownout (lastTxStatus == 1)
 3. Battery voltage read and filtered
 4. TMP112 temperature reading requested
 5. Contact pin (GPIO14) state read
 6. Telemetry frame transmitted (temperature + contact)
 7. telemetrySinceMetrics counter incremented
 8. If ACK required: wait for acknowledgement
 9. If metrics due (counter >= interval) OR first boot OR button wake:
    a. Metrics frame transmitted (full 207-byte FRAM metrics region)
    b. telemetrySinceMetrics counter reset to 0
    c. Listen for commands (waitAfterTx duration)
10. Accumulate TX/RX/Active time to FRAM metrics
11. Flush all dirty FRAM regions
12. Radio enters deep sleep
13. MCU enters deep sleep
```

### Brownout Detection

Before each TX attempt, the firmware writes `lastTxStatus = 1` to the FRAM scratchpad. On TX success, it writes `2`; on detected failure, `3`. On wake, if `lastTxStatus == 1`, the previous TX never completed — likely a brownout. The device enters recovery mode with exponentially increasing sleep intervals.

### Contact Sensor Wake (ext1) - Edge Detection

The ext1 wake source uses dynamic polarity to achieve edge-like behavior:

- At sleep entry, GPIO14 is read via `rtc_gpio_get_level()`
- If currently **LOW** (closed): wake trigger set to `ESP_EXT1_WAKEUP_ANY_HIGH`
- If currently **HIGH** (open): wake trigger set to `ESP_EXT1_WAKEUP_ALL_LOW`
- Internal pull-up is always enabled on GPIO14

This ensures the device wakes **once per state change**, not continuously while the contact remains in a given state.

---

## 8. Decoding Examples

### Decoding Telemetry Payload (Plaintext)

Given raw payload bytes: `09 E3 01`

```
Temperature:
  raw = (0x09 << 8) | 0xE3 = 0x09E3 = 2531 (signed)
  tempC = 2531 / 100.0 = 25.31 C

Contact:
  0x01 = closed
```

### Decoding Telemetry Payload (Encrypted)

Given the full frame, extract bytes [19..49] as the encrypted payload:

```
 1. Extract IV:        bytes [19..30]  (12 bytes)
 2. Extract ciphertext: bytes [31..33]  (3 bytes)
 3. Extract tag:       bytes [34..49]  (16 bytes)
 4. Reconstruct AAD:   frameType(0x01) + sourceID(bytes [3..6]) + seqNum(bytes [13..16])
 5. Decrypt:           AES-128-GCM with session key, IV, AAD, ciphertext, tag
 6. Parse plaintext:   3 bytes as described above
```

### Decoding Metrics Payload (Plaintext)

Given the first 51 bytes of the 207-byte metrics payload:

```
Byte 0:     metricsVersion  = 0x01
Byte 1:     firmwareVersion = 0x01
Byte 2:     hardwareVersion = 0x01
Byte 3:     sensorType      = 0x01
Bytes 4-5:  batteryVoltage  = (0x01 << 8) | 0x74 = 372 → 3.72 V
Bytes 6-9:  totalTxTime     = 0x00001A2B = 6699 ms
Bytes 10-13: totalRxTime    = 0x0000C350 = 50000 ms
Bytes 14-17: totalActiveTime = 0x00005DC0 = 24000 ms
Bytes 18-21: totalSleepTime = 0x00015180 = 86400 seconds (1 day)
Bytes 22-25: cycleCount     = 0x00000090 = 144 cycles
Bytes 26-29: txCount        = 0x0000008E = 142 transmissions
Byte 30:    ackFailCount    = 0x00 (no consecutive failures)
Bytes 31-32: ackFailTotal   = 0x0002 = 2 lifetime failures
Bytes 33-34: telemetrySinceMetrics = 0x0003 = 3 since last report
Bytes 35-36: bootCount      = 0x0001 = 1 cold boot
Bytes 37-40: totalEnergy    = 0x000186A0 = 100000 µWh = 100 mWh
Bytes 41-50: reserved       = 0x00...
Bytes 51-206: sensorSpecificMetrics (sensor-type dependent)
```

### Checksum Verification

```
checksum = 0
for each byte from offset 3 to (total_frame_size - 2):
    checksum += byte
expected = checksum & 0xFF
actual = frame[total_frame_size - 1]
valid = (expected == actual)
```

---

## Frame Size Summary

| Frame | Plaintext Size | Encrypted Size |
|-------|---------------|----------------|
| Telemetry (0x01) | 23 bytes | 51 bytes |
| Metrics (0x02) | 227 bytes | 255 bytes |
| Settings Report (0x04) | 227 bytes | 255 bytes |

---

*Document generated from v1_resonant_device firmware implementation.*  
*Source files: `main.cpp`, `main.h`, `resonant_fram_storage.h`, `resonant_frame.cpp`, `resonant_encryption.h`, `Sensor.cpp`, `resonant_power_manager.cpp`*

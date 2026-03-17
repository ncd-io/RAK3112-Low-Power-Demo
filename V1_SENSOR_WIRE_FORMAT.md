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
6. [Transmission Behavior](#6-transmission-behavior)
7. [Decoding Examples](#7-decoding-examples)

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

Sent on first boot and user-button wake. Contains device health and identification data.

### Application Payload (9 bytes plaintext)

```
 Byte   Field              Type       Description
 ────   ─────              ────       ───────────────────────────────────────
 0      Firmware version   uint8_t    Currently 1
 1      Hardware version   uint8_t    Currently 1
 2      Sensor type        uint8_t    Currently 0x01
 3      Energy MSB         uint16_t   totalEnergy_mAh * 100, high byte
 4      Energy LSB         uint16_t   totalEnergy_mAh * 100, low byte
 5      Battery MSB        uint16_t   batteryVoltage_V * 100, high byte
 6      Battery LSB        uint16_t   batteryVoltage_V * 100, low byte
 7      Reserved           uint8_t    0x00
 8      Reserved           uint8_t    0x00
```

**Energy encoding**: Cumulative energy consumption in milliamp-hours, multiplied by 100, stored as unsigned 16-bit big-endian.

**Battery voltage encoding**: Battery voltage in volts, multiplied by 100, stored as unsigned 16-bit big-endian. Measured via ADC with resistor divider correction (x2.2794 scaling factor).

| Example Voltage | Encoded (hex) |
|----------------|---------------|
| 3.72 V | `0x0174` (372) |
| 4.20 V | `0x01A4` (420) |
| 2.80 V | `0x0118` (280) |

### Encrypted Metrics Payload (37 bytes)

```
 Offset   Length   Field
 ──────   ──────   ─────────────────────────
 0-11     12       GCM IV
 12-20    9        Ciphertext (encrypted metrics)
 21-36    16       GCM Authentication Tag
```

### Complete Metrics Frame (Encrypted)

**Total size**: 57 bytes

```
 Offset   Length   Field                 Example Value
 ──────   ──────   ─────                 ─────────────
 0        1        Header                0x85
 1-2      2        Packet length         0x0036 (54)
 3-6      4        Source ID             [MAC bytes 2-5]
 7-10     4        Destination ID        0xFFFFFFFF (broadcast)
 11       1        Frame type            0x02
 12       1        Options               0x20 (no ACK)
 13-16    4        Sequence number       [big-endian counter]
 17       1        Total packets         0x01
 18       1        Packet index          0x00
 19-30    12       GCM IV                [random]
 31-39    9        Ciphertext            [encrypted payload]
 40-55    16       GCM Tag               [auth tag]
 56       1        Checksum              [sum bytes 3-55 & 0xFF]
```

### Complete Metrics Frame (Plaintext Fallback)

**Total size**: 29 bytes

```
 Offset   Length   Field                 Example Value
 ──────   ──────   ─────                 ─────────────
 0        1        Header                0x85
 1-2      2        Packet length         0x001A (26)
 3-6      4        Source ID             [MAC bytes 2-5]
 7-10     4        Destination ID        0xFFFFFFFF (broadcast)
 11       1        Frame type            0x02
 12       1        Options               0x20
 13-16    4        Sequence number       [big-endian counter]
 17       1        Total packets         0x01
 18       1        Packet index          0x00
 19       1        Firmware version      0x01
 20       1        Hardware version      0x01
 21       1        Sensor type           0x01
 22       1        Energy MSB            e.g. 0x00
 23       1        Energy LSB            e.g. 0x64
 24       1        Battery MSB           e.g. 0x01
 25       1        Battery LSB           e.g. 0x74
 26       1        Reserved              0x00
 27       1        Reserved              0x00
 28       1        Checksum              [sum bytes 3-27 & 0xFF]
```

---

## 6. Transmission Behavior

### Wake Sources and Frame Sequences

| Wake Source | GPIO | Mechanism | Frames Sent | Description |
|-------------|------|-----------|-------------|-------------|
| Timer | -- | `esp_sleep_enable_timer_wakeup` | Telemetry | Periodic interval reporting |
| User button | GPIO2 | ext0 (level, HIGH) | Telemetry, then Metrics | Manual trigger, full report |
| Contact sensor | GPIO14 | ext1 (dynamic polarity) | Telemetry | State-change reporting |
| Power-on | -- | `ESP_RST_POWERON` | Telemetry, then Metrics | Initial boot, full report |

### Transmission Order

When both telemetry and metrics are sent in a single wake cycle:

```
 1. Sensor wakes
 2. TMP112 temperature reading requested
 3. Contact pin (GPIO14) state read
 4. Telemetry frame transmitted (temperature + contact)
 5. If ACK required: wait for acknowledgement
 6. Metrics frame transmitted (versions + energy + battery)
 7. Metrics time updated in NVS
 8. Radio enters deep sleep
 9. MCU enters deep sleep
```

### Contact Sensor Wake (ext1) - Edge Detection

The ext1 wake source uses dynamic polarity to achieve edge-like behavior:

- At sleep entry, GPIO14 is read via `rtc_gpio_get_level()`
- If currently **LOW** (closed): wake trigger set to `ESP_EXT1_WAKEUP_ANY_HIGH`
- If currently **HIGH** (open): wake trigger set to `ESP_EXT1_WAKEUP_ALL_LOW`
- Internal pull-up is always enabled on GPIO14

This ensures the device wakes **once per state change**, not continuously while the contact remains in a given state.

---

## 7. Decoding Examples

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

Given raw payload bytes: `01 01 01 00 64 01 74 00 00`

```
Firmware version:  0x01 = 1
Hardware version:  0x01 = 1
Sensor type:       0x01
Energy:            (0x00 << 8) | 0x64 = 100 -> 100 / 100.0 = 1.00 mAh
Battery voltage:   (0x01 << 8) | 0x74 = 372 -> 372 / 100.0 = 3.72 V
Reserved:          0x00, 0x00
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
| Metrics (0x02) | 29 bytes | 57 bytes |

---

*Document generated from v1_resonant_device firmware implementation.*  
*Source files: `main.cpp`, `main.h`, `resonant_frame.cpp`, `resonant_encryption.h`, `Sensor.cpp`, `resonant_power_manager.cpp`*

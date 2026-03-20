# FRAM Memory Map — MB85RS64V (8192 bytes)

**Chip**: Fujitsu MB85RS64V (64Kbit / 8192 bytes SPI FRAM)  
**Interface**: SPI (8 MHz), CS on GPIO 12  
**Endurance**: 10 trillion read/write cycles  
**Retention**: 10 years at 85°C

---

## Region Overview

| Region           | Start    | End      | Size       | Purpose                                                      |
| ---------------- | -------- | -------- | ---------- | ------------------------------------------------------------ |
| Active Settings  | `0x0000` | `0x00CE` | 207 bytes  | Configurable parameters — directly transmittable as settings report frame payload |
| Factory Settings | `0x00CF` | `0x019D` | 207 bytes  | Snapshot of defaults — copied to Active Settings on factory reset |
| Metrics          | `0x019E` | `0x026B` | 207 bytes  | Cumulative accumulators + identity — directly transmittable as metrics frame payload |
| Scratchpad       | `0x026C` | `0x03FB` | 400 bytes  | Firmware-private cycle state — never transmitted, expandable into free space |
| Reserved/Free    | `0x03FC` | `0x1FFF` | 7172 bytes | Future use (scratchpad growth, reading buffers, certificates, etc.) |

**Total defined**: 1021 bytes (12.5% of 8192)

---

## 1. Settings Region (207 bytes)

Starts at FRAM address `0x0000`. The entire 207-byte region can be read as a single block and used directly as the settings report frame payload (encrypted).

| Offset | Size | Name                   | Type     | Default      | Notes                                   |
| ------ | ---- | ---------------------- | -------- | ------------ | --------------------------------------- |
| 0      | 1    | settingsVersion        | uint8_t  | `0x01`       | Map version — bump on layout changes    |
| 1–2    | 2    | telemetryInterval      | uint16_t | `0x0005`     | Telemetry report interval (seconds)     |
| 3–4    | 2    | telemetryMaxWake       | uint16_t | `0x1388`     | Max awake time for telemetry cycle (ms) |
| 5      | 1    | txPower                | uint8_t  | `0x16`       | TX power dBm (2–22)                     |
| 6      | 1    | spreadingFactor        | uint8_t  | `0x07`       | LoRa spreading factor (7–12)            |
| 7      | 1    | bandwidth              | uint8_t  | `0x00`       | 0=125kHz, 1=250kHz, 2=500kHz           |
| 8–11   | 4    | frequency              | uint32_t | `0x36A48C00` | Radio center frequency in Hz (915 MHz)  |
| 12     | 1    | codingRate             | uint8_t  | `0x01`       | LoRa coding rate                        |
| 13–14  | 2    | waitAfterTx            | uint16_t | `0x1F40`     | RX listen after metrics TX (ms)         |
| 15     | 1    | ackFailThreshold       | uint8_t  | `0x05`       | Consecutive ACK failures to orphan      |
| 16     | 1    | telemetryAckRequired   | uint8_t  | `0x00`       | Require ACK on telemetry (0=no, 1=yes)  |
| 17–18  | 2    | metricsReportInterval  | uint16_t | `0x0006`     | Metrics every N telemetry cycles        |
| 19–22  | 4    | parentID               | uint32_t | `0xFFFFFFFF` | Adopted gateway ID (broadcast = unadopted) |
| 23–32  | 10   | reserved               | —        | `0x00`       | Reserved for future universal settings  |
| 33     | 1    | sensorType             | uint8_t  | `0x01`       | Sensor type identifier                  |
| 34     | 1    | firmwareVersion        | uint8_t  | `0x01`       | Firmware version                        |
| 35     | 1    | hardwareVersion        | uint8_t  | `0x01`       | Hardware version                        |
| 36–206 | 171  | sensorSpecificSettings | —        | `0x00`       | Sensor-type-specific settings           |

**Universal fields**: bytes 0–35 (36 bytes)  
**Sensor-specific**: bytes 36–206 (171 bytes)

---

## 2. Factory Settings Region (207 bytes)

Starts at FRAM address `0x00CF`. Identical layout to Active Settings. Written once at first boot (or firmware update). On factory reset, this region is copied byte-for-byte to the Active Settings region.

---

## 3. Metrics Region (207 bytes)

Starts at FRAM address `0x019E`. The entire 207-byte region can be read as a single block and used directly as the metrics report frame payload (encrypted). Updated every wake cycle with cumulative accumulators.

| Offset | Size | Name                  | Type     | Default      | Notes                                          |
| ------ | ---- | --------------------- | -------- | ------------ | ---------------------------------------------- |
| 0      | 1    | metricsVersion        | uint8_t  | `0x01`       | Metrics layout version                         |
| 1      | 1    | firmwareVersion       | uint8_t  | `0x01`       | Identity (duplicated for self-contained frame) |
| 2      | 1    | hardwareVersion       | uint8_t  | `0x01`       | Identity                                       |
| 3      | 1    | sensorType            | uint8_t  | `0x01`       | Identity                                       |
| 4–5    | 2    | batteryVoltage        | uint16_t | `0x0000`     | Filtered lowest-known voltage (centivolts)     |
| 6–9    | 4    | totalTxTime           | uint32_t | `0x00000000` | Cumulative TX time (ms)                        |
| 10–13  | 4    | totalRxTime           | uint32_t | `0x00000000` | Cumulative RX time (ms)                        |
| 14–17  | 4    | totalActiveTime       | uint32_t | `0x00000000` | Cumulative awake-not-TX/RX time (ms)           |
| 18–21  | 4    | totalSleepTime        | uint32_t | `0x00000000` | Cumulative sleep time (seconds)                |
| 22–25  | 4    | cycleCount            | uint32_t | `0x00000000` | Total wake cycles since first boot             |
| 26–29  | 4    | txCount               | uint32_t | `0x00000000` | Total successful transmissions                 |
| 30     | 1    | ackFailCount          | uint8_t  | `0x00`       | Consecutive ACK failures (runtime)             |
| 31–32  | 2    | ackFailTotal          | uint16_t | `0x0000`     | Lifetime ACK failures                          |
| 33–34  | 2    | telemetrySinceMetrics | uint16_t | `0x0000`     | Counter for metrics interval gating            |
| 35–36  | 2    | bootCount             | uint16_t | `0x0000`     | Cold boot count (non-deep-sleep resets)        |
| 37–40  | 4    | totalEnergy           | uint32_t | `0x00000000` | Cumulative energy (microwatt-hours)            |
| 41–50  | 10   | reserved              | —        | `0x00`       | Reserved for future universal metrics          |
| 51–206 | 156  | sensorSpecificMetrics | —        | `0x00`       | Sensor-type-specific metrics                   |

**Universal fields**: bytes 0–50 (51 bytes)  
**Sensor-specific**: bytes 51–206 (156 bytes)

---

## 4. Scratchpad Region (400 bytes)

Starts at FRAM address `0x026C`. Firmware-private working memory that persists across deep sleep. Never transmitted to the gateway. Positioned last among defined regions so it can grow into the 7172 bytes of free space.

| Offset  | Size | Name                  | Type     | Default      | Notes                                                         |
| ------- | ---- | --------------------- | -------- | ------------ | ------------------------------------------------------------- |
| 0       | 1    | lastTxStatus          | uint8_t  | `0x00`       | 0=idle, 1=TX attempted, 2=TX success, 3=TX failed (detected) |
| 1–2     | 2    | preTxBatteryVoltage   | uint16_t | `0x0000`     | Voltage reading before TX attempt (centivolts)                |
| 3       | 1    | brownoutRecoveryCount | uint8_t  | `0x00`       | Consecutive brownout recovery cycles                          |
| 4       | 1    | lastWakeReason        | uint8_t  | `0x00`       | 0=timer, 1=button, 2=contact, 3=power-on                     |
| 5       | 1    | cycleFlags            | uint8_t  | `0x00`       | Bitfield: b0=metricsSent, b1=cmdReceived, b2=ackReceived      |
| 6–7     | 2    | rawBatteryVoltage     | uint16_t | `0x0000`     | Most recent ADC reading (centivolts)                          |
| 8–11    | 4    | txSequenceNumber      | uint32_t | `0x00000001` | Monotonic TX sequence counter                                 |
| 12–31   | 20   | reserved              | —        | `0x00`       | Reserved for future scratchpad fields                         |
| 32–399  | 368  | sensorReadingBuffer   | —        | `0x00`       | Available for buffered sensor readings and future growth      |

**Universal fields**: bytes 0–31 (32 bytes)  
**Sensor buffer**: bytes 32–399 (368 bytes)

---

## Design Principles

### Zero-Copy Frame Building
The Settings and Metrics regions are sized at exactly 207 bytes — the maximum application payload in a single encrypted Resonant wireless frame (255 byte LoRa packet − 20 byte frame overhead − 28 byte GCM overhead = 207 bytes). This allows the firmware to read the region into a buffer and use it directly as the frame payload without assembly.

### Factory Reset
Factory reset copies the 207-byte Factory Settings region to the Active Settings region, resets the Metrics region to defaults, and clears the Scratchpad.

### First Boot Initialization
On first boot (detected by `settingsVersion == 0xFF` indicating erased FRAM), the firmware writes default values to all regions and copies Active Settings to Factory Settings.

### Sensor-Specific Regions
Each sensor type defines its own sub-layout within the tail of the Settings (bytes 36–206), Metrics (bytes 51–206), and Scratchpad (bytes 32–399) regions. The shared library provides raw byte access to these regions; the sensor-specific `Sensor` class in each firmware project manages the sub-layout.

### Byte Order
All multi-byte fields are stored **big-endian** to match the Resonant wire protocol and allow direct transmission without byte-swapping.

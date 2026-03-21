# SX1262 LoRa Radio Driver

Lightweight, self-contained SX1262 radio driver for Arduino-compatible platforms.  
Written from scratch — no external radio libraries required.

Supports **LoRa** and **GFSK** modulation with full runtime configuration.

---

## Pin Configuration

Default pin mapping targets the **DX-LR20** module on an STM32F103C8 board.  
To use a different board, edit the defines at the top of `sx1262_driver.h`:

```cpp
#define SX1262_NSS_PIN    PA4   // SPI chip select (directly drives SX1262 NSS)
#define SX1262_RESET_PIN  PA3   // Hardware reset    (active LOW pulse)
#define SX1262_BUSY_PIN   PA2   // Busy indicator    (HIGH = chip busy)
#define SX1262_TXEN_PIN   PA0   // TX RF switch enable
#define SX1262_RXEN_PIN   PA1   // RX RF switch enable
```

The driver uses the default Arduino `SPI` bus (`SPI.begin()`).  
On STM32F103 this maps to **PA5** (SCK), **PA6** (MISO), **PA7** (MOSI).

---

## Quick Start

### Minimal Transmit Example

```cpp
#include "sx1262_driver.h"

SX1262Radio radio;

void setup() {
    Serial.begin(115200);
    if (!radio.begin(433000000)) {   // 433 MHz
        Serial.println("Radio init failed");
        while (1);
    }
    Serial.println("Radio ready");
}

void loop() {
    uint8_t msg[] = "Hello LoRa";
    if (radio.send(msg, sizeof(msg) - 1)) {
        Serial.println("Sent OK");
    } else {
        Serial.println("Send failed");
    }
    delay(5000);
}
```

### Minimal Receive Example

```cpp
#include "sx1262_driver.h"

SX1262Radio radio;

void setup() {
    Serial.begin(115200);
    if (!radio.begin(433000000)) {
        Serial.println("Radio init failed");
        while (1);
    }
    radio.startReceive();
}

void loop() {
    PacketInfo pkt;
    if (radio.checkForPacket(pkt)) {
        if (pkt.crcError) {
            Serial.println("CRC error");
        } else {
            Serial.write(pkt.data, pkt.length);
            Serial.print("  RSSI: ");
            Serial.print(pkt.rssi);
            Serial.print(" dBm  SNR: ");
            Serial.println(pkt.snr);
        }
        // No need to call startReceive() again — RX stays active
    }
}
```

---

## API Reference

### Initialization

#### `bool begin(uint32_t freqHz = 433000000)`

Initializes the radio hardware and applies default configuration.

| Parameter | Description |
|-----------|-------------|
| `freqHz`  | Operating frequency in Hz (e.g. `433000000`, `868000000`, `915000000`) |

**Returns:** `true` if the chip responded and initialized successfully, `false` on failure.

**Behavior:**
- Configures GPIO pins (NSS, RESET, BUSY, TXEN, RXEN)
- Initializes SPI at 2 MHz
- Performs hardware reset, calibration, and image rejection calibration
- Sets PA config for +22 dBm output
- Applies default configuration (see [Default Configuration](#default-configuration))
- Leaves radio in **IDLE** state

Call this once in `setup()`. The radio is ready to transmit or receive after `begin()` returns `true`.

---

### Transmit & Receive

#### `bool send(const uint8_t* data, uint8_t len, uint32_t timeoutMs = 5000)`

Transmits a packet and blocks until completion or timeout.

| Parameter   | Description |
|-------------|-------------|
| `data`      | Pointer to the payload bytes |
| `len`       | Payload length (1–255) |
| `timeoutMs` | Maximum time to wait for TX_DONE (default: 5000 ms) |

**Returns:** `true` if the packet was transmitted, `false` on timeout or invalid length.

**Behavior:**
- Switches to standby, enables TX RF switch, writes payload to FIFO
- Blocks in a polling loop until `IRQ_TX_DONE` or timeout
- Returns to **IDLE** state after completion
- If the radio was in RX mode, RX is stopped before transmitting

---

#### `bool startReceive()`

Puts the radio into continuous receive mode (no timeout).

**Returns:** `true` always.

**Behavior:**
- Enables RX RF switch
- Starts RX with infinite timeout (`0xFFFFFF`)
- The radio remains in RX until you call `goStandby()` or `send()`
- Poll `checkForPacket()` regularly to retrieve received packets

---

#### `bool checkForPacket(PacketInfo& pkt)`

Checks if a packet has been received and retrieves it.

| Parameter | Description |
|-----------|-------------|
| `pkt`     | Output structure filled with packet data, RSSI, SNR, and status |

**Returns:** `true` if a packet was available, `false` if nothing received yet.

**Behavior:**
- Non-blocking — returns immediately if no `IRQ_RX_DONE`
- Fills `pkt.data[]`, `pkt.length`, `pkt.rssi`, `pkt.snr`, `pkt.signalRssi`
- Sets `pkt.crcError` if the packet failed CRC (data is still available)
- Sets `pkt.timestamp` to the `millis()` value at reception
- Clears IRQ flags and increments internal packet/error counters
- RX mode remains active — no need to call `startReceive()` again

---

#### `void goStandby()`

Stops TX or RX and puts the radio in standby mode.

**Behavior:**
- Disables both TX and RX RF switches
- Enters XOSC or RC standby depending on `setStandbyXosc()` setting
- Sets state to `RADIO_IDLE`

---

#### `int16_t readRssi()`

Reads the instantaneous RSSI (signal strength) from the radio.

**Returns:** RSSI in dBm (negative value, e.g. `-90`).

Can be called while in RX mode to monitor the current noise floor.

---

### Configuration

All setters apply the new value immediately by reconfiguring the radio.  
If the radio was in RX mode before the call, RX is automatically restarted.  
Safe to call at any time.

#### LoRa Parameters

| Method | Parameters | Description |
|--------|-----------|-------------|
| `setFrequency(uint32_t hz)` | Frequency in Hz | Sets RF frequency. Triggers image recalibration. |
| `setSpreadingFactor(uint8_t sf)` | 5–12 | Sets LoRa spreading factor. Higher = longer range, slower data rate. |
| `setBandwidth(uint8_t bwCode)` | Bandwidth code constant | Sets channel bandwidth. Use `BW_125K`, `BW_250K`, etc. (see [Bandwidth Codes](#bandwidth-codes)). |
| `setCodingRate(uint8_t cr)` | 5–8 | Sets coding rate as 4/`cr`. Pass `5` for CR 4/5, `6` for CR 4/6, etc. |
| `setTxPower(int8_t dbm)` | -9 to +22 | Sets transmit power in dBm. Clamped to valid range. |
| `setPreambleLength(uint16_t len)` | Symbol count | Sets preamble length. Typical values: 8–65535. |
| `setCrc(bool on)` | `true`/`false` | Enables or disables CRC on transmitted packets. |
| `setIqInverted(bool inv)` | `true`/`false` | Enables IQ inversion (used by some protocols for downlink). |
| `setSyncWord(uint8_t sw)` | Sync word byte | Sets LoRa sync word. `0x12` = private, `0x34` = public (LoRaWAN). |
| `setHeaderImplicit(bool on, uint8_t payloadLen)` | Enable, fixed length | Switches between explicit (default) and implicit header mode. In implicit mode, `payloadLen` must match on TX and RX. |
| `setLdroMode(LdroMode mode)` | `LDRO_AUTO`, `LDRO_ON`, `LDRO_OFF` | Controls Low Data Rate Optimization. `LDRO_AUTO` enables it when symbol time exceeds 16 ms. |
| `setSymbolTimeout(uint8_t symbols)` | 0–255 | Sets RX symbol timeout. `0` = no timeout. |
| `setRxBoosted(bool on)` | `true`/`false` | Enables boosted RX gain for improved sensitivity (~2 dB improvement). |

#### Hardware / System

| Method | Parameters | Description |
|--------|-----------|-------------|
| `setModem(ModemType modem)` | `MODEM_LORA` or `MODEM_GFSK` | Switches between LoRa and GFSK modem modes. |
| `setStandbyXosc(bool on)` | `true`/`false` | `true` = standby uses crystal oscillator (faster wake), `false` = RC oscillator (lower power). Does **not** trigger reconfigure. |
| `setRegulatorDcdc(bool on)` | `true`/`false` | `true` = DC-DC regulator (recommended), `false` = LDO. |

#### GFSK Parameters

| Method | Parameters | Description |
|--------|-----------|-------------|
| `setGfskBitrate(uint32_t bps)` | 600–300000 bps | Sets GFSK data rate. Clamped to valid range. |
| `setGfskFdev(uint32_t hz)` | 100–200000 Hz | Sets GFSK frequency deviation. |
| `setGfskBw(uint8_t bwCode)` | Bandwidth register code | Sets GFSK receiver bandwidth. |
| `setGfskWhitening(bool on)` | `true`/`false` | Enables or disables data whitening. |

---

### Diagnostics

| Method | Returns | Description |
|--------|---------|-------------|
| `readIrqStatus()` | `uint16_t` | Returns current IRQ flags (see [IRQ Masks](#irq-masks)). |
| `clearIrqStatus()` | — | Clears all pending IRQ flags. |
| `readDeviceErrors()` | `uint16_t` | Returns the device error register. |
| `clearDeviceErrors()` | — | Clears all device errors. |
| `readRegister8(uint16_t addr)` | `uint8_t` | Reads a single byte from any SX1262 register. |
| `getModem()` | `ModemType` | Returns current modem type (`MODEM_LORA` or `MODEM_GFSK`). |

---

### Getters

| Method | Returns | Description |
|--------|---------|-------------|
| `getConfig()` | `const LoRaConfig&` | Returns a reference to the current configuration struct. |
| `getState()` | `RadioState` | Returns `RADIO_IDLE`, `RADIO_TX`, or `RADIO_RX`. |
| `getPacketCount()` | `uint32_t` | Total packets received since last `resetStats()`. |
| `getCrcErrorCount()` | `uint32_t` | Total CRC errors since last `resetStats()`. |
| `resetStats()` | — | Resets packet and CRC error counters to zero. |

---

### Bandwidth Utilities (Static)

These can be called without a radio instance: `SX1262Radio::bwFromKHz(125.0f)`.

| Method | Parameters | Returns | Description |
|--------|-----------|---------|-------------|
| `bwFromKHz(float kHz)` | Bandwidth in kHz | `uint8_t` | Returns the closest matching bandwidth code constant. |
| `bwToStr(uint8_t bw)` | Bandwidth code | `const char*` | Returns a human-readable string (e.g. `"125 kHz"`). |
| `bwToKHz(uint8_t bw)` | Bandwidth code | `float` | Converts a bandwidth code to its value in kHz. |

---

## Data Structures

### `PacketInfo`

Returned by `checkForPacket()` when a packet is received.

```cpp
struct PacketInfo {
    uint8_t  data[255];     // Received payload
    uint8_t  length;        // Payload length in bytes
    int16_t  rssi;          // Packet RSSI in dBm
    int16_t  signalRssi;    // Signal RSSI in dBm (LoRa only)
    int8_t   snr;           // Signal-to-noise ratio in dB (LoRa only)
    bool     crcError;      // true if CRC check failed
    uint32_t timestamp;     // millis() at time of reception
};
```

### `LoRaConfig`

Holds the complete radio configuration. Accessible via `radio.getConfig()`.

```cpp
struct LoRaConfig {
    // Modem selection
    ModemType modem;            // MODEM_LORA or MODEM_GFSK

    // LoRa parameters
    uint32_t frequencyHz;       // RF frequency in Hz
    uint8_t  spreadingFactor;   // 5–12
    uint8_t  bandwidth;         // Bandwidth code (BW_125K, etc.)
    uint8_t  codingRate;        // 1–4 (internal encoding: CR 4/5 = 1, CR 4/8 = 4)
    int8_t   txPowerDbm;        // -9 to +22 dBm
    uint16_t preambleLength;    // Preamble symbols
    bool     crcOn;             // CRC enabled
    bool     iqInverted;        // IQ inversion
    uint8_t  syncWord;          // LoRa sync word (0x12 = private, 0x34 = public)
    bool     implicitHeader;    // Implicit header mode
    uint8_t  implicitPayloadLen;// Fixed payload length for implicit mode
    LdroMode ldroMode;          // LDRO_AUTO, LDRO_ON, LDRO_OFF
    uint8_t  symbolTimeout;     // RX symbol timeout
    bool     rxBoosted;         // Boosted RX gain
    bool     standbyXosc;       // Standby clock source
    bool     regulatorDcdc;     // DC-DC regulator

    // GFSK parameters
    uint32_t gfskBitrate;       // Bitrate in bps
    uint32_t gfskFdev;          // Frequency deviation in Hz
    uint8_t  gfskPulseShape;    // Pulse shaping filter
    uint8_t  gfskBw;            // Receiver bandwidth code
    uint16_t gfskPreambleBits;  // Preamble length in bits
    uint8_t  gfskSyncLen;       // Sync word length in bytes
    bool     gfskWhiteningOn;   // Data whitening
};
```

### Enums

```cpp
enum RadioState : uint8_t {
    RADIO_IDLE,     // Standby — not transmitting or receiving
    RADIO_TX,       // Transmitting a packet
    RADIO_RX        // Listening for packets
};

enum LdroMode : uint8_t {
    LDRO_AUTO = 0,  // Enable LDRO automatically when symbol time > 16 ms
    LDRO_ON,        // Always enable LDRO
    LDRO_OFF        // Always disable LDRO
};

enum ModemType : uint8_t {
    MODEM_GFSK = 0, // FSK modulation
    MODEM_LORA = 1  // LoRa modulation
};
```

---

## Bandwidth Codes

Use these constants with `setBandwidth()`:

| Constant   | Bandwidth  | Typical Use |
|------------|------------|-------------|
| `BW_7K8`   | 7.81 kHz   | Maximum range, very slow |
| `BW_10K4`  | 10.42 kHz  | |
| `BW_15K6`  | 15.63 kHz  | |
| `BW_20K8`  | 20.83 kHz  | |
| `BW_31K25` | 31.25 kHz  | |
| `BW_41K7`  | 41.67 kHz  | |
| `BW_62K5`  | 62.50 kHz  | |
| `BW_125K`  | 125 kHz    | **Default** — good balance of range and speed |
| `BW_250K`  | 250 kHz    | Higher data rate |
| `BW_500K`  | 500 kHz    | Maximum data rate, shortest range |

---

## IRQ Masks

Available as defines for use with `readIrqStatus()`:

| Constant                | Value    | Description |
|-------------------------|----------|-------------|
| `IRQ_TX_DONE`           | `0x0001` | Transmission complete |
| `IRQ_RX_DONE`           | `0x0002` | Packet received |
| `IRQ_PREAMBLE_DETECTED` | `0x0004` | Preamble detected |
| `IRQ_HEADER_VALID`      | `0x0010` | Valid LoRa header received |
| `IRQ_HEADER_ERROR`      | `0x0020` | LoRa header CRC error |
| `IRQ_CRC_ERROR`         | `0x0040` | Payload CRC error |
| `IRQ_TIMEOUT`           | `0x0200` | RX or TX timeout |
| `IRQ_ALL`               | `0xFFFF` | All IRQ flags |

---

## Default Configuration

After calling `begin()`, the radio is configured with:

| Parameter         | Default Value |
|-------------------|---------------|
| Modem             | LoRa          |
| Spreading Factor  | SF9           |
| Bandwidth         | 125 kHz       |
| Coding Rate       | 4/6           |
| TX Power          | +22 dBm       |
| Preamble Length   | 8 symbols     |
| CRC               | Off           |
| IQ Inversion      | Off           |
| Sync Word         | 0x12 (private)|
| Header Mode       | Explicit      |
| LDRO              | Auto          |
| Symbol Timeout    | 0 (none)      |
| RX Gain           | Boosted       |
| Standby Clock     | XOSC          |
| Regulator         | DC-DC         |

---

## Usage Examples

### Change LoRa Parameters at Runtime

```cpp
radio.setSpreadingFactor(12);      // SF12 — maximum range
radio.setBandwidth(BW_250K);       // 250 kHz bandwidth
radio.setCodingRate(8);            // CR 4/8 — maximum error correction
radio.setTxPower(14);             // +14 dBm
radio.setCrc(true);               // Enable CRC
radio.setSyncWord(0x34);          // Public sync word (LoRaWAN compatible)
radio.setPreambleLength(12);      // 12 symbols
```

Each setter takes effect immediately. If the radio was receiving, RX is restarted automatically with the new settings.

### Read Current Configuration

```cpp
const LoRaConfig& cfg = radio.getConfig();
Serial.print("Frequency: ");
Serial.println(cfg.frequencyHz);
Serial.print("SF: ");
Serial.println(cfg.spreadingFactor);
Serial.print("BW: ");
Serial.println(SX1262Radio::bwToStr(cfg.bandwidth));
Serial.print("TX Power: ");
Serial.print(cfg.txPowerDbm);
Serial.println(" dBm");
```

### Frequency Hopping

```cpp
uint32_t channels[] = { 433050000, 433300000, 433550000 };
uint8_t ch = 0;

void transmitNext() {
    radio.setFrequency(channels[ch]);
    uint8_t msg[] = "ping";
    radio.send(msg, 4);
    ch = (ch + 1) % 3;
}
```

### Implicit Header Mode

Both TX and RX must agree on the exact payload length:

```cpp
// Transmitter
radio.setHeaderImplicit(true, 16);   // Fixed 16-byte packets
uint8_t data[16] = { /* ... */ };
radio.send(data, 16);

// Receiver
radio.setHeaderImplicit(true, 16);   // Must match transmitter
radio.startReceive();
```

### GFSK Mode

```cpp
radio.setModem(MODEM_GFSK);
radio.setGfskBitrate(50000);        // 50 kbps
radio.setGfskFdev(25000);           // 25 kHz deviation
radio.setGfskWhitening(true);

uint8_t data[] = "FSK packet";
radio.send(data, sizeof(data) - 1);
```

### Monitoring Signal Quality

```cpp
// Read instantaneous RSSI while in RX
radio.startReceive();
int16_t noise = radio.readRssi();
Serial.print("Noise floor: ");
Serial.print(noise);
Serial.println(" dBm");

// After receiving a packet
PacketInfo pkt;
if (radio.checkForPacket(pkt)) {
    Serial.print("Packet RSSI: ");
    Serial.print(pkt.rssi);
    Serial.print(" dBm  SNR: ");
    Serial.print(pkt.snr);
    Serial.println(" dB");
}
```

### Error Checking

```cpp
uint16_t errors = radio.readDeviceErrors();
if (errors != 0) {
    Serial.print("Device errors: 0x");
    Serial.println(errors, HEX);
    radio.clearDeviceErrors();
}
```

---

## Notes

- The driver uses `Serial1` for initialization debug output. These messages can be removed from `begin()` if not needed.
- `send()` is blocking — it waits for TX completion. For non-blocking TX, use the SX1262 in interrupt-driven mode (not implemented in this driver).
- The SPI bus runs at 2 MHz, which is well within the SX1262's SPI speed limit.
- The PA configuration in `begin()` is set for the SX1262's high-power PA (+22 dBm max). If using an SX1261, the PA config must be changed.
- When switching between LoRa and GFSK modes, all relevant parameters from `LoRaConfig` are applied — the driver handles both modem types through the same configuration struct.

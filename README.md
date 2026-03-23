# STM32 LoRa Terminal

A feature-rich serial CLI firmware for **STM32F103C8 "Blue Pill"** + **SX1262 LoRa radio** (DX-LR20 module), turning cheap hardware into a versatile LoRa tool with packet sniffing, spectrum analysis, encrypted chat, Meshtastic monitoring with AES decryption, and full on-the-fly radio configuration — all from a serial terminal or a **Flipper Zero**.

![Platform](https://img.shields.io/badge/platform-STM32F103C8-blue)
![Radio](https://img.shields.io/badge/radio-SX1262-green)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-orange)
![Flipper](https://img.shields.io/badge/Flipper%20Zero-Compatible-orange)

---

## Example Hardware

This board from Aliexpress:
DX-LR20 Semtech LLCC68+STM32F103C8T6 8KM Communication LoRa Module 433Mhz 470 868/915Mhz Wireless Rf Module Development Boards

<img src="https://github.com/NeutralSystem/STM32_DX-LR20_LORA/blob/main/STM32DXLR20.png" alt="Diagram" width="50" />

---

## Features

### Radio & Messaging
- **LoRa TX/RX** — send text or raw hex payloads, receive with full metadata (RSSI, SNR, signal RSSI)
- **GFSK mode** — switch between LoRa and FSK modulation on the fly
- **Beacon mode** — periodic automatic transmissions at configurable intervals
- **Packet sniffer** — continuous receive with hex dump, ASCII sidebar, and live statistics

### Spectrum Tools
- **Band scanner** — RSSI sweep across frequency ranges (presets for 433/868/915 MHz or custom)
- **Live spectrum analyzer** — real-time in-place RSSI table with peak hold and configurable threshold

### Meshtastic Monitoring
- **Passive Meshtastic listener** — apply LongFast/LongSlow/ShortFast presets and sniff Meshtastic traffic
- **AES-128-CTR decryption** — decrypt Meshtastic payloads with default LongFast key or custom key
- **Protocol decoder** — binary OTA header parsing (from/to/id/channel/hop-limit) and protobuf payload extraction
- **Region support** — US, EU868, EU433, CN, JP, ANZ, KR, TW, IN, RU frequency presets

### Encrypted Chat
- **XTEA-CTR encrypted chatrooms** — join numbered rooms (0–255) with shared keys
- **SipHash-2-4 MAC** — message authentication prevents tampering
- **Auto key generation** — firmware generates and displays keys for easy sharing
- **Flexible key input** — accepts 8–32 hex character keys, automatically expanded
- **Replay protection** — per-peer sequence tracking prevents replay attacks
- **Nicknames** — set a display name (up to 12 characters)

### Full Runtime Configuration
Every radio parameter is adjustable on the fly — no recompilation needed:

| Parameter | Range | Example |
|-----------|-------|---------|
| Frequency | 150–960 MHz | `freq 433.000` |
| TX Power | -9 to +22 dBm | `power 22` |
| Spreading Factor | 5–12 | `sf 9` |
| Bandwidth | 7.8–500 kHz | `bw 125` |
| Coding Rate | 4/5–4/8 | `cr 6` |
| Preamble Length | 1–65535 | `preamble 8` |
| CRC | on/off | `crc on` |
| IQ Inversion | normal/invert | `iq normal` |
| Sync Word | 0x00–0xFF | `syncword 12` |
| Header Mode | explicit/implicit | `header explicit` |
| LDRO | auto/on/off | `ldro auto` |
| RX Boost | on/off | `rxboost on` |
| Symbol Timeout | 0–255 | `symtimeout 0` |
| Standby Mode | xosc/rc | `standby xosc` |
| Regulator | dcdc/ldo | `regulator dcdc` |
| GFSK Bitrate | 600–300000 bps | `bitrate 50000` |
| GFSK Freq Dev | 100–200000 Hz | `fdev 25000` |
| GFSK BW | hex code | `fskbw 13` |
| GFSK Whitening | on/off | `whitening on` |

### System
- **Uptime** — view MCU uptime
- **Version** — firmware build info and board details
- **Sleep** — put radio into low-power standby
- **Reboot** — software reset of the MCU

### Flipper Zero App
- **Full companion app** — control the LoRa board entirely from a Flipper Zero
- **Pre-built .fap** — just copy to your SD card and go
- See the [Flipper_Zero_Lora_Terminal](Flipper_Zero_Lora_Terminal/) folder for details

---

## Hardware

| Component | Details |
|-----------|---------|
| **MCU** | STM32F103C8T6 "Blue Pill" (64 KB Flash, 20 KB RAM) |
| **Radio Module** | DX-LR20 (SX1262 / LLCC68) |
| **Dev Board** | DX-LR20-900M22SP |
| **Interface** | UART1 @ 115200 baud (PA9=TX, PA10=RX) |

### Pin Mapping

```
SPI1 (Radio)          Control             UART1 (Serial CLI)
─────────────         ───────             ──────────────────
PA4  → NSS            PA3  → RESET        PA9  → TX
PA5  → SCK            PA2  → BUSY         PA10 → RX
PA6  → MISO           PA0  → TXEN
PA7  → MOSI           PA1  → RXEN
                      PC13 → LED
```

---

## Getting Started

### Prerequisites
- [PlatformIO](https://platformio.org/) (VS Code extension or CLI)
- USB-to-Serial adapter (connect to PA9/PA10)
- STM32F103C8 board with DX-LR20 SX1262 module

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/NeutralSystem/STM32_DX-LR20_LORA.git
cd STM32_DX-LR20_LORA

# Build with PlatformIO
pio run

# Upload via serial
pio run --target upload
```

Or use the included `build_hex.bat` to generate an Intel HEX file for flashing with MCU ISP tools.

### Connect

1. Wire a USB-Serial adapter to `PA9` (TX) and `PA10` (RX) at **3.3V** logic.
2. Open a serial terminal at **115200 baud**.
3. Reset the board — you'll see a `>` prompt.
4. Type `help` for the full command list.

---

## Quick Examples

```text
> help                              # Show all commands
> send Hello World                  # Transmit a LoRa message
> sendhex 48656C6C6F                # Send raw hex bytes
> sniff                             # Continuous receive mode (Ctrl+C to stop)
> freq 868.100                      # Change frequency to 868.1 MHz
> sf 12                             # Set spreading factor to 12
> power 22                          # Set TX power to max (+22 dBm)
> scan 433                          # Scan the 433 MHz band
> analyze 868                       # Live spectrum analyzer on 868 MHz
> beacon 5000 PING                  # Transmit "PING" every 5 seconds
> meshlisten longfast us            # Listen for Meshtastic on US freq
> meshlisten longslow eu868         # Listen on EU868 frequency
> meshkey default                   # Use default Meshtastic AES key
> chatjoin 7                        # Join encrypted chat room 7
> chatjoin 7 AABBCCDD              # Join with specific key
> chatnick Alice                    # Set your nickname
> Hello everyone!                   # Send encrypted message (in chat mode)
> chatstatus                        # Show chat room info
> status                            # Show full radio configuration
> version                           # Show firmware and hardware info
> uptime                            # Show MCU uptime
```

---

## Command Reference

Type `help` in the terminal for a compact command list, or `<command> -help` for usage details on any command.

### Core Commands

| Command | Description |
|---------|-------------|
| `send <message>` | Send a text LoRa message |
| `sendhex <hex>` | Send raw hex bytes |
| `sniff` | Continuous receive with packet dump |
| `beacon <ms> <msg>` | Periodic transmission |
| `stop` | Stop sniff/beacon/analyze/chat |
| `status` | Show full radio configuration |
| `rssi` | Read instantaneous RSSI |
| `reset` | Re-initialize radio |

### Radio Configuration

| Command | Description |
|---------|-------------|
| `freq <MHz>` | Set RF frequency (150–960) |
| `power <dBm>` | Set TX power (-9 to +22) |
| `sf <5..12>` | Set spreading factor |
| `bw <kHz>` | Set bandwidth |
| `cr <5..8>` | Set coding rate (4/5 to 4/8) |
| `preamble <n>` | Set preamble length (1–65535) |
| `crc <on\|off>` | Enable/disable CRC |
| `iq <normal\|invert>` | Set IQ inversion |
| `syncword <hex>` | Set LoRa sync word (e.g. 12, 34) |
| `header <explicit\|implicit>` | Set header mode |
| `ldro <auto\|on\|off>` | Low data rate optimization |
| `rxboost <on\|off>` | RX gain boost |
| `symtimeout <0..255>` | Symbol timeout |
| `standby <xosc\|rc>` | Standby clock source |
| `regulator <dcdc\|ldo>` | Power regulator mode |
| `modem <lora\|gfsk>` | Switch modulation type |

### GFSK Configuration

| Command | Description |
|---------|-------------|
| `bitrate <600..300000>` | GFSK bitrate in bps |
| `fdev <100..200000>` | Frequency deviation in Hz |
| `fskbw <hexcode>` | GFSK bandwidth filter code |
| `whitening <on\|off>` | Data whitening |

### Spectrum & Scanning

| Command | Description |
|---------|-------------|
| `scan [band\|start end step]` | RSSI frequency sweep |
| `scanpreset <433\|868\|915\|all>` | Preset band scan |
| `analyze [band\|start end step]` | Live RSSI analyzer |
| `analyzecfg <key> <value>` | Configure analyzer (peak/threshold) |

### Meshtastic

| Command | Description |
|---------|-------------|
| `meshlisten <preset> [region\|MHz]` | Passive Meshtastic listener |
| `meshkey <32hex>\|default` | Set AES key for decryption |

Presets: `longfast`, `longslow`, `shortfast`
Regions: `us`, `eu868`, `eu433`, `cn`, `jp`, `anz`, `kr`, `tw`, `in`, `ru`

### Encrypted Chat

| Command | Description |
|---------|-------------|
| `chatjoin <room> [hexkey]` | Join encrypted chatroom (key: 8–32 hex chars) |
| `chatnick <name>` | Set nickname (max 12 chars) |
| `chat <message>` | Send message (or type directly in chat mode) |
| `chatstatus` | Show chat room info |
| `chatleave` | Leave chatroom |

### System

| Command | Description |
|---------|-------------|
| `version` / `ver` | Firmware version and board info |
| `uptime` | MCU uptime |
| `sleep` | Radio standby (low power) |
| `clear` / `cls` | Clear terminal screen |
| `reboot` | Software reset of MCU |

---

## Encryption Details

### Chat Encryption
- **Cipher**: XTEA in CTR mode (64-bit block, 32 rounds)
- **Authentication**: SipHash-2-4 (64-bit MAC tag)
- **Key**: 128-bit — auto-generated or user-provided (8–32 hex chars, expanded to 16 bytes)
- **Nonce**: Derived from room ID + sender ID + sequence number
- **Replay protection**: Per-peer sequence number tracking (8 peer slots)

### Meshtastic Decryption
- **Cipher**: AES-128-CTR
- **Default key**: LongFast channel key (`d4f1bb3a20290759f0bcffabcf4e6901`)
- **Custom keys**: Set with `meshkey <32 hex chars>`
- **Decoding**: Binary OTA header parsing + protobuf Data message extraction

---

## Project Structure

```
STM32_DX-LR20_LORA/
├── src/
│   └── main.cpp                  # Firmware: serial CLI + all application logic
├── lib/SX1262_Custom/
│   ├── sx1262_driver.h           # Custom SX1262 driver header
│   └── sx1262_driver.cpp         # Custom SX1262 driver implementation
├── Flipper_Zero_Lora_Terminal/
│   ├── lora_terminal.fap         # Pre-built Flipper Zero app
│   └── README.md                 # Flipper app documentation
├── platformio.ini                # PlatformIO build configuration
└── build_hex.bat                 # HEX generation script for ISP flashing
```

---

## Custom SX1262 Driver

This project uses a **from-scratch SX1262 driver** instead of third-party libraries (RadioLib, LoRaRF, etc.). The driver was built from the manufacturer's working reference code after external libraries failed to communicate with the DX-LR20 module.

Key design decisions:
- **Exact manufacturer init sequence** — STANDBY_RC → crystal trim → STANDBY_XOSC → DC-DC → PA config
- **Crystal trimming** — calibrates the TCXO capacitor values (critical for frequency accuracy on this module)
- **Explicit RF switch control** — the DX-LR20 requires manual TXEN/RXEN GPIO toggling
- **Object-oriented C++** — clean `SX1262Radio` class with runtime-configurable parameters
- **Zero external dependencies** — only requires Arduino SPI

---

## License

This project is open source and available for personal and educational use.

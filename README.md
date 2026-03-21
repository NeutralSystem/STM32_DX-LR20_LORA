# STM32 LoRa Terminal

A feature-rich serial CLI firmware for **STM32F103C8 "Blue Pill"** + **SX1262 LoRa radio** (DX-LR30 module), turning cheap hardware into a versatile LoRa tool with packet sniffing, spectrum analysis, encrypted chat, Meshtastic monitoring, and full on-the-fly radio configuration — all from a serial terminal.
<img src="https://github.com/NeutralSystem/STM32_DX-LR20_LORA/blob/main/STM32DXLR20.png" alt="Diagram" width="600" />
![Platform](https://img.shields.io/badge/platform-STM32F103C8-blue)
![Radio](https://img.shields.io/badge/radio-SX1262-green)
![Framework](https://img.shields.io/badge/framework-Arduino%20%2B%20PlatformIO-orange)

---

## Features

### Radio & Messaging
- **LoRa TX/RX** — send text or raw hex payloads, receive with full metadata (RSSI, SNR, frequency error)
- **GFSK mode** — switch between LoRa and FSK modulation on the fly
- **Beacon mode** — periodic automatic transmissions at configurable intervals
- **Packet sniffer** — continuous receive with hex dump, ASCII sidebar, and live statistics

### Spectrum Tools
- **Band scanner** — RSSI sweep across frequency ranges (presets for 433/868/915 MHz or custom)
- **Live spectrum analyzer** — real-time in-place RSSI table with peak hold and configurable threshold

### Meshtastic Monitoring
- **Passive Meshtastic listener** — apply LongFast/LongSlow/ShortFast presets and sniff Meshtastic traffic
- **Protocol decoder** — best-effort protobuf field extraction (from/to/id/channel/hop-limit/portnum)

### Encrypted Chat
- **AES-encrypted chatrooms** — join numbered rooms with shared keys
- **Auto key generation** — firmware generates and displays keys for easy sharing
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
| GFSK Bitrate | 600–300000 bps | `bitrate 50000` |
| GFSK Freq Dev | 100–200000 Hz | `fdev 25000` |

---

## Hardware

| Component | Details |
|-----------|---------|
| **MCU** | STM32F103C8T6 "Blue Pill" (64 KB Flash, 20 KB RAM) |
| **Radio Module** | DX-LR30 (SX1262) |
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
- STM32F103C8 board with DX-LR30 SX1262 module

### Build & Flash

```bash
# Clone the repository
git clone https://github.com/NeutralSystem/STM32_DX-LR20_LORA.git
cd STM32LORA

# Build with PlatformIO
pio run

# Upload via serial (hold BOOT0 during reset, then release)
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
> help                          # Show all commands
> send Hello World              # Transmit a LoRa message
> sniff                         # Enter continuous receive mode (Ctrl+C to stop)
> freq 868.100                  # Change frequency to 868.1 MHz
> sf 12                         # Set spreading factor to 12
> power 22                      # Set TX power to max (+22 dBm)
> scan 433                      # Scan the 433 MHz band
> analyze 868                   # Live spectrum analyzer on 868 MHz
> beacon 5000 PING              # Transmit "PING" every 5 seconds
> meshlisten longfast 906.875   # Listen for Meshtastic traffic
> chatjoin 7                    # Join encrypted chat room 7
> chatnick Alice                # Set your nickname
> Hello everyone!               # Send encrypted message to room
> status                        # Show full radio configuration
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
| `stop` | Stop sniff/beacon/analyze |
| `status` | Show full radio configuration |
| `rssi` | Read instantaneous RSSI |
| `reset` | Re-initialize radio |

### Radio Configuration

| Command | Description |
|---------|-------------|
| `freq <MHz>` | Set RF frequency |
| `power <dBm>` | Set TX power (-9 to +22) |
| `sf <5..12>` | Set spreading factor |
| `bw <kHz>` | Set bandwidth |
| `cr <5..8>` | Set coding rate |
| `preamble <n>` | Set preamble length |
| `crc <on\|off>` | Enable/disable CRC |
| `iq <normal\|invert>` | Set IQ inversion |
| `syncword <hex>` | Set LoRa sync word |
| `header <explicit\|implicit>` | Set header mode |
| `modem <lora\|gfsk>` | Switch modulation type |

### Spectrum & Scanning

| Command | Description |
|---------|-------------|
| `scan [band\|start end step]` | RSSI frequency sweep |
| `scanpreset <433\|868\|915\|all>` | Preset band scan |
| `analyze [band\|start end step]` | Live RSSI analyzer |
| `analyzecfg <key> <value>` | Configure analyzer (peak/threshold) |
| `meshlisten <preset> <freq>` | Meshtastic passive listener |

### Encrypted Chat

| Command | Description |
|---------|-------------|
| `chatjoin <room> [hexkey]` | Join encrypted chatroom |
| `chatnick <name>` | Set nickname (max 12 chars) |
| `chatstatus` | Show chat room info |
| `chatleave` | Leave chatroom |

---

## Project Structure

```
STM32LORA/
├── src/
│   └── main.cpp              # Firmware: serial CLI + all application logic
├── lib/SX1262_Custom/
│   ├── sx1262_driver.h       # Custom SX1262 driver header
│   └── sx1262_driver.cpp     # Custom SX1262 driver implementation
├── platformio.ini            # PlatformIO build configuration
└── build_hex.bat             # HEX generation script for ISP flashing
```

---

## Custom SX1262 Driver

This project uses a **from-scratch SX1262 driver** instead of third-party libraries (RadioLib, LoRaRF, etc.). The driver was built from the manufacturer's working reference code after external libraries failed to communicate with the DX-LR30 module.

Key design decisions:
- **Exact manufacturer init sequence** — STANDBY_RC → crystal trim → STANDBY_XOSC → DC-DC → PA config
- **Crystal trimming** — calibrates the TCXO capacitor values (critical for frequency accuracy on this module)
- **Explicit RF switch control** — the DX-LR30 requires manual TXEN/RXEN GPIO toggling
- **Object-oriented C++** — clean `SX1262Radio` class with runtime-configurable parameters
- **Zero external dependencies** — only requires Arduino SPI

---

## License

This project is open source and available for personal and educational use.

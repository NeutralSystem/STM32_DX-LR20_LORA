# Flipper Zero LoRa Terminal

A **Flipper Zero application** (.fap) that turns the Flipper into a full-featured controller for the **DX-LR20 STM32 LoRa board** — no laptop required. Send messages, sniff packets, scan frequencies, configure radio parameters, and run AES-encrypted chat — all from the Flipper's screen and buttons.

![Flipper](https://img.shields.io/badge/device-Flipper%20Zero-orange)
![Radio](https://img.shields.io/badge/radio-SX1262%20%2F%20LLCC68-green)
![Interface](https://img.shields.io/badge/interface-UART%20115200-blue)
![Category](https://img.shields.io/badge/category-GPIO-lightgrey)

> **Requires**: A DX-LR20 LoRa board running the [STM32_DX-LR20_LORA](https://github.com/NeutralSystem/STM32_DX-LR20_LORA) firmware.

---

## Features

| Scene | What it does |
|-------|-------------|
| **Send Message** | On-screen keyboard to type and transmit LoRa text messages |
| **Sniff Packets** | Continuous receive mode — scrolling display of incoming packets with RSSI/SNR |
| **Frequency Scan** | RSSI sweep across 433 / 868 / 915 MHz bands (or all at once) |
| **Radio Config** | Live-adjust Modem, SF, BW, CR, TX Power, and CRC without recompiling |
| **Encrypted Chat** | Join AES-encrypted chat rooms by room number — full duplex with live RX |
| **Radio Status** | Query and display the current radio configuration at a glance |

---

## Installation

### Pre-Built (Easiest)

1. Copy **`lora_terminal.fap`** from this folder to your Flipper Zero SD card:
   ```
   SD Card/apps/GPIO/lora_terminal.fap
   ```
2. On the Flipper, navigate to **Apps → GPIO → LoRa Terminal**.

You can copy the file using:
- **qFlipper** (desktop app — drag and drop)
- **Flipper Zero mobile app** (File Manager)
- **Direct SD card access** (pop the micro SD and use a reader)

### Build From Source

If you want to build it yourself (or modify the code):

```bash
# 1. Clone the Flipper Zero firmware (with submodules)
git clone --recursive https://github.com/flipperdevices/flipperzero-firmware.git
cd flipperzero-firmware

# 2. Copy the app source into the firmware tree
#    (copy the flipper_app folder from this repo)
cp -r /path/to/STM32_DX-LR20_LORA/flipper_app applications_user/lora_terminal

# 3. Build the FAP
./fbt fap_lora_terminal        # Linux/macOS
.\fbt.cmd fap_lora_terminal    # Windows

# 4. The built .fap will be at:
#    build/f7-firmware-D/.extapps/lora_terminal.fap
```

> **Note**: The firmware toolchain (~700 MB) downloads automatically on first build. This is a one-time setup.

---

## Wiring — Flipper Zero to DX-LR20 Board

Only **3 wires** are needed. Both devices operate at **3.3V logic** — no level shifter required.

```
 Flipper Zero                    DX-LR20 / STM32 Board
 ────────────                    ──────────────────────
 Pin 13 (TX)  ──────────────────→  PA10 (RX)
 Pin 14 (RX)  ←──────────────────  PA9  (TX)
 Pin 8  (GND) ──────────────────→  GND
```

### Flipper GPIO Header Reference

```
              Flipper Zero (top edge, screen facing you)
         ┌──────────────────────────────────────────────┐
         │  1  2  3  4  5  6  7  8  9 10 11 12 13 14  │
         │ 15 16 17 18 19 20 21 22 23 24 25 26 27 28  │
         └──────────────────────────────────────────────┘

  Pin  1 = +5V             Pin  8 = GND
  Pin 13 = USART TX (PA14) Pin 14 = USART RX (PA13)
```

| Flipper Pin | Signal | Direction | STM32 Pin | Notes |
|:-----------:|--------|:---------:|:---------:|-------|
| **1** | +5V | Power OUT | 5V / VIN | Powers the LoRa board (see below) |
| **8** | GND | Ground | GND | Common ground — **always connect** |
| **13** | TX | Flipper → STM32 | PA10 (RX) | UART data to board |
| **14** | RX | STM32 → Flipper | PA9 (TX) | UART data from board |

---

## Powering the LoRa Board from the Flipper

The Flipper Zero can supply **5V @ ~400 mA** from **Pin 1** on the GPIO header when running on battery (higher when USB-connected). The DX-LR20 board draws approximately **80–120 mA** during TX at +22 dBm and under **30 mA** during RX/idle — well within the Flipper's budget.

### Wiring for Flipper-Powered Operation (4 wires)

```
 Flipper Zero                    DX-LR20 / STM32 Board
 ────────────                    ──────────────────────
 Pin  1 (+5V) ──────────────────→  5V  (VIN)
 Pin  8 (GND) ──────────────────→  GND
 Pin 13 (TX)  ──────────────────→  PA10 (RX)
 Pin 14 (RX)  ←──────────────────  PA9  (TX)
```

> **Important**: Connect 5V to the board's **5V/VIN** pin (not the 3.3V pin). The STM32 board has its own 3.3V regulator that will step down from 5V.

### Building a Self-Contained Module

To build a clean, portable Flipper + LoRa module:

**Parts needed:**
| Part | Purpose | Approximate Cost |
|------|---------|:----------------:|
| DX-LR20 STM32 LoRa board | LoRa radio + MCU | ~$5–8 |
| 4× female-to-female Dupont jumpers | Wiring harness | ~$1 |
| Flipper Zero GPIO pin header (2×7 female) | Board-side connector | ~$0.50 |
| Small piece of protoboard (optional) | Mount point | ~$1 |
| Heat shrink tubing (optional) | Strain relief / insulation | ~$1 |

**Assembly steps:**

1. **Flash the STM32 board** with the [DX-LR20 LoRa firmware](https://github.com/NeutralSystem/STM32_DX-LR20_LORA) using PlatformIO or a HEX flasher (STLink / USB-Serial + boot0 jumper).

2. **Prepare the wiring harness** — cut 4 Dupont jumper wires to equal length (~8–10 cm). Use color coding:
   - 🔴 Red → 5V power (Pin 1 → VIN)
   - ⚫ Black → Ground (Pin 8 → GND)
   - 🟢 Green → TX (Pin 13 → PA10)
   - 🔵 Blue → RX (Pin 14 → PA9)

3. **Solder or connect** the wires to the DX-LR20 board header pins (5V, GND, PA10, PA9).

4. **Optional — protoboard mount**: Solder the DX-LR20 board onto a small piece of protoboard. Add a 2×7 female pin header that plugs directly into the Flipper's GPIO. Route the 4 traces/wires from the header to the board pads. This creates a clean plug-in module.

5. **Optional — case**: Use heat shrink or a 3D-printed enclosure to protect the board and antenna connection.

6. **Antenna**: Ensure a proper antenna is connected to the DX-LR20 board's SMA/u.FL connector. **Never transmit without an antenna** — this can damage the SX1262 PA.

### Wiring Diagram

```
                    Flipper GPIO Header (top view)
    ┌────────────────────────────────────────────────────────┐
    │ [5V]  2   3   4   5   6   7  [GND]  9  10  11  12 [TX] [RX] │
    │  15  16  17  18  19  20  21   22   23  24  25  26  27   28   │
    └────────────────────────────────────────────────────────┘
      │                             │                        │    │
      │ Red                         │ Black                  │    │
      │                             │                  Green │    │ Blue
      ▼                             ▼                        ▼    ▼
    ┌─────────────────────────────────────────────────────────────────┐
    │                     DX-LR20 LoRa Board                         │
    │                                                                 │
    │  [5V/VIN]  [GND]  [PA9/TX]  [PA10/RX]            [SMA Antenna] │
    │                       │          │                       │      │
    │                       └──────────┘                       │      │
    │                       UART to Flipper              Antenna      │
    └─────────────────────────────────────────────────────────────────┘
```

---

## Usage Guide

### Main Menu

When you launch the app, you'll see six options:

1. **Send Message** — Opens the Flipper keyboard. Type your message and press Enter to transmit via LoRa.

2. **Sniff Packets** — Puts the radio in continuous RX. Incoming packets scroll on screen with RSSI and SNR metadata. Press Back to stop and return.

3. **Frequency Scan** — Choose a band (All / 433 MHz / 868 MHz / 915 MHz). The board sweeps the band and reports RSSI levels at each step. Results scroll on screen.

4. **Radio Config** — Scroll through radio parameters and adjust them with Left/Right:
   - **Modem**: LoRa or GFSK
   - **Spreading Factor**: SF5 through SF12
   - **Bandwidth**: 7.8 kHz to 500 kHz
   - **Coding Rate**: 4/5 through 4/8
   - **TX Power**: -9 to +22 dBm
   - **CRC**: On or Off

   Changes are sent to the board immediately — no save step needed.

5. **Encrypted Chat** — Enter a room number (0–255) to join. Once joined:
   - Incoming messages appear automatically
   - Press Back to leave the room
   - Messages are AES-encrypted by the STM32 firmware

6. **Radio Status** — Sends the `status` command and displays the current radio configuration.

### Tips

- **Antenna first**: Always connect an antenna to the DX-LR20 board before transmitting.
- **Range check**: Use low SF and high BW for close-range speed, high SF and low BW for maximum range.
- **Packet sniffing**: Both sides must use the same frequency, SF, BW, CR, and sync word to see each other's packets.
- **Power draw**: At +22 dBm, the radio draws ~120 mA during TX. Lower the TX power if you need to conserve Flipper battery.

---

## Source File Structure

```
flipper_app/
├── application.fam          # App manifest (name, category, icon, entry point)
├── lora_terminal.h          # Shared types, enums, and function declarations
├── lora_terminal_app.c      # Entry point — app alloc/free, scene wiring
├── uart_bridge.c            # UART init/deinit, send, receive, buffer management
├── icons/
│   └── lora_10px.png        # App icon (10×10 1-bit PNG)
└── scenes/
    ├── scene_main_menu.c    # Main menu (6 items)
    ├── scene_send.c         # Text input → transmit
    ├── scene_sniff.c        # Continuous packet receiver
    ├── scene_scan.c         # Frequency band scanner
    ├── scene_config.c       # Radio parameter editor
    └── scene_chat.c         # Encrypted chat room
```

---

## Compatibility

| Requirement | Details |
|-------------|---------|
| **Flipper Zero firmware** | Official firmware (latest release recommended) |
| **STM32 board firmware** | [STM32_DX-LR20_LORA](https://github.com/NeutralSystem/STM32_DX-LR20_LORA) |
| **UART** | 115200 baud, 8N1 |
| **Logic levels** | 3.3V (native on both devices) |
| **Power** | 5V from Flipper Pin 1, or external USB to the STM32 board |

---

## Troubleshooting

| Problem | Solution |
|---------|----------|
| App shows no response | Check wiring — TX/RX may be swapped. Flipper TX goes to STM32 RX (PA10). |
| Garbled text on screen | Verify both ends are set to 115200 baud. Reset the STM32 board. |
| Board not powering on from Flipper | Ensure 5V goes to the board's VIN/5V pin, not the 3.3V pin. Check GND. |
| "No packets" during sniff | Confirm a transmitter is active on the same frequency, SF, BW, CR, and sync word. |
| Short range | Attach a proper antenna. Increase SF and decrease BW for longer range. |
| Flipper battery drains fast | Reduce TX power (`Radio Config → TX Power`). Avoid continuous TX (beacon mode). |

---

## License

This project is open source and available for personal and educational use. See the [main repository](https://github.com/NeutralSystem/STM32_DX-LR20_LORA) for details.

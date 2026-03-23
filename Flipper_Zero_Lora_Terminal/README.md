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
| **Radio Config** | Live-adjust all 13 radio parameters without recompiling |
| **Encrypted Chat** | Join XTEA-encrypted chat rooms — full duplex with live RX, press OK to compose |
| **Mesh Listen** | Meshtastic passive listener — pick preset + region, auto-decrypts with AES-128 |
| **Radio Status** | Query and display the current radio configuration at a glance |
| **About** | Project info, hardware details, and GitHub link |

---

## Installation

1. Download **`lora_terminal.fap`** from this folder.
2. Copy it to your Flipper Zero SD card:
   ```
   SD Card/apps/GPIO/lora_terminal.fap
   ```
3. On the Flipper, navigate to **Apps → GPIO → LoRa Terminal**.

You can copy the file using:
- **qFlipper** (desktop app — drag and drop)
- **Flipper Zero mobile app** (File Manager)
- **Direct SD card access** (pop the micro SD and use a reader)

---

## Project Picture

### Simplest Flipper module possible.
#### Front

<img src="https://github.com/NeutralSystem/STM32_DX-LR20_LORA/blob/main/Flipper_Zero_Lora_Terminal/FlipperPic/LoraFlipper1.png" alt="Diagram" width="200" />

#### Back

<img src="https://github.com/NeutralSystem/STM32_DX-LR20_LORA/blob/main/Flipper_Zero_Lora_Terminal/FlipperPic/LoraFlipper2.png" alt="Diagram" width="200" />



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
         │  1  2  3  4  5  6  7  8  9 10 11 12 13 14    │
         │ 15 16 17 18 19 20 21 22 23 24 25 26 27 28    │
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
    ┌─────────────────────────────────────────────────────────────┐
    │ [5V]  2   3   4   5   6   7  [GND]  9  10  11  12 [TX] [RX] │
    │  15  16  17  18  19  20  21   22   23  24  25  26  27   28  │
    └─────────────────────────────────────────────────────────────┘
      │                             │                     │    │
      │ Red                         │ Black               │    │
      │                             │               Green │    │ Blue
      ▼                             ▼                     ▼    ▼
    ┌─────────────────────────────────────────────────────────────────┐
    │                     DX-LR20 LoRa Board                          │
    │                                                                 │
    │  [5V/VIN]  [GND]  [PA9/TX]  [PA10/RX]            [SMA Antenna]  │
    │                       │          │                       │      │
    │                       └──────────┘                       │      │
    │                       UART to Flipper              Antenna      │
    └─────────────────────────────────────────────────────────────────┘
```

---

## Usage Guide

### Main Menu

When you launch the app, you'll see eight options:

1. **Send Message** — Opens the Flipper keyboard. Type your message and press Enter to transmit via LoRa.

2. **Sniff Packets** — Puts the radio in continuous RX. Incoming packets scroll on screen with RSSI, SNR, hex dump, and Meshtastic decode (if enabled). Press Back to stop.

3. **Frequency Scan** — Choose a band (All / 433 MHz / 868 MHz / 915 MHz). The board sweeps the band and reports RSSI levels at each step. Results scroll on screen.

4. **Radio Config** — Scroll through 13 radio parameters and adjust with Left/Right:
   - **Frequency**: 433.000 / 433.125 / 868.000 / 868.100 / 915.000 / 906.875 MHz
   - **Modem**: LoRa or GFSK
   - **Spreading Factor**: SF5 through SF12
   - **Bandwidth**: 7.8 kHz to 500 kHz (10 options)
   - **Coding Rate**: 4/5 through 4/8
   - **TX Power**: -9 to +22 dBm
   - **Preamble**: 4 / 8 / 12 / 16 / 32 / 64 symbols
   - **CRC**: On or Off
   - **IQ Polarity**: Normal or Inverted
   - **Sync Word**: 0x12 (Private) or 0x34 (Public)
   - **Header Mode**: Explicit or Implicit
   - **LDRO**: Auto / On / Off
   - **RX Boost**: On or Off

   Changes are sent to the board immediately — no save step needed.

5. **Encrypted Chat** — Pre-filled with default room "Flipperchat 12345678" — edit or press OK to join. Once joined:
   - Incoming messages appear automatically (live RX)
   - Press **OK** to compose and send a message
   - Press **Back** to leave the room
   - Encryption: XTEA-CTR with SipHash-2-4 MAC

6. **Mesh Listen** — Meshtastic passive listener:
   - First, pick a preset: **LongFast** (SF11/BW250), **LongSlow** (SF12/BW125), or **ShortFast** (SF7/BW250)
   - Then pick a region: US, EU868, EU433, CN, JP, ANZ, KR, TW, IN, RU
   - The board auto-configures all LoRa parameters and starts sniffing
   - Received Meshtastic packets are decrypted (AES-128-CTR) and decoded
   - Press Back to stop and return

7. **Radio Status** — Sends the `status` command and displays the current radio configuration.

8. **About** — Project info, hardware details, encryption methods, and GitHub link.

### Tips

- **Antenna first**: Always connect an antenna to the DX-LR20 board before transmitting.
- **Range check**: Use low SF and high BW for close-range speed, high SF and low BW for maximum range.
- **Packet sniffing**: Both sides must use the same frequency, SF, BW, CR, and sync word to see each other's packets.
- **Power draw**: At +22 dBm, the radio draws ~120 mA during TX. Lower the TX power if you need to conserve Flipper battery.
- **5V auto-power**: The app automatically enables 5V on GPIO Pin 1 when launched and disables it on exit.

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

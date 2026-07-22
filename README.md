# FunModularKeyboard — Modular Macro Keypad

> An ESP32-S3 powered modular macro keyboard featuring multi-key mapping, voice commands, speech-to-text, and magnetic snap-on expansion modules, all configurable via a companion desktop app.

[![Platform](https://img.shields.io/badge/platform-ESP32--S3-green)](https://www.espressif.com/)
[![Framework](https://img.shields.io/badge/framework-Arduino-blue)](https://www.arduino.cc/)
[![License](https://img.shields.io/badge/license-MIT-yellow)](LICENSE)

---

## 📖 Overview

**FunModularKeyboard** is a highly customizable macro keypad consisting of **1 main module + 2 magnetic expansion modules**. The main unit communicates with expansion modules via a magnetic Pogo-Pin interface (I²C bus) with hot-plug support.

### 🧩 Hardware Architecture

| Module | MCU | Description |
|--------|-----|-------------|
| **Main Module** | ESP32-S3 | TFT LCD display, 16-key matrix, rotary encoder, speaker, MEMS mic, Li-Po battery, RGB LEDs, magnetic host interface |
| **Module A (ModA)** | STM8S103F3 | 3 rotary encoders + 2 slider potentiometers, I²C communication via magnetic connector |
| **Module B (ModB)** | ESP32-S3 | Based on [SmartKnob](https://github.com/scottbez1/smartknob) — brushless motor knob + strain gauge press sensing + LED ring, I²C via magnetic connector |

```
┌─────────────────────────────────────────────┐
│            Main Module (ESP32-S3)            │
│  ┌─────┐  ┌──────────┐  ┌────────────────┐  │
│  │ TFT │  │ 16-Key   │  │ Rotary Encoder │  │
│  │ LCD │  │ Matrix   │  │   + Button     │  │
│  └─────┘  └──────────┘  └────────────────┘  │
│  ┌─────┐  ┌──────┐  ┌──────┐  ┌─────────┐  │
│  │ Mic │  │Speaker│  │ RGB  │  │ Battery │  │
│  └─────┘  └──────┘  └──────┘  └─────────┘  │
│         ┌──────┐  ┌──────┐                  │
│         │ USB  │  │ BLE  │  Dual-Mode HID   │
│         └──────┘  └──────┘                  │
│     ════════ Magnetic Pogo-Pin Port ═══════  │
│          I²C + Power + UART                 │
├──────────────────┬──────────────────────────┤
│  Module A (STM8) │  Module B (ESP32-S3)     │
│  3× Encoders     │  BLDC Motor Knob (FOC)   │
│  2× Sliders      │  Strain Gauge Press      │
│                  │  8× LED Ring             │
└──────────────────┴──────────────────────────┘
```

---

## ✨ Features

### ⌨️ Keyboard
- **8 switchable keymap profiles** — toggle between profiles on-the-fly via the rotary encoder for different workflows
- **Macro key combos** — one keypress triggers multi-key combinations (e.g. `Ctrl+Win+D` to show desktop)
- **Multimedia controls** — play/pause, volume, track skip, etc.
- **USB HID + BLE dual-mode** — wired or wireless, your choice

### 🎤 Voice
- **Voice commands** — long-press the MIC key, speak a command, and trigger preset actions via Baidu ASR (e.g. "Open Spotify")
- **Speech-to-text** — voice input transcribed to text and sent to the PC
- Custom keyword-to-script bindings supported

### 💻 Companion Desktop App
A desktop configuration tool that communicates with the keyboard over WiFi/TCP:
- 🗺️ **Visual keymap editor** — drag-and-drop key function configuration
- ⚙️ **Device settings** — WiFi, RGB effects, display brightness/theme, volume, etc.
- 📜 **Script engine** — Python script support for one-click app launching and desktop automation
- 🔄 **Firmware OTA updates** — wirelessly upgrade keyboard firmware
- 📊 **PC status monitoring** — real-time CPU usage, disk space, and more shown on the keyboard display
- 🎵 **Music lyrics sync** — synchronized lyrics display while music plays
- 🏠 **HomeAssistant integration** — reserved interface for smart home control

### 🎨 Visual
- **TFT LCD with LVGL** — rich GUI with multiple theme support
- **16× WS2812B RGB LEDs** — rainbow, fire, meteor, pulse, and more effects
- **Music spectrum visualization** — real-time FFT audio spectrum animation
- **Status bar** — clock, battery level, connection status, active keymap profile, etc.

### 🧲 Expansion Modules
- **Module A** — 3 encoders + 2 sliders, ideal for color grading, audio mixing, video editing
- **Module B** — brushless motor knob (SimpleFOC), haptic feedback, strain-gauge press sensing, premium tactile feel
- Expansion modules are **plug-and-play** via the magnetic connector with auto-detection

---

## 📁 Project Structure

```
FunModularKeyboard-OPEN/
├── firmware/                         # Firmware source code
│   ├── FunModularKeyboard/           # Main module firmware (ESP32-S3, Arduino)
│   │   ├── src/                      # Core source
│   │   │   ├── main.cpp              # Entry point
│   │   │   ├── MainTask.cpp/h        # Main task scheduler
│   │   │   ├── DisplayTask.cpp/h     # LVGL display task
│   │   │   ├── MatrixScanner.cpp/h   # Key matrix scanner
│   │   │   ├── RotaryEncoder.cpp/h   # Rotary encoder driver
│   │   │   ├── RGBLightControl.cpp/h # RGB LED effects controller
│   │   │   ├── USBKeyboardImpl.cpp/h # USB HID keyboard implementation
│   │   │   ├── BLEKeyboardImpl.cpp/h # BLE keyboard implementation
│   │   │   ├── VoiceRecognizer.cpp/h # Baidu ASR voice recognition
│   │   │   ├── AudioAnalyzer.cpp/h   # FFT audio spectrum analyzer
│   │   │   ├── Mic.cpp/h             # MEMS microphone driver
│   │   │   ├── Speaker.cpp/h         # Speaker driver
│   │   │   ├── SerialProtocol.cpp/h  # Host app communication protocol
│   │   │   ├── Configuration.cpp/h   # Configuration file manager
│   │   │   ├── Upgrade.cpp/h         # OTA firmware upgrade
│   │   │   ├── LogManager.cpp/h      # Logging system
│   │   │   └── modules/              # I²C master controller & extensions
│   │   ├── data/                     # SPIFFS data files
│   │   │   ├── config.ini            # Default configuration
│   │   │   └── keymap1~8.ini         # 8 keymap profiles
│   │   └── platformio.ini            # PlatformIO build configuration
│   ├── FunModularKeyboard_ModA/      # Module A firmware (STM8S103F3, IAR)
│   │   ├── main.c                    # Main program
│   │   ├── ec11.c/h                  # EC11 encoder driver
│   │   ├── adc.c/h                   # ADC slider reading
│   │   ├── i2c.c/h                   # I²C slave communication
│   │   └── Uart.c/h                  # UART debug output
│   └── FunModularKeyboard_ModB/      # Module B firmware (ESP32-S3, PlatformIO)
│       └── firmware/src/             # Modified SmartKnob-based firmware
├── electronics/                      # PCB designs (LCSC EDA / EasyEDA)
│   ├── MainBoard/                    # Main board PCB project
│   ├── ModA/                         # Module A PCB project
│   └── ModB/                         # Module B PCB project
├── cad/                              # 3D mechanical files
│   ├── Top.step / Bottom.step        # Main enclosure shells
│   ├── Plate.step                    # Switch mounting plate
│   ├── Keycap.step / Keycap2.step    # Keycaps
│   ├── FunModularKeyboard.3mf        # 3D printing project file
│   ├── ModA/                         # Module A enclosure
│   └── ModB/                         # Module B enclosure
├── software/scripts/                 # Desktop automation scripts
│   ├── desktop_automation_launch_apps_template.py  # One-click app launcher template
│   └── desktop_automation_long_press_shutdown.py   # Long-press shutdown script
├── tools/                            # Utility tools
└── README.md
```

---

## 🔧 Tech Stack

| Category | Technology |
|----------|------------|
| **Main MCU** | ESP32-S3 (240 MHz, 320 KB RAM, 8 MB Flash) |
| **Module A MCU** | STM8S103F3 |
| **Module B MCU** | ESP32-S3 |
| **Framework** | Arduino (PlatformIO) / IAR (STM8) |
| **GUI Library** | LVGL 8.3 + TFT_eSPI |
| **Keyboard Protocols** | USB HID (TinyUSB) + BLE HID |
| **Voice Recognition** | Baidu Speech Recognition API (REST) |
| **Motor Control** | SimpleFOC 2.3 |
| **Communication** | I²C (inter-module) / WiFi TCP (host app) |
| **File System** | SPIFFS |
| **RGB Driver** | FastLED (WS2812B) |
| **FFT Analysis** | arduinoFFT |

### Key Library Dependencies

```
ESP32 BLE Keyboard    — BLE HID keyboard
FastLED               — RGB LED control
TFT_eSPI              — TFT display driver
lvgl                  — Embedded GUI framework
ESP32-audioI2S        — I²S audio input
SimpleIni             — INI config file parser
ArduinoJson           — JSON serialization
arduinoFFT            — Fast Fourier Transform
SimpleFOC             — FOC motor control
TLV493D               — 3D magnetic sensor
```

---

## 🚀 Getting Started

### Main Module Firmware

1. Install [PlatformIO IDE](https://platformio.org/install) (VS Code extension)

2. Clone the repo and navigate to the main firmware directory:
```bash
git clone https://github.com/yourname/FunModularKeyboard-OPEN.git
cd FunModularKeyboard-OPEN/firmware/FunModularKeyboard
```

3. Configure WiFi and Baidu Speech API credentials (edit `data/config.ini` or configure via serial)

4. Build and flash:
```bash
# Build
pio run

# Flash firmware + upload SPIFFS data
pio run --target upload
pio run --target uploadfs
```

### Module A Firmware

Open `firmware/FunModularKeyboard_ModA/RGB_INIT.eww` with **IAR for STM8**, then build and flash.

### Module B Firmware

```bash
cd FunModularKeyboard-OPEN/firmware/FunModularKeyboard_ModB
pio run --target upload
```

### Pinout Reference

Main module key pin assignments (see `platformio.ini` and source code for full details):

| Function | GPIO |
|----------|------|
| LED Data | 6 |
| LED Power | 36 |
| Encoder CLK | 5 |
| Encoder DT | 21 |
| Encoder SW | 9 |
| I²C SDA | 15 |
| I²C SCL | 8 |
| Key Matrix Rows | 48, 10, 47, 33, 14 |
| Key Matrix Cols | 35, 34, 7, 13 |

---

## 🎮 Usage Guide

### Keymap Configuration
1. Connect to the companion desktop app over WiFi
2. Use the visual drag-and-drop editor to assign functions to each key (single key, combo, macro, media control, etc.)
3. Configure up to 8 different keymap profiles — switch between them by long-pressing the rotary encoder

### Voice Features
1. Configure your Baidu API Key and Secret Key in the companion app
2. Long-press the MIC-mapped key to start recording
3. The recognized speech is matched against preset keywords and triggers the corresponding script

### Firmware Upgrade
1. The companion app pushes new firmware to the keyboard over WiFi
2. The keyboard auto-reboots into OTA mode and completes the upgrade

---

## 🏗️ Hardware Fabrication

- **PCBs**: Open the `.epro2` project files under `electronics/` with [EasyEDA](https://pro.lceda.cn/)
- **3D Enclosure**: STEP mechanical files and 3MF print-ready files are provided in `cad/`
- **Magnetic Connector**: Uses Pogo-Pin spring-loaded contacts for I²C communication and power delivery between modules

---

## 📝 Roadmap

- [ ] Open-source the companion desktop configuration app
- [ ] Community-contributed expansion modules
- [ ] Full HomeAssistant integration
- [ ] Macro recording and playback
- [ ] Support for additional speech recognition engines
- [ ] Complete English documentation

---

## 🤝 Contributing

Issues and Pull Requests are welcome! If you've designed a cool expansion module, we'd love to see it shared here.

---

## 🙏 Acknowledgments

- [SmartKnob](https://github.com/scottbez1/smartknob) — excellent open-source haptic feedback knob project
- [LVGL](https://lvgl.io/) — lightweight embedded GUI library
- [PlatformIO](https://platformio.org/) — cross-platform embedded development ecosystem
- [Baidu Speech Recognition](https://ai.baidu.com/tech/speech) — Chinese speech recognition service


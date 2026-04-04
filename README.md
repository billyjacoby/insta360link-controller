# Insta360 Link Controller

[![GitHub Release](https://img.shields.io/github/v/release/jfwoods/insta360link-joystick-controller)](https://github.com/jfwoods/insta360link-joystick-controller/releases/latest)
[![CI](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/ci.yml/badge.svg)](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/ci.yml)
[![Release](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/release.yml/badge.svg)](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/release.yml)

The first-party control software for the [Insta360 Link](https://www.insta360.com/product/insta360-link) is (no offense) bloated garbage, even though the hardware is pretty solid. So, I took matters into my own hands. 

This implementation controls the camera directly via USB/UVC extension units through `linkctl-daemon` a lightweight daemon — no Insta360 desktop app required. Interactions with the daemon can be driven by either:

1. A CLI client `linkctl` 
2. An ESP32-based joystick controller 

This repo contains the ESP32 firmware, Mac daemon and CLI source codes, and KiCad PCB design files for a custom handheld controller.

```
[Insta360 Link] ──USB/UVC──> [linkctl-daemon (Mac)]
                                                    ──WiFi/WebSocket──> [ESP32 + Joystick]
                                                    ──CLI Client──────> [linkctl Interface]
```
## Opportunities For Future Work:
- [ ] **Windows Support**: this is on my todo list
- [ ] **Linux Support**: this is not on my todo list currently, but PRs are welcome :)
- [ ] **Keyframe Implementation**: as I've been using this, the ability to have orientation presets has become the *one* feature I've missed from the first-party software. This is on my todo list for after Windows support

## Setup

### 1. Install the Mac daemon

The daemon controls the Insta360 Link over USB and exposes a WebSocket + TCP interface for clients.

**Option A: Download the latest release**

Download `linkctl-daemon-*-macos-*.tar.gz` from the [latest release](https://github.com/jfwoods/insta360link-joystick-controller/releases/latest), then:

```bash
tar xzf linkctl-daemon-*-macos-*.tar.gz
cd linkctl-daemon
./install.sh
```

**Option B: Build from source**

```bash
# Install dependencies
brew install libwebsockets cjson openssl@3

# Build
cd host/linkctl-daemon
mkdir -p build && cd build
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH" cmake ..
make

# Run
./linkctl-daemon
```

The daemon finds the Insta360 Link over USB, starts a WebSocket server on port 9000 and a TCP control socket on port 9001, and advertises itself via mDNS.

### 2. Install the CLI client (optional)

**Option A: Download the latest release**

Download `linkctl-*-macos-*.tar.gz` from the [latest release](https://github.com/jfwoods/insta360link-joystick-controller/releases/latest), then:

```bash
tar xzf linkctl-*-macos-*.tar.gz
sudo cp linkctl/linkctl /usr/local/bin/
```

**Option B: Build from source**

```bash
cd host/linkctl
mkdir -p build && cd build
cmake ..
make

# Optional: install system-wide
sudo cmake --install . --prefix /usr/local
```

Or run directly from the build directory: `./build/linkctl <command>`.

### 3. Flash ESP32 firmware (if using joystick controller)

Requires [PlatformIO](https://platformio.org/).

```bash
pio run --target upload
```

### 4. Configure WiFi on the ESP32 (if using joystick controller)

On first boot, the ESP32 starts a captive portal:

1. Connect to the **Insta360-Controller** WiFi network from your phone or laptop
2. A setup page opens automatically — enter your WiFi network name and password
3. The ESP32 saves the credentials and reboots onto your network

Alternatively, configure via the serial CLI (115200 baud):

```
wifi MyNetwork MyPassword
connect
```

### 5. Use it

**CLI:** Run `linkctl status` to verify the daemon is running and the camera is connected, then use `linkctl jog` for interactive control.

**Joystick:** Move the joystick to pan and tilt the camera. The ESP32 reads the analog input, converts it to direction and speed, and sends commands to the daemon over WebSocket. The ESP32 discovers the daemon automatically via mDNS — no IP configuration needed.

## Dependencies

### Daemon (`linkctl-daemon`)

| Dependency | Source | Purpose |
|-----------|--------|---------|
| [libwebsockets](https://libwebsockets.org/) | Homebrew | WebSocket server |
| [cJSON](https://github.com/DaveGamble/cJSON) | Homebrew | JSON parsing |
| [OpenSSL 3](https://www.openssl.org/) | Homebrew | Required by libwebsockets |
| IOKit | macOS (built-in) | USB device access |
| CoreFoundation | macOS (built-in) | System data structures |
| dns_sd | macOS (built-in) | mDNS advertisement |

### CLI (`linkctl`)

No external dependencies — pure POSIX C.

### ESP32 Firmware

| Dependency | Source | Purpose |
|-----------|--------|---------|
| [arduinoWebSockets](https://github.com/Links2004/arduinoWebSockets) | PlatformIO (`links2004/WebSockets@^2.4.1`) | WebSocket client |
| [ArduinoJson](https://github.com/bblanchon/ArduinoJson) | PlatformIO (`bblanchon/ArduinoJson@^7.0.0`) | JSON encoding/parsing |
| [PlatformIO](https://platformio.org/) | CLI or VS Code extension | Build toolchain (Arduino framework) |

## Daemon Options

```
./linkctl-daemon                  # Default: port 9000, mDNS enabled
./linkctl-daemon --port 8080      # Custom port
./linkctl-daemon --no-mdns        # Disable mDNS advertisement
./linkctl-daemon --test           # Test camera control and exit
```

## CLI Client (`linkctl`)

`linkctl` is a lightweight command-line client for controlling the Insta360 Link via the daemon's TCP control socket. It has zero external dependencies (pure POSIX C) and connects to the daemon at `localhost:9001`.

### Commands

| Command | Description |
|---------|-------------|
| `linkctl help` | Show usage information |
| `linkctl status` | Show camera connection state and current zoom |
| `linkctl center` | Center the gimbal and reset zoom to 1.0x |
| `linkctl zoom <1.0-4.0>` | Set zoom level (e.g., `linkctl zoom 2.5`) |
| `linkctl zoom` | Show the current zoom level |
| `linkctl stop` | Stop all gimbal movement |
| `linkctl jog` | Enter interactive pan/tilt mode |

### Jog Mode

`linkctl jog` enters an interactive mode for real-time camera control using keyboard input.

| Key | Action |
|-----|--------|
| `W` | Tilt up |
| `S` | Tilt down |
| `A` | Pan left |
| `D` | Pan right |
| `+` / `=` | Increase speed |
| `-` | Decrease speed |
| Space | Stop movement |
| `C` | Center gimbal |
| `Q` / ESC | Exit jog mode |

Movement continues while a key is held (via terminal key repeat) and automatically stops ~400ms after release. Default speeds: pan=15, tilt=10. Speed range: 3-30 (pan), 3-20 (tilt).

## ESP32 Serial CLI

Connect to the ESP32's serial port at 115200 baud. Type `help` for a full list.

| Command | Description |
|---------|-------------|
| `wifi <ssid> [pass]` | Set WiFi credentials |
| `connect` | Connect to WiFi and daemon |
| `disconnect` | Disconnect |
| `status` | Show connection state |
| `stop` | Stop gimbal movement |
| `center` | Center the gimbal |
| `joy` | Test joystick output (5 sec) |
| `calibrate` | Calibrate joystick center point |
| `invertx` / `inverty` | Toggle axis inversion |
| `portal` | Start captive portal for WiFi setup |
| `webconfig` | Start web config on current network |
| `host <ip>` | Override daemon IP (skip mDNS) |
| `port <port>` | Override daemon port |
| `clear` | Clear all saved config |
| `reboot` | Restart ESP32 |

## Protocol

The daemon exposes two interfaces:

- **WebSocket** on port **9000** — for the ESP32 joystick controller (JSON over WebSocket text frames)
- **TCP** on port **9001** — for the `linkctl` CLI client (newline-terminated text commands)

### WebSocket Commands (ESP32 → Daemon)

```json
{"cmd": "pan_tilt", "pan_dir": 1, "pan_speed": 15, "tilt_dir": 255, "tilt_speed": 10}
{"cmd": "zoom", "value": 250}
{"cmd": "stop"}
{"cmd": "center"}
{"cmd": "status"}
```

### WebSocket Responses (Daemon → ESP32)

```json
{"type": "status", "camera_connected": true, "zoom": 150}
{"type": "error", "message": "camera not found"}
```

Direction encoding: `1` = positive (right/up), `255` = negative (left/down), `0` = stop. Speed ranges: pan 0-30, tilt 0-20, zoom 100-400.

## How It Works

The Insta360 Link exposes pan/tilt/zoom controls as UVC extension unit registers accessible over USB. The `linkctl-daemon` writes to these registers directly using macOS IOKit, bypassing the Insta360 desktop app entirely. The daemon runs a WebSocket server on port 9000 that accepts JSON commands and translates them into the appropriate UVC control transfers, plus a TCP control socket on port 9001 for the `linkctl` CLI. The ESP32 reads the joystick, discovers the daemon via mDNS, and streams movement commands over WiFi. For use without an ESP32, the `linkctl` CLI connects to the daemon's TCP socket and sends commands directly from the terminal.

## Testing

The daemon includes an integration test suite that exercises WebSocket communication, camera control, error handling, and multi-client support. Requires the Insta360 Link to be connected.

```bash
# Run all tests (camera will move)
python3 host/linkctl-daemon/test_daemon.py

# Skip tests that move the camera
python3 host/linkctl-daemon/test_daemon.py --skip-motion
```

## Hardware

### ESP32-WROOM-32 (Breadboard Prototype)

| Function | GPIO | Notes |
|----------|------|-------|
| Joystick X (pan) | 34 | ADC1, input only |
| Joystick Y (tilt) | 35 | ADC1, input only |
| Joystick button (home) | 32 | ADC1, internal pull-up, active low |
| Zoom potentiometer | 33 | ADC1 |
| I2C SDA | 21 | Hardware I2C default |
| I2C SCL | 22 | Hardware I2C default |
| Calibrate button | 25 | Internal pull-up, active low |
| Spare button | 26 | Internal pull-up, active low |
| Status LED | 2 | On-board LED |

> **Note:** Use ADC1 pins (GPIO 32-39) for analog inputs — ADC2 conflicts with WiFi on the ESP32. GPIO 0/1/3/6-11/12/15 are reserved (boot/flash/UART/strapping).

**LED status patterns:**
- **Solid** — Connected to daemon, camera ready
- **Fast blink** — Connected to daemon, waiting for camera
- **Slow blink** — WiFi connecting
- **Off** — No connection

### ESP32-S3-WROOM (PCB)

| Function | GPIO | Notes |
|----------|------|-------|
| Joystick X (pan) | 4 | ADC1 |
| Joystick Y (tilt) | 5 | ADC1 |
| Joystick button (home) | 13 | Internal pull-up, active low |
| Zoom potentiometer | 6 | ADC1 |
| I2C SDA (OLED + fuel gauge) | 8 | S3 default I2C SDA |
| I2C SCL (OLED + fuel gauge) | 9 | S3 default I2C SCL |
| Calibrate button | 14 | Internal pull-up, active low |
| Spare button | 15 | Internal pull-up, active low |
| Status LED (connectivity) | 16 | External LED |
| Status LED (TBD) | 17 | External LED |
| Status LED (TBD) | 18 | External LED |
| USB D- | 19 | Native USB (reserved) |
| USB D+ | 20 | Native USB (reserved) |

> **Note:** Use ADC1 pins (GPIO 1-10) for analog inputs — ADC2 conflicts with WiFi on the S3. GPIO 19/20 are reserved for native USB.

## Project Structure

```
├── platformio.ini                   # ESP32 build config
├── src/
│   ├── main.cpp                     # Setup and main loop
│   ├── config.h                     # Pin definitions, constants
│   ├── joystick.h/cpp               # Analog input, deadzone, calibration
│   ├── wifi_manager.h/cpp           # WiFi connection handling
│   ├── websocket_client.h/cpp       # WebSocket + mDNS discovery
│   ├── commands.h/cpp               # JSON command encoding
│   ├── serial_cli.h/cpp             # Serial CLI
│   ├── status_led.h/cpp             # LED status patterns
│   ├── zoom.h/cpp                   # Zoom potentiometer input
│   └── captive_portal.h/cpp         # AP mode WiFi config portal
├── host/
│   ├── linkctl/
│   │   ├── CMakeLists.txt           # CLI build config
│   │   ├── linkctl.c                # CLI client implementation
│   │   └── README.md                # CLI usage documentation
│   └── linkctl-daemon/
│       ├── CMakeLists.txt           # Daemon build config
│       ├── main.c                   # Entry point and event loop
│       ├── camera.c/h               # IOKit USB camera control
│       ├── server.c/h               # WebSocket server
│       ├── control.c/h              # TCP control socket (for CLI)
│       ├── mdns.c/h                 # mDNS advertisement
│       ├── install.sh               # Installation script
│       ├── service.sh               # launchd service management
│       └── test_daemon.py           # Integration test suite
└── hardware/
    ├── insta360-joystick.kicad_pro  # KiCad project
    ├── insta360-joystick.kicad_sch  # Schematic
    ├── insta360-joystick.kicad_pcb  # PCB layout
    ├── specs.md                     # Hardware specs and pin assignments
    ├── Components/                  # KiCad symbols and footprints
    └── Datasheets/                  # Component datasheets
```

-----

**GenAI Disclaimer:** I'm a hardware guy-- I *tolerate* writing code, but I don't love it. So while the hardware design is truly my own work product, Claude Code has been used to support firmware/software development. All code is still reviewed by me before release.

**Commit Message Disclaimer:** I apologize if my commit messages are occasionally less than informative-- this is a weekend project for me, and sometimes I'm a silly goose.

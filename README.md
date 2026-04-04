# Insta360 Link Joystick Controller

[![GitHub Release](https://img.shields.io/github/v/release/jfwoods/insta360link-joystick-controller)](https://github.com/jfwoods/insta360link-joystick-controller/releases/latest)
[![CI](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/ci.yml/badge.svg)](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/ci.yml)
[![Release](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/release.yml/badge.svg)](https://github.com/jfwoods/insta360link-joystick-controller/actions/workflows/release.yml)

Wireless pan/tilt/zoom control for the Insta360 Link webcam using an ESP32 and analog joystick. Controls the camera directly via USB/UVC extension units through a lightweight Mac daemon — no Insta360 desktop app required. This repo contains the ESP32 firmware, Mac daemon source code, and KiCad PCB design files for a custom handheld controller.

```
[Insta360 Link] ──USB/UVC──> [linkctl-daemon (Mac)] ──WiFi/WebSocket──> [ESP32 + Joystick]
```

## Hardware

- **ESP32** development board (any variant with ADC1 pins)
- **Analog joystick** (2-axis, e.g. KY-023 or PS2-style thumbstick)
- **Insta360 Link** webcam connected via USB to a Mac

### Wiring

| Joystick Pin | ESP32 Pin | Notes |
|-------------|-----------|-------|
| VRx | GPIO 34 | X-axis (pan) |
| VRy | GPIO 35 | Y-axis (tilt) |
| +5V | 3.3V | |
| GND | GND | |

The built-in LED on GPIO 2 shows connection status:
- **Solid** — Connected to daemon, camera ready
- **Fast blink** — Connected to daemon, waiting for camera
- **Slow blink** — WiFi connecting
- **Off** — No connection

> **Note:** Use ADC1 pins (GPIO 32-39) for the joystick. ADC2 pins conflict with WiFi on the ESP32.

## Setup

### 1. Build and flash the ESP32 firmware

Requires [PlatformIO](https://platformio.org/).

```bash
pio run --target upload
```

### 2. Configure WiFi on the ESP32

On first boot, the ESP32 starts a captive portal:

1. Connect to the **Insta360-Controller** WiFi network from your phone or laptop
2. A setup page opens automatically — enter your WiFi network name and password
3. The ESP32 saves the credentials and reboots onto your network

Alternatively, configure via the serial CLI (115200 baud):

```
wifi MyNetwork MyPassword
connect
```

### 3. Build and run the Mac daemon

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

The daemon finds the Insta360 Link over USB, starts a WebSocket server on port 9000, and advertises itself via mDNS. The ESP32 discovers the daemon automatically — no IP configuration needed.

### 4. Use it

Move the joystick to pan and tilt the camera. The ESP32 reads the analog input, converts it to direction and speed, and sends commands to the daemon over WebSocket.

## Daemon Options

```
./linkctl-daemon                  # Default: port 9000, mDNS enabled
./linkctl-daemon --port 8080      # Custom port
./linkctl-daemon --no-mdns        # Disable mDNS advertisement
./linkctl-daemon --test           # Test camera control and exit
```

## Serial CLI

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

JSON over WebSocket. The ESP32 sends commands, the daemon responds with status.

**Commands (ESP32 → Daemon):**

```json
{"cmd": "pan_tilt", "pan_dir": 1, "pan_speed": 15, "tilt_dir": 255, "tilt_speed": 10}
{"cmd": "zoom", "value": 250}
{"cmd": "stop"}
{"cmd": "center"}
{"cmd": "status"}
```

**Responses (Daemon → ESP32):**

```json
{"type": "status", "camera_connected": true, "zoom": 150}
{"type": "error", "message": "camera not found"}
```

Direction encoding: `1` = positive (right/up), `255` = negative (left/down), `0` = stop. Speed ranges: pan 0–30, tilt 0–20, zoom 100–400.

## Testing

The daemon includes an integration test suite that exercises WebSocket communication, camera control, error handling, and multi-client support. Requires the Insta360 Link to be connected.

```bash
# Run all tests (camera will move)
python3 host/linkctl-daemon/test_daemon.py

# Skip tests that move the camera
python3 host/linkctl-daemon/test_daemon.py --skip-motion
```

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
│   └── captive_portal.h/cpp         # AP mode WiFi config portal
├── host/
│   └── linkctl-daemon/
│       ├── CMakeLists.txt           # Daemon build config
│       ├── main.c                   # Entry point and event loop
│       ├── camera.c/h               # IOKit USB camera control
│       ├── server.c/h               # WebSocket server
│       ├── mdns.c/h                 # mDNS advertisement
│       └── test_daemon.py           # Integration test suite
└── hardware/
    ├── insta360-joystick.kicad_pro  # KiCad project
    ├── insta360-joystick.kicad_sch  # Schematic
    ├── insta360-joystick.kicad_pcb  # PCB layout
    ├── specs.md                     # Hardware specs and pin assignments
    ├── Components/                  # KiCad symbols and footprints (for parts not included in the default KiCad libraries)
    └── Datasheets/                  # Component datasheets
```

## How It Works

The Insta360 Link exposes pan/tilt/zoom controls as UVC extension unit registers accessible over USB. The `linkctl-daemon` writes to these registers directly using macOS IOKit, bypassing the Insta360 desktop app entirely. The daemon runs a WebSocket server that accepts JSON commands and translates them into the appropriate UVC control transfers. The ESP32 reads the joystick, discovers the daemon via mDNS, and streams movement commands over WiFi.

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based joystick controller for Insta360 Link webcam. Controls pan/tilt/zoom via joystick input over WiFi. The camera is controlled directly via USB/UVC extension units through a lightweight Mac daemon — no Insta360 desktop app required, no auth tokens needed.

## Architecture

```
[Insta360 Link] --USB/UVC--> [linkctl-daemon (C, libuvc)] --WiFi/WebSocket--> [ESP32 + Joystick]
                               mDNS: _insta360ctrl._tcp
                               Binds all interfaces
                               No auth required
```

- **linkctl-daemon**: Mac-side C program that controls the camera directly via UVC extension units
- **ESP32 firmware**: Reads joystick, discovers daemon via mDNS, sends JSON commands over WebSocket

## Hardware

- **Controller**: ESP32 development board
- **Input**: Analog joystick (2-axis for pan/tilt)
- **Target**: Insta360 Link webcam (VID: 0x2e1a, PID: 0x4c01)

**Important**: Use ADC1 pins (GPIO32-39) for joystick when WiFi is active. ADC2 pins conflict with WiFi.

## Toolchain

- **IDE**: VSCode with PlatformIO extension
- **Framework**: Arduino (ESP32), CMake (Mac daemon)
- **Build system**: PlatformIO for ESP32, CMake for daemon

## Build Commands

### ESP32 Firmware

```bash
pio run                              # Build firmware
pio run --target upload              # Upload to ESP32
pio device monitor                   # Monitor serial output
pio run --target upload && pio device monitor  # Build, upload, monitor
```

### Mac Daemon

```bash
# Install dependencies
brew install libuvc libwebsockets cjson

# Build
cd host/linkctl-daemon
mkdir -p build && cd build
PKG_CONFIG_PATH="/opt/homebrew/lib/pkgconfig:$PKG_CONFIG_PATH" cmake ..
make

# Run
./linkctl-daemon              # Start daemon (port 9000, mDNS enabled)
./linkctl-daemon --test       # Test camera control and exit
./linkctl-daemon --port 8080  # Custom port
./linkctl-daemon --no-mdns    # Disable mDNS advertisement
```

## Protocol

JSON over WebSocket (text frames). ESP32 sends commands, daemon responds with status.

### Commands (ESP32 → Daemon)

```json
{"cmd": "pan_tilt", "pan_dir": 1, "pan_speed": 15, "tilt_dir": 255, "tilt_speed": 10}
{"cmd": "zoom", "value": 250}
{"cmd": "stop"}
{"cmd": "center"}
{"cmd": "status"}
```

### Responses (Daemon → ESP32)

```json
{"type": "status", "camera_connected": true, "zoom": 150}
{"type": "error", "message": "camera not found"}
```

### Direction encoding: 1 = positive, 255 = negative, 0 = stop
### Speed ranges: pan 0-30, tilt 0-20. Zoom 100-400.

## UVC Control Mapping

| Command | Unit | Selector | Data |
|---------|------|----------|------|
| pan_tilt | 0x09 (XU) | 0x16 | 4 bytes: [pan_dir, pan_speed, tilt_dir, tilt_speed] |
| stop | 0x09 (XU) | 0x16 | [0x00, 0x01, 0x00, 0x01] |
| center | 0x09 (XU) | 0x1A | 8 bytes, all zeros |
| zoom | 0x01 (CT) | 0x0B | 2 bytes, little-endian |

## Key Libraries

**ESP32:**
- `WiFi.h` - ESP32 WiFi (built-in)
- `ESPmDNS.h` - mDNS service discovery (built-in)
- `WebSocketsClient` - Arduino WebSocket client (links2004/arduinoWebSockets)
- `ArduinoJson` - JSON encoding/parsing (bblanchon/ArduinoJson)

**Mac daemon:**
- `libuvc` - Direct UVC device control
- `libwebsockets` - WebSocket server
- `cJSON` - JSON parsing
- `dns_sd.h` - mDNS advertisement (built into macOS)

## File Structure

```
├── platformio.ini               # ESP32 build config
├── src/
│   ├── main.cpp                 # Setup and main loop
│   ├── config.h                 # Pin definitions, constants
│   ├── joystick.h/cpp           # Analog input, deadzone, calibration
│   ├── wifi_manager.h/cpp       # WiFi connection handling
│   ├── websocket_client.h/cpp   # WebSocket client + JSON messaging + mDNS discovery
│   ├── commands.h/cpp           # JSON command encoding
│   ├── serial_cli.h/cpp         # Serial CLI for configuration
│   ├── status_led.h/cpp         # LED status patterns
│   └── captive_portal.h/cpp     # AP mode web config
├── host/
│   └── linkctl-daemon/
│       ├── CMakeLists.txt       # Daemon build config
│       ├── main.c               # Daemon entry point and event loop
│       ├── camera.c/h           # libuvc camera control
│       ├── server.c/h           # WebSocket server + JSON command dispatch
│       └── mdns.c/h             # mDNS service advertisement
└── docs/
    └── 2026-03-29-direct-uvc-control-design.md  # Design document
```

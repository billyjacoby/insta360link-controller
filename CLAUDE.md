# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP32-based joystick controller for Insta360 Link webcam. Controls pan/tilt axes via joystick input, with zoom control TBD. Supports two operation modes:
- **Tethered**: Camera connected to macOS host - ESP32 connects via WiFi to WebSocket server on Mac
- **Wireless**: TBD - may require direct camera communication or alternative approach

## Hardware

- **Controller**: ESP32 development board
- **Input**: Analog joystick (2-axis for pan/tilt)
- **Target**: Insta360 Link webcam (PTZ USB webcam with UVC extensions)

**Important**: Use ADC1 pins (GPIO32-39) for joystick when WiFi is active. ADC2 pins conflict with WiFi.

## Toolchain

- **IDE**: VSCode with PlatformIO extension
- **Framework**: Arduino
- **Build system**: PlatformIO (`platformio.ini` configuration)

## Build Commands

```bash
pio run                              # Build firmware
pio run --target upload              # Upload to ESP32
pio device monitor                   # Monitor serial output
pio run --target upload && pio device monitor  # Build, upload, monitor
pio run --target clean               # Clean build artifacts
```

## Insta360 Link Control Protocol

Reference implementation: https://github.com/creatorsgarten/insta360-link-controller

### Architecture

```
[Insta360 Link] --USB--> [Mac + Insta360 Link App] --WebSocket--> [ESP32 Controller]
                              localhost:PORT
```

The Insta360 Link desktop app exposes a WebSocket server on localhost. Connection requires port and token from QR code displayed in the app.

### WebSocket Connection

- URL: `ws://HOST:PORT/?token=TOKEN`
- Port/token obtained from QR code in Insta360 Link desktop app
- Binary messages using Protocol Buffers

### Message Types (Protobuf)

**Sending (Request)**:
- `ControlRequest` - Required on connect with token
- `HeartbeatRequest` - Send every 1 second
- `UVCRequest` - Simple camera params (zoom)
- `UVCExtendRequest` - Extended params (pan/tilt)

**Receiving (Response)**:
- `DeviceInfoNotification` - Device list and current state
- `ConnectionNotification` - Connection status
- `HeartbeatResponse` - Heartbeat acknowledgment

### Pan/Tilt Control

```cpp
// UVCExtendRequest for pan/tilt
paramType: PARAM_PAN_TILT_RELATIVE (12)
selector: XU_PANTILT_RELATIVE_CONTROL (22)
data: [pan_dir, pan_speed, tilt_dir, tilt_speed]

// Direction byte: 1 = positive, 255 = negative, 0 = stop
// Speed: 0-30 for pan, 0-20 for tilt
```

### Zoom Control

```cpp
// UVCRequest for zoom
paramType: PARAM_ZOOM (1)
value: 100-400  // 100 = 1x, 400 = 4x
```

## Implementation Plan

### Phase 1: Basic Joystick + WiFi

1. **PlatformIO project setup** with ESP32 + Arduino framework
2. **Joystick module**: Read 2-axis analog input, apply deadzone and calibration
3. **WiFi module**: Connect to configured network
4. **WebSocket client**: Connect to Mac, handle binary protobuf messages
5. **Protobuf encoding**: Manually encode Request messages (avoid full protobuf library overhead)
6. **Main loop**: Read joystick → map to pan/tilt speed → send WebSocket command

### Phase 2: Configuration

1. **WiFi credentials**: Store in EEPROM/NVS or use WiFiManager captive portal
2. **Server config**: Store WebSocket host, port, token
3. **Serial CLI**: Configure via USB serial for development

### Phase 3: Enhancements

1. **Zoom control**: Add buttons or second axis for zoom
2. **Preset positions**: Trigger saved camera positions
3. **LED feedback**: Connection status indication
4. **BLE config**: Configure WiFi/server via phone app (optional)

## Key Libraries

- `WiFi.h` - ESP32 WiFi (built-in)
- `WebSocketsClient` - Arduino WebSocket client (links2004/arduinoWebSockets)
- `pb_encode.h` / `pb_decode.h` - nanopb for lightweight protobuf (optional)

## File Structure

```
├── platformio.ini
├── src/
│   ├── main.cpp              # Setup and main loop
│   ├── config.h              # Pin definitions, constants
│   ├── joystick.h/cpp        # Analog input, deadzone, calibration
│   ├── wifi_manager.h/cpp    # WiFi connection handling
│   ├── websocket_client.h/cpp # WebSocket + protobuf messaging
│   └── protocol.h/cpp        # Insta360 protocol message encoding
├── include/
│   └── insta360.pb.h         # Protobuf definitions (if using nanopb)
└── lib/
```

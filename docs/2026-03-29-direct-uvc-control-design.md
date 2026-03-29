# Direct UVC Control — Design & Implementation Plan

Bypass the Insta360 desktop app entirely. A lightweight Mac daemon controls the camera directly via USB/UVC extension units, with the ESP32 joystick connecting over WiFi.

## Background

The current system requires the Insta360 Link desktop app running on the Mac, which exposes a WebSocket server that demands a QR-code-derived auth token. This design eliminates that dependency by talking to the camera's UVC extension units directly — which require no authentication at the USB level.

Evidence: the `qqice/insta360_link_uvc_ctrl` project, `Daniel15/WebCamControl`, and standard `v4l2-ctl` all control the Insta360 Link without any auth handshake.

## Architecture

```
[Insta360 Link] --USB/UVC--> [linkctl-daemon (C, libuvc)] --WiFi/WebSocket--> [ESP32 + Joystick]
                               mDNS: _insta360ctrl._tcp
                               Binds all interfaces (incl. Tailscale)
                               No auth required
```

### Mac Daemon (`linkctl-daemon`)

Single C program linking `libuvc` and `libwebsockets`. On startup:

1. Find camera by VID/PID (`0x2e1a:0x4c01`)
2. Open UVC device handle
3. Start WebSocket server (default port 9000)
4. Advertise via mDNS as `_insta360ctrl._tcp`
5. Enter event loop: libwebsockets drives the main loop via `lws_service()`; camera rediscovery polling runs on a timed callback within the same loop (no threads needed)

### ESP32 Firmware

Minimal changes — replace the protocol/auth layer, add mDNS discovery:

- Replace protobuf encoder with JSON command encoder
- Remove auth flow and application-level heartbeat
- Add mDNS discovery via `ESPmDNS` (host/port become optional manual overrides)
- Keep joystick, WiFi, serial CLI, captive portal, LED status as-is

## Protocol

JSON over WebSocket. All messages have a `"cmd"` or `"type"` field.

### Client to Daemon (ESP32 to Mac)

```json
{"cmd": "pan_tilt", "pan_dir": 1, "pan_speed": 15, "tilt_dir": 255, "tilt_speed": 10}
{"cmd": "zoom", "value": 250}
{"cmd": "stop"}
{"cmd": "center"}
{"cmd": "status"}
```

### Daemon to Client (Mac to ESP32)

```json
{"type": "status", "camera_connected": true, "zoom": 150}
{"type": "error", "message": "camera not found"}
```

### Direction Encoding

Matches UVC wire format directly:
- `1` = positive (pan right, tilt up)
- `255` = negative (pan left, tilt down)
- `0` = stop

Speed ranges: pan 0-30, tilt 0-20. Zoom 100-400.

### Keepalive

WebSocket ping/pong frames (protocol-level, no application heartbeat needed).

## UVC Control Mapping

| Command | UVC Unit | Selector | Data | Notes |
|---------|----------|----------|------|-------|
| `pan_tilt` | 0x09 (XU) | 0x16 (22) | `[pan_dir, pan_speed, tilt_dir, tilt_speed]` | 4 bytes |
| `stop` | 0x09 (XU) | 0x16 (22) | `[0x00, 0x01, 0x00, 0x01]` | Per qqice project |
| `center` | 0x09 (XU) | 0x1A (26) | 8 bytes, all zeros | Gimbal reset |
| `zoom` | 0x01 (CT) | 0x0B (11) | 2 bytes, little-endian | Range 100-400 |
| `status` | -- | -- | -- | Returns connection state + zoom via `GET_CUR` |

### Camera Discovery

On startup and every 5s if disconnected: `uvc_find_device(ctx, &dev, 0x2e1a, 0x4c01, NULL)`. On success, open and hold the handle.

### Error Handling

If `uvc_set_ctrl()` fails (camera disconnected), the daemon closes the handle, sets `camera_connected = false`, notifies clients, and begins rediscovery polling.

## Component Breakdown

### New: Mac Daemon (`host/linkctl-daemon/`)

| File | Purpose |
|------|---------|
| `main.c` | Arg parsing, startup orchestration, main event loop |
| `camera.c/h` | libuvc device discovery, open/close, control transfers |
| `server.c/h` | libwebsockets WebSocket server, JSON parsing, client management |
| `mdns.c/h` | mDNS service advertisement (dns_sd.h, built into macOS) |
| `CMakeLists.txt` | Build config, link libuvc + libwebsockets |

### Modified: ESP32 Firmware (`src/`)

| File | Change |
|------|--------|
| `protocol.h/cpp` | **Replaced** by `commands.h/cpp` — JSON command encoding |
| `websocket_client.h/cpp` | **Simplified** — remove protobuf, auth, heartbeat; add mDNS discovery, JSON status parsing |
| `config.h` | Remove `WS_TOKEN`; add mDNS service name; host/port become optional overrides |
| `serial_cli.cpp` | Remove `token` command; host/port become optional manual override |
| `captive_portal.cpp` | Remove token field from web config form |

### Unchanged: ESP32 Firmware

- `joystick.h/cpp`
- `wifi_manager.h/cpp`
- `status_led.h/cpp`
- `main.cpp` (minor call-site updates only)

### Dependencies

**Mac daemon:**
- `libuvc` (Homebrew)
- `libwebsockets` (Homebrew)
- `cJSON` (single-file C JSON parser, MIT license — bundled or via Homebrew)
- `dns_sd.h` (built into macOS)

**ESP32:**
- Add `ESPmDNS` (built into ESP32 Arduino core)
- Add `ArduinoJson` (for JSON encoding/parsing)
- Remove protobuf encoding (no external lib was used — just delete)

---

## Implementation Plan

### Phase 1: Mac Daemon — Camera Control

Build the daemon's UVC layer and verify it can move the camera.

- [ ] Create `host/linkctl-daemon/` directory structure and `CMakeLists.txt`
- [ ] Implement `camera.c/h` — `camera_init()`, `camera_find()`, `camera_open()`, `camera_close()`
- [ ] Implement `camera_pan_tilt(dir, speed)`, `camera_stop()`, `camera_center()`, `camera_zoom(value)`
- [ ] Implement `camera_get_zoom()` for status queries
- [ ] Write a minimal `main.c` that finds the camera and sends a test pan command
- [ ] Verify the camera moves without the Insta360 desktop app running
- [ ] Handle camera disconnect/reconnect gracefully

### Phase 2: Mac Daemon — WebSocket Server

Add the network layer so clients can send commands.

- [ ] Implement `server.c/h` — libwebsockets setup, accept connections, manage client list
- [ ] Implement JSON command parsing (parse `cmd` field, dispatch to camera functions)
- [ ] Implement status response encoding (`camera_connected`, `zoom`)
- [ ] Implement error response encoding
- [ ] Wire server into `main.c` event loop alongside camera polling
- [ ] Test with a WebSocket client tool (e.g., `websocat`) sending JSON commands

### Phase 3: Mac Daemon — mDNS

Advertise the service so ESP32 can find it automatically.

- [ ] Implement `mdns.c/h` — register `_insta360ctrl._tcp` service via `dns_sd.h`
- [ ] Include port number in mDNS TXT record
- [ ] Verify discovery with `dns-sd -B _insta360ctrl._tcp` from another terminal
- [ ] Add daemon CLI flags: `--port`, `--no-mdns`

### Phase 4: ESP32 — Protocol Replacement

Replace the protobuf/auth layer with JSON commands and mDNS discovery.

- [ ] Create `commands.h/cpp` — `encodeCommand()` functions producing JSON strings for each command type
- [ ] Simplify `websocket_client.h/cpp`:
  - [ ] Remove `sendControlRequest()`, `sendHeartbeat()`, `handleMessage()` protobuf parsing
  - [ ] Remove `AUTHENTICATED` state — connection is ready immediately on WebSocket connect
  - [ ] Add JSON status response parsing (`camera_connected`, `zoom`, `error`)
  - [ ] Add mDNS discovery: resolve `_insta360ctrl._tcp` to get host/port, fall back to manual config
- [ ] Update `config.h` — remove `WS_TOKEN`, add `MDNS_SERVICE_NAME` constant
- [ ] Update `serial_cli.cpp` — remove `token` command, make `host`/`port` optional override
- [ ] Update `captive_portal.cpp` — remove token field from web config form
- [ ] Update `main.cpp` — use new `commands.h` API at call sites

### Phase 5: Integration Testing

End-to-end verification.

- [ ] Start daemon, connect ESP32, verify pan/tilt via joystick
- [ ] Verify zoom command (if wired up)
- [ ] Verify `stop` and `center` commands
- [ ] Test camera disconnect mid-session — daemon recovers, ESP32 gets status update
- [ ] Test daemon restart — ESP32 reconnects via mDNS
- [ ] Test manual host/port override (mDNS bypass)
- [ ] Verify daemon reachable over Tailscale interface

### Phase 6: Cleanup

- [ ] Delete old `protocol.h/cpp`
- [ ] Update `CLAUDE.md` with new architecture and build instructions
- [ ] Update `platformio.ini` if dependency changes are needed

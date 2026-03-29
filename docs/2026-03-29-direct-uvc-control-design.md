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

- [x] Create `host/linkctl-daemon/` directory structure and `CMakeLists.txt`
  - [x] CMake config finding libuvc via cmake config, libwebsockets/cjson via pkg-config
  - [x] Handle Homebrew vs MacPorts pkg-config path conflict
  - [x] Link dns_sd framework on macOS
- [x] Implement `camera.c/h`
  - [x] `camera_init()` — initialize libuvc context
  - [x] `camera_open()` — find device by VID/PID (0x2e1a:0x4c01) and open handle
  - [x] `camera_close()` — close handle and unref device
  - [x] `camera_destroy()` — shut down libuvc context
  - [x] `camera_is_connected()` — check if handle is open
- [x] Implement camera control commands
  - [x] `camera_pan_tilt(pan_dir, pan_speed, tilt_dir, tilt_speed)` — UVC SET_CUR on unit 0x09, selector 0x16
  - [x] `camera_stop()` — send [0x00, 0x01, 0x00, 0x01] to halt movement
  - [x] `camera_center()` — send 8 zero bytes to unit 0x09, selector 0x1A
  - [x] `camera_zoom(value)` — 2-byte LE SET_CUR on unit 0x01, selector 0x0B
  - [x] `camera_get_zoom()` — GET_CUR for current zoom level
  - [x] `camera_get_status()` — combined connection + zoom query
- [x] Write `main.c` with `--test` mode (find camera, test pan left/right, stop)
- [ ] Verify the camera moves without the Insta360 desktop app running (requires hardware)
- [x] Handle camera disconnect/reconnect — control functions close handle on error, main loop polls for reconnection every 5s

### Phase 2: Mac Daemon — WebSocket Server

Add the network layer so clients can send commands.

- [x] Implement `server.c/h` — libwebsockets setup
  - [x] Protocol registration with per-session data struct
  - [x] Client linked-list tracking (add on ESTABLISHED, remove on CLOSED)
  - [x] LWS_PRE-padded message sending
  - [x] Pending message queue for writeable callbacks
- [x] Implement JSON command parsing (cJSON)
  - [x] Parse `cmd` field, dispatch to camera_pan_tilt/zoom/stop/center/status
  - [x] Extract pan_dir, pan_speed, tilt_dir, tilt_speed from pan_tilt commands
  - [x] Extract value from zoom commands
- [x] Implement response encoding
  - [x] `server_broadcast_status(camera_connected, zoom)` — JSON status to all clients
  - [x] `server_broadcast_error(message)` — JSON error to all clients
  - [x] Send initial status on client connect
- [x] Wire into `main.c` event loop — `server_service(50)` in main while loop
- [ ] Test with `websocat` sending JSON commands (requires hardware)

### Phase 3: Mac Daemon — mDNS

Advertise the service so ESP32 can find it automatically.

- [x] Implement `mdns.c/h` — `DNSServiceRegister()` for `_insta360ctrl._tcp`
  - [x] Register callback logging success/failure
  - [x] Process initial registration result
  - [x] `mdns_unregister()` via `DNSServiceRefDeallocate()`
- [x] Add daemon CLI flags: `--port`, `--no-mdns`, `--test`, `--help`
- [ ] Verify discovery with `dns-sd -B _insta360ctrl._tcp` (requires running daemon)

### Phase 4: ESP32 — Protocol Replacement

Replace the protobuf/auth layer with JSON commands and mDNS discovery.

- [x] Create `commands.h/cpp` — JSON command encoding via ArduinoJson
  - [x] `cmdPanTilt(panDir, panSpeed, tiltDir, tiltSpeed)` → JSON string
  - [x] `cmdZoom(value)` → JSON string
  - [x] `cmdStop()`, `cmdCenter()`, `cmdStatus()` → static JSON strings
- [x] Rewrite `websocket_client.h/cpp` as `LinkCtlClient`
  - [x] Remove `sendControlRequest()`, `sendHeartbeat()`, protobuf handleMessage
  - [x] Remove `AUTHENTICATED` state — `CONNECTED` is ready immediately
  - [x] Add `handleMessage()` JSON parsing via ArduinoJson (status, error)
  - [x] Add `connectViaMdns()` — `MDNS.queryService()` to resolve host/port
  - [x] Add `sendStop()`, `sendCenter()`, `requestStatus()` methods
  - [x] Track `_cameraConnected` and `_cameraZoom` from status messages
  - [x] Switch from `sendBIN` to `sendTXT` for JSON
- [x] Update `config.h`
  - [x] Remove `HEARTBEAT_INTERVAL_MS`
  - [x] Remove `NVS_KEY_WS_TOKEN`
  - [x] Add `MDNS_SERVICE_NAME` and `MDNS_RETRY_INTERVAL_MS`
- [x] Update `serial_cli.h/cpp`
  - [x] Remove `_wsToken` field and `getWsToken()` accessor
  - [x] Remove `token` command and `qr` command (parseQrUrl)
  - [x] Change `setServerConfig()` to 2-arg (host, port) — no token
  - [x] Change `hasServerConfig()` to not require token
  - [x] Add `stop` and `center` serial commands
  - [x] Update help text and status display for mDNS
- [x] Update `captive_portal.h/cpp`
  - [x] Remove `_wsToken` field and accessor
  - [x] Remove QR URL parsing from `handleSave()`
  - [x] Remove token field from HTML form
  - [x] Update form to show host/port as optional (mDNS is default)
- [x] Update `main.cpp`
  - [x] Add mDNS initialization on WiFi connect (`MDNS.begin()`)
  - [x] Try manual config first, fall back to `connectViaMdns()`
  - [x] Add mDNS retry logic (`MDNS_RETRY_INTERVAL_MS`)
  - [x] Remove token from `setServerConfig()` calls
  - [x] Update LED patterns: SOLID = connected + camera, BLINK_FAST = connected
- [x] Add `ArduinoJson@^7.0.0` to `platformio.ini` lib_deps
- [x] Verify firmware compiles

### Phase 5: Integration Testing

End-to-end verification (requires hardware).

- [ ] Start daemon, connect ESP32, verify pan/tilt via joystick
- [ ] Verify zoom command (if wired up)
- [ ] Verify `stop` and `center` commands via serial CLI
- [ ] Test camera disconnect mid-session — daemon recovers, ESP32 gets status update
- [ ] Test daemon restart — ESP32 reconnects via mDNS
- [ ] Test manual host/port override (mDNS bypass)
- [ ] Verify daemon reachable over Tailscale interface

### Phase 6: Cleanup

- [x] Delete old `protocol.h/cpp`
- [x] Update `CLAUDE.md` with new architecture and build instructions
- [x] Update `platformio.ini` with ArduinoJson dependency

# Native macOS controller plan

## Goal

Use an Insta360 Link 2 Pro as a webcam and control its supported hardware features
without the Insta360 desktop application. Keep video capture local and reuse the
existing C daemon rather than introducing another USB implementation in the GUI.

## What this repository provides

- A macOS IOKit daemon, WebSocket JSON API on port 9000 (`arduino` subprotocol),
  loopback TCP CLI API on port 9001, and optional ESP32 controller.
- Original Link controls: relative pan/tilt, center, stop, and 1–4× zoom.
- No existing preview, image controls, tracking implementation, or absolute-position
  API. The center command itself does not reset zoom despite the old CLI table.
- The original camera match is `2e1a:4c01`. The connected Link 2 Pro reports
  `2e1a:4c06`, with UVC VideoControl on interface 0. A different product ID alone
  is not proof that vendor-specific commands are compatible.

### Hardware evidence collected

The read-only [probe](../host/linkctl-probe/README.md) successfully inspected the
connected Link 2 Pro. XU9 pan/tilt selector `0x16` advertises GET/SET and length 4;
center selector `0x1a` advertises GET/SET and length 8. CT1 zoom selector `0x0b`
returns current zoom 100. See the [full findings](../host/linkctl-probe/FINDINGS.md).
These match the existing payload layout, but physical motor semantics still need
verification. The daemon's explicit `--link2-pro` profile keeps this distinction
visible; `--link2-pro --status` is a read-only connection check.

The runtime profile also read zoom minimum 100, maximum 400, and resolution 1.
A subsequent authorized write test returned USB success for zoom/pan/stop, but
zoom readback stayed at 100 and the user observed no motion while the camera was
in standby. The first check therefore did not establish functional control.

The user then confirmed live video in the new native app. With that preview
active, zoom changed 100 → 110 → 100 with matching readback, and the user confirmed
a 200 ms rightward pan at speed 3 followed by stop. This verifies these commands
through the camera module. The user then launched the daemon from the GUI and
successfully tested the GUI right-jog and Apply Zoom controls, verifying the full
application path. Tilt, center, reconnects, and extended sessions remain to be
tested. Start preview before Link 2 Pro controls.

## Architecture

```text
SwiftUI macOS app
  ├─ AVFoundation → selected camera's standard video stream
  ├─ URLSession WebSocket → linkctl-daemon → IOKit/UVC controls
  └─ Process → bundled camera service automatically owned by this app
```

The preview camera and daemon-controlled camera are separate selections. A preview
does not establish that the daemon supports that camera. The GUI must communicate
this distinction, particularly when several webcams are connected.

Use Swift Package Manager with no third-party GUI dependencies and a standalone `.app`
packaging script. Bundle the service and its transitive native libraries, using
relative load paths so Homebrew is not a runtime requirement. The app automatically
starts its helper on a private loopback endpoint with the TCP CLI socket disabled.
The helper detects a single supported Link or Link 2 Pro on startup/reconnect.
The app bundle carries its camera permission description. Request
camera access only when the user starts preview. Local development uses ad-hoc
signing; distribution signing/notarization is a later packaging task.

## Milestones

### 1. Desktop foundation

- Native window with camera preview and device picker.
- Separate daemon and camera connection states, actionable errors, status polling.
- Existing API controls: bounded directional jogs, speed, stop, center, zoom.
- Serialize commands; stop on focus loss and before disconnect/exit where possible.
- Launch the bundled service automatically, show diagnostics, stop only the app-owned process.
- Give users camera status and a retry action rather than service/executable setup.
- Keep standalone CLI/ESP32 daemon operation separate from the private desktop helper.
- Protocol tests and read-only daemon transport regression tests.

### 2. Link 2 Pro hardware compatibility

1. Inspect USB configuration descriptors and UVC control bitmaps without writes.
2. Query advertised control capabilities/lengths and standard zoom values.
3. Compare vendor extension unit IDs, selectors, and payload sizes with the original
   Link implementation. Treat matching lengths as evidence, not semantic proof.
4. Add a model-specific profile only for verified controls. Report unsupported or
   experimental controls explicitly rather than silently sending original-Link commands.
5. Validate zoom and short motor commands with the physical camera, then reconnect,
   unplug, app termination, and simultaneous video capture.

Acceptance: preview and each enabled control work on the Link 2 Pro with the vendor
app closed; connection state reflects unplug/replug; movement cannot remain active
because the GUI lost focus or disconnected. A daemon-side motion timeout is needed
before treating crash/disconnection handling as complete.

### 3. Daily-driver controls

- Menu-bar controls and Raycast Script Commands share the native app's managed
  camera connection. See [desktop automation](desktop-automation.md) for the URL
  action contract, lifecycle behavior, and packaging.
- Device-discovered standard UVC image controls: brightness, contrast, color,
  sharpness, white balance, focus, anti-flicker, and exposure where advertised.
  Present automatic/manual modes and device-reported ranges. Implementation and
  model-specific limits are documented in [camera settings](camera-settings.md).
- Link 2 Pro tracking, DeskView, overhead, and whiteboard auto-detection use the
  model-specific mode interface described in [AI camera controls](ai-camera-controls.md).
- Proprietary exposure/ISO, HDR, tracking speed/framing/targets, and gesture
  configuration still need verified Pro mappings; standard CT/PU discovery does
  not cover those vendor features.
- Saved image settings, launch-at-login, packaged daemon, and service diagnostics.
- Presets only after identifying absolute pan/tilt readback or a reliable positioning
  protocol. Timed relative moves are not accurate absolute presets.
- Tracking/gestures/background effects are separate investigations. Determine which
  run in camera firmware and which require host software before estimating them.

## Backend work for the GUI

The CLI and WebSocket paths previously did not share the same camera mutex. They
now share one, including reconnect polling. A failed/short zoom read invalidates the
camera handle so status does not keep reporting a disconnected device as connected.

WebSocket replies are queued and sent from writable callbacks, preserving burst
response ordering. The control thread wakes the WebSocket loop periodically so
camera reconnect polling and idle daemon shutdown can run without incoming traffic.

## Publishing

There is no project-level software license in this clone. Confirm the upstream
author's license/grant before redistributing a derived open-source release; choose
a license for original contributions without claiming ownership of upstream code.

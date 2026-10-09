# Link Control for macOS

A native camera dashboard with live preview, pan/tilt jogs, center, zoom, image
settings, and connection status. The app includes and manages its camera service automatically.

## Using the app

1. Unzip the packaged app and move **Link Control.app** to Applications.
2. Open **Link Control** and connect your Insta360 Link or Link 2 Pro.
3. Select your camera, click **Start Preview**, and allow camera access.
4. Use the camera controls once the camera is ready.

There is no separate service installation, executable picker, terminal command,
Homebrew dependency, or model-profile switch for end users. The bundled service
starts with the app, automatically detects the supported model, and stops when
the app quits. Camera permission is requested only when starting preview.

The camera menu-bar menu stays available after you close the dashboard. Closing
the window keeps an already-started preview and the camera service running; use
**Stop Preview** to stop capture or **Quit Link Control** for full shutdown.

**Keep a video stream active before controlling Link 2 Pro.** In standby, this
camera may acknowledge writes without applying them. Preview in Link Control or
another camera app wakes it.

If a connection fails, use the app's retry action. The expandable diagnostics log
provides details for troubleshooting. An incomplete app bundle must be replaced
with a complete packaged copy; the app does not ask users to locate internal files.

## Raycast and menu-bar controls

The menu-bar camera icon provides status, preview start/stop, bounded jogs, center,
manual stop, zoom presets, supported AI modes, refresh, and a dashboard shortcut.
It shares the dashboard's camera connection and shows the latest automation result.

The packaged app also includes **13 Raycast Script Commands** for these actions,
plus setting individual image controls. To connect them:

1. Install the updated app in `/Applications` and open it once.
2. Choose **Open Raycast Scripts Folder** from its menu-bar menu.
3. In **Raycast Settings → Extensions → + → Add Script Directory**, select that
   folder. Use **Command–Shift–G** and paste the folder path copied by the app.
4. Search for commands such as **Set Camera Mode**, **Set Camera Zoom**, or
   **Jog Camera Left**. Assign Raycast hotkeys or aliases as desired.

Raycast launches the app when needed. Actions go through its existing managed
helper, with startup waiting and explicit errors. The scripts report delivery
(“Sent to Link Control”); the app/menu bar reports the action result. Unsupported
settings or busy camera modes are not silently treated as successful commands.

Use **Start Camera Preview** explicitly when another app is not streaming from
the camera. Ordinary control commands do not automatically start capture.

See [Raycast setup, command list, and URL interface](../../integrations/raycast/README.md).

## Supported hardware and platforms

- Original Insta360 Link: USB `2e1a:4c01`.
- Link 2 Pro: USB `2e1a:4c06`, with descriptor and control-capability validation.
  Preview, zoom, and rightward jog/stop were verified end-to-end on real hardware.
  Tilt, center, other firmware versions, and extended sessions need further checks.
- Connect one supported control camera at a time. Preview selection is independent
  of the USB camera selected by the service. Unsupported models are not controlled.
- Build artifacts are architecture-specific. Use an Apple Silicon (`arm64`) or
  Intel (`x86_64`) package matching your Mac.
- The packaged minimum macOS version reflects every bundled executable/library,
  not just the SwiftUI source's macOS 13 deployment target. Builds produced on
  newer macOS/Homebrew versions may require that newer OS.

## Controls

- Each direction click sends a 350 ms jog followed by stop; speed is adjustable.
- Center sends the center command followed by stop after 500 ms.
- The red stop button and loss of application focus request a stop.
- Zoom ranges from 100–400%. Move the slider, then click **Apply Zoom**.
- Commands are serialized and acknowledged. Quitting waits for a final stop and
  closes the app-owned service. A disconnected or crashed client cannot guarantee
  delivery of a stop; a daemon-side motion watchdog is a remaining improvement.
- The app uses a private loopback WebSocket endpoint. It does not attach to or
  shut down an independently installed daemon, or use the CLI's TCP port 9001.

## Image settings

The image-settings panel talks directly to the camera over USB. Supported controls
are discovered from the connected device, with its actual ranges and step sizes.
Depending on the model, these include brightness, contrast, hue, saturation,
sharpness, anti-flicker frequency, white balance, and focus.

On the tested Link 2 Pro, anti-flicker is currently read-only because its reported
value does not match the options defined by its advertised UVC version. The other
settings remain available; manual focus and white balance unlock when their
automatic modes are turned off.

- Use automatic white balance/autofocus toggles or disable them to edit the
  corresponding manual setting.
- Numeric edits are drafts until applied. The app reads the camera back after
  a change instead of assuming a USB acknowledgement means the value changed.
- Refresh reads current settings; per-setting defaults are available only when
  the camera supplies them. These are not factory-reset commands.
- Settings requests and timed motion controls do not overlap.
- These affect the camera's video output, not just this app's preview. Persistence
  across power cycles depends on the camera firmware.

Some official-app features, including proprietary exposure controls, HDR,
tracking speed/framing/targets, and gesture configuration, still require
model-specific integration. Standard
exposure controls appear only when advertised by the connected camera. See
[settings support and protocol notes](../../docs/camera-settings.md).

## AI tracking and camera modes

For compatible Link 2 Pro cameras, **AI tracking & camera modes** offers:

- **Normal** — disable the special camera mode.
- **AI tracking** — follow a person visible in the live stream.
- **DeskView** — rotate the camera output for viewing a desk; use the jog controls
  to aim it at your work surface.
- **Whiteboard auto-detect** — let the camera find and correct a visible board.
- **Overhead** — point downward with rotated output.

Keep Preview or another camera stream running. Mode changes are asynchronous; the
app reads the actual state while a transition or whiteboard detection is in progress.
It passes through Normal when switching between different special modes. A stopped
stream can cause the camera to return to Normal.

Select **Normal** before manually positioning a camera that is tracking a person.
The red Stop button stops manual jog commands; disabling camera-side tracking is
a mode change. After entering DeskView, manual jogs remain available to aim at the
work surface.

The protocol is based on published **Link 2 Pro hardware tests**, with model,
descriptor, capability, and buffer-length checks on this camera. It preserves the
other bytes in the camera's 61-byte state buffer. Local verification has read that
state; visible behavior of the new modes still needs confirmation on this camera.

Speed, framing/target selection, and gesture configuration are not implemented:
there is no verified Pro command for them yet. This also does not implement the
official software's Virtual Camera or a manual whiteboard corner editor. See
[AI protocol evidence and limitations](../../docs/ai-camera-controls.md).

## Building from source (developers)

Requires Xcode/Command Line Tools with Swift 5.9+, CMake, Python 3, and build-time
Homebrew libraries. These are **not** requirements for someone using the packaged app.

```sh
brew install cmake libwebsockets cjson openssl@3 pkgconf
cd host/linkctl-gui
swift test
./scripts/package-app.sh
open "dist/Link Control.app"
```

The packaging script builds the camera service, copies its transitive non-system
libraries into the app, rewrites their load paths, verifies the dependency closure,
signs nested components and the app, and creates a ZIP. It also includes notices
for the bundled third-party libraries. See [packaging details](scripts/README.md).

Use the packaged app for normal development testing; a bare `swift run` executable
does not contain the bundled helper. Tests inject helper locations and mock services.

Current local packages are **ad-hoc signed**, not Developer ID signed/notarized.
Release automation builds architecture-specific ZIPs; publishing a frictionless
download still requires Developer ID signing and notarization credentials.

## Verification

Swift tests exercise loopback WebSocket communication, startup/lifecycle behavior,
command ordering, connection failures, bounded GUI jogs, logs, settings validation,
automatic-mode dependencies, and stale settings responses across reconnection.
Daemon tests verify private loopback-only service startup without the CLI socket,
plus settings discovery, write validation, byte encoding, and readback using a fake
USB transport.
The packaged helper is checked after relocation with a sanitized environment.

For an end-to-end check of automatic startup using the actual packaged service:

```sh
LINKCTL_TEST_BUNDLE="$PWD/dist/Link Control.app" swift test --filter PackagedAppTests
```

This test starts the bundled helper, waits for a real status reply, reads settings
when a camera is connected, and verifies shutdown. It does not edit settings, jog,
or change zoom; shutdown sends the normal stop command.

Hardware verification: the user confirmed live preview, service connection, GUI
right-jog/stop, and GUI zoom (100 → 110 → 100). USB zoom readback also matched those
values. Full findings are in [`../linkctl-probe/FINDINGS.md`](../linkctl-probe/FINDINGS.md).
The user also quit the original development app, reopened the self-contained
build, and confirmed it reached **Camera ready** automatically without service setup.

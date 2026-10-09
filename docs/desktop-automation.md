# Desktop automation: Raycast and menu bar

## Ownership

All desktop entry points target the running `org.linkctl.gui` application. That
application owns one bundled camera helper on its private loopback endpoint.
Raycast commands do not discover daemon ports, connect to the legacy TCP API,
spawn helpers, or stop independently installed camera services.

The app remains running after its dashboard closes. Its menu-bar menu provides
common camera actions, preview control, dashboard reopening, Raycast setup, and
Quit. Quitting uses the existing asynchronous preview/helper shutdown path.

## Public action URLs

Base: `linkcontrol://action/`

| Route | Parameters | Meaning |
| --- | --- | --- |
| `dashboard` | None | Show/reopen the dashboard |
| `start-preview` | None | Explicitly start the app's capture session |
| `stop-preview` | None | Stop the app's capture session |
| `center` | None | Existing bounded center operation |
| `stop` | None | Cancel pending automation and stop manual motion |
| `jog` | `direction=up/down/left/right` | One existing bounded jog |
| `zoom` | `value=100..400` | Absolute zoom percentage |
| `mode` | `value=normal/tracking/whiteboard/overhead/deskview` | Supported Pro mode |
| `setting` | `id=<image setting ID>&value=<integer>` | Device-validated image setting |
| `refresh` | None | Read current camera settings |

Only recognized routes and parameters are accepted. A URL cannot select a remote
endpoint or directly specify a daemon command, USB selector, or arbitrary payload.
Normal mode disables AI tracking; the manual Stop action is not a camera-mode reset.

The receiver waits for cold-start readiness within a bounded period. Camera
capability, automatic/manual state, mode transitions, and connection lifecycle
still govern whether a requested action can execute. Delayed actions must not be
replayed onto a new camera connection after disconnect/shutdown.

## Delivery versus execution

The shell scripts use macOS Launch Services:

```sh
open -g -b org.linkctl.gui 'linkcontrol://action/zoom?value=150'
```

Successful `open` means the action was sent to the app. It is **not** a camera
acknowledgement. Script output says “Sent to Link Control”; the app's observable
automation status reports execution/errors. This avoids presenting a successful
camera change when the camera is absent, a control is unavailable, or a mode is busy.

This version is a set of Raycast Script Commands, not a Raycast Store extension
with synchronous query/result IPC. Scripts need no additional language runtime.

## Packaging and setup

Sources live in [`integrations/raycast`](../integrations/raycast). Packaging copies
the entire directory, including `lib/link-control.sh`, to
`Link Control.app/Contents/Resources/Raycast` before signing the app.
The menu-bar action opens that directory and copies its path for Raycast's
**Add Script Directory** picker. The setup guide ships alongside the scripts.

Tests run scripts in dry-run mode and check metadata, argument validation, exact
action URLs, and relocation into paths with spaces. App tests cover parsing and
automation lifecycle using a mock helper; they do not send physical camera actions.

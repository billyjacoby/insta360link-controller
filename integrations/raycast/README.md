# Link Control for Raycast

These Script Commands send actions to the native **Link Control** app. They use
the app's own bundled camera service; no separate daemon, CLI installation,
Homebrew, Python, or Node runtime is required.

## Set up

1. Move the newly packaged **Link Control.app** into `/Applications` and open it
   once. Replace an older copy so macOS routes commands to the updated app.
2. In Link Control's camera menu-bar menu, choose **Open Raycast Scripts Folder**.
   The app reveals its bundled scripts and copies that folder's path.
3. Open **Raycast Settings → Extensions → + → Add Script Directory**.
4. In the folder picker, press **Command–Shift–G**, paste the copied path, and
   select the folder. For a normal installation, it is:

   ```text
   /Applications/Link Control.app/Contents/Resources/Raycast
   ```

Developers can instead add this repository's `integrations/raycast` directory.
Keep the `lib` subdirectory alongside the commands: it contains their shared helper.

You can assign Raycast aliases and hotkeys to individual commands. The four jog
directions have separate commands so each can have its own hotkey.

## Commands

| Command | Input |
| --- | --- |
| Open Camera Dashboard | None |
| Start Camera Preview / Stop Camera Preview | None |
| Center Camera | None |
| Stop Camera Jog | None; cancels queued automation and stops manual motion |
| Jog Camera Up / Down / Left / Right | None; bounded jog at the app's configured speed |
| Set Camera Zoom | Integer percent, **100–400** |
| Set Camera Mode | Normal, AI Tracking, DeskView, Whiteboard Auto-detect, Overhead |
| Set Camera Image Setting | Setting dropdown plus integer value |
| Refresh Camera Settings | None |

Use **Normal** to disable AI tracking before manually positioning the camera.
Stop Camera Jog stops manual motion; it does not turn off the camera's AI mode.
The app preserves the inverted up/down mapping and bounded jog duration.

The image-setting dropdown is a list of recognized setting names, not a claim
that every camera supports them. The app checks the connected camera's actual
capability, range, step, and automatic/manual state. Look in the dashboard for
valid values. For boolean settings use **0 = off, 1 = on**.

## Preview and results

Keep a camera stream running: use **Start Camera Preview**, the menu-bar preview
action, or a meeting app already using the camera. Camera control commands do not
silently start a preview. The explicit Start command can request macOS camera
permission on first use. Preview can continue while the dashboard is closed;
**Stop Camera Preview** stops only the session owned by Link Control.

Raycast can launch Link Control if it is closed. The app waits for its managed
camera service before processing camera actions, with a bounded timeout. It
displays unavailable settings, busy-camera conditions, or connection failures
instead of silently discarding commands.

The scripts report **“Sent to Link Control”**, not “Camera action succeeded.”
macOS URL delivery is asynchronous: look at the app/menu-bar automation result
for execution status. Mode changes additionally wait for actual camera readback.

Closing the dashboard leaves the menu-bar app and its owned camera service running.
Use **Quit Link Control** from the menu or **Command–Q** for full shutdown.

## URL interface

The same interface works in macOS Shortcuts, shell scripts, or other launchers:

```sh
open -g -b org.linkctl.gui 'linkcontrol://action/start-preview'
open -g -b org.linkctl.gui 'linkcontrol://action/center'
open -g -b org.linkctl.gui 'linkcontrol://action/jog?direction=up'
open -g -b org.linkctl.gui 'linkcontrol://action/zoom?value=150'
open -g -b org.linkctl.gui 'linkcontrol://action/mode?value=tracking'
open -g -b org.linkctl.gui 'linkcontrol://action/setting?id=white_balance_auto&value=0'
open -g -b org.linkctl.gui 'linkcontrol://action/setting?id=white_balance_temperature&value=5500'
open -g -b org.linkctl.gui 'linkcontrol://action/stop'
open -g -b org.linkctl.gui 'linkcontrol://action/refresh'
open -g -b org.linkctl.gui 'linkcontrol://action/stop-preview'
open -g -b org.linkctl.gui 'linkcontrol://action/dashboard'
```

Actions and parameters are allowlisted. Unknown commands, duplicate/extra query
parameters, malformed values, credentials, ports, and fragments are rejected.
There is no URL for a raw USB write, arbitrary daemon command, or remote endpoint.

## Developer verification (no hardware actions)

```sh
LINK_CONTROL_DRY_RUN=1 ./camera-zoom.sh 150
```

This validates inputs and prints the URL without opening the app. From the
repository root, run the script transport/metadata tests with:

```sh
python3 integrations/tests/test_raycast.py
```

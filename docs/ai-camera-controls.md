# AI tracking and camera modes

## Scope

Link Control sends hardware commands directly over USB. The camera's tracking
algorithm runs on the device; the app is a controller for that algorithm.

Insta360's Link 2 Pro product documentation describes single-person/group
tracking, tracking/pause areas, DeskView, gesture shortcuts, and whiteboard modes.
Feature availability in the official application is not a specification for USB
commands. The SDK overview is not a complete compatibility specification; the
published Link SDK does include macOS binaries and explicitly accepts the Pro PID
(see the gesture protocol evidence below).

The official software also offers a Smart Whiteboard Virtual Camera path. That
is distinct from the hardware whiteboard detection/perspective-correction mode
documented below. Link Control does not create a virtual camera.

Official references:

- [Link 2 Pro features](https://www.insta360.com/product/insta360-link2-pro)
- [SDK platforms and supported cameras](https://onlinemanual.insta360.com/developer/en-us/resource/sdk)
- [Official Stream Deck integration](https://marketplace.elgato.com/product/insta360-webcam-1824b564-d54e-496d-bd73-198cb12ec9c4)

## Link 2 Pro mode protocol

The model-specific source is
[tagawa0525/insta360-link2pro-linux](https://github.com/tagawa0525/insta360-link2pro-linux),
which reports frame-based hardware verification on `2e1a:4c06`, firmware
`v0.3.0.8`. Its [implementation](https://github.com/tagawa0525/insta360-link2pro-linux/blob/main/link2pro.py)
and [hardware findings](https://github.com/tagawa0525/insta360-link2pro-linux/blob/main/references/README.md)
document the following extension-unit interface:

- XU9, selector `0x02`, **61 bytes**.
- Read the entire current buffer, alter bytes 0 and 1 only, then write the full
  buffer back. Never zero-fill or pad the original Link's 52-byte command.
- Match the model, XU GUID, advertised selector, read/write capability, and exact
  length before enabling writes.

| Mode | Byte 0 | Entry byte 1 | Camera behavior |
| --- | --- | --- | --- |
| Normal | `00` | `00` | Leaves the special mode; old byte 1 values can remain stale |
| AI tracking | `01` | `00` | Follows a visible person with the gimbal |
| Whiteboard auto-detect | `04` | `00` | Detects a board; substate `02` means locked and `03` means detection failed |
| Overhead | `05` | `03` | Points downward and rotates the output |
| DeskView | `06` | `00` | Rotates output for a desk view; use the jog controls to aim downward |

Modes require an active video stream. Tracking needs a person in the frame;
whiteboard detection needs a recognizable board. The camera may return mode
`ff` during a transition; additional writes in that state can be ignored.
Switching between different special modes must pass through Normal first.
The GUI waits through transitions using bounded, asynchronous refreshes rather
than holding the command queue while the camera is detecting a subject.
An accepted USB transfer is not shown as a completed mode change: the requested
mode must appear in readback, and whiteboard detection must settle. Timeouts leave
the actual reported state visible and do not cause unbounded command retries.

DeskView's hardware mode alone is not an automatic tilt preset. The best desk
angle depends on the mounting height; no fixed tilt is imposed by the app.
Stopping the stream can return the camera to Normal within a few seconds.

The settings API exposes one mutually exclusive `ai_mode` enum in group `ai`.
Its optional `pending` Boolean indicates transition/detection in progress; `detail`
describes the reported state. Unknown states remain read-only. There is no
fabricated factory default. Normal is the way to disable the special mode.

## Features without a verified Pro command

- Tracking speed.
- Head/half/full framing: the Pro-specific source tested the commonly suggested
  XU9 selector `0x13` values and observed no framing effect despite matching readback.
- Single/group or selected-person targeting: the legacy candidate at XU10 selector
  `0x01` is read-only on the Pro; its byte 4 is a counter, not a target setting.
- Gesture bindings: the SDK documents binding commands, but Pro-specific behavior
  has not been established; binding writes are not exposed.
- Tracking/pause-area configuration.

These do not have working controls in the UI. Completing them requires Pro-specific
protocol evidence, such as differential captures of the corresponding official-app
settings followed by visible behavioral checks. Register presence, a successful
USB write, or matching readback alone does not establish the function.

Manual whiteboard corner selection is documented by the external source, but this
implementation uses the camera's automatic detection. A future corner editor must
send normalized TL/BL/BR/TR points together with the active stream aspect ratio;
hard-coding 16:9 is not correct for every supported stream.

## Gesture switch protocol

Static inspection of the official [Insta360 Link SDK](https://github.com/Insta360Develop/Link-SDK/tree/2dd2c78186b0335a530042ae49accbef249d6a95)
establishes enable/disable commands. Unlike a legacy-model guess, this SDK's macOS
discovery explicitly accepts VID `2e1a`, PID `4c06` and routes it through the same
controller. The implementation is restricted to this Pro model and the matching
XU9 GUID above, advertised selector, capability, and exact length.

| Setting ID | XU9 selector | Length | Bit |
| --- | --- | --- | --- |
| `gesture_control` | `1b` | 2 bytes, little-endian | 4 (`0010`) |
| `gesture_palm` | `05` | 1 byte | 1 (`02`) |
| `gesture_zoom` (L) | `05` | 1 byte | 2 (`04`) |
| `gesture_whiteboard` (V) | `05` | 1 byte | 3 (`08`) |

Every write reads the full current register, changes only the requested bit, and
checks readback. The master switch and individual preferences are independent;
turning the master off does not clear the individual settings. No binding or other
feature bits are changed. A switch does not exit a mode already in progress.

Reproducible static evidence at that commit:

- macOS dylib Git blob `c4b760813f2eb8ed089be5709ef1cc8ce51c71cd`, ARM64 slice:
  `EnableAllGesture` at `0xbc64` passes feature index 4 to
  `EnableExtendFuncWork` at `0xd238`, which reads/modifies/writes selector `1b` as
  a 16-bit mask. `EnableGestureWork` at `0xbc78` and `GetGestureStatus` at
  `0xbfcc` use selector `05`, with gesture enum values as bit indices.
- Linux SDK blob `9aa109f72f374fc4c0860b34053d6317b0b49130` independently uses the
  same mapping. Public headers define Palm=1, L=2, V=3 and distinguish the master
  switch from per-gesture enable state.
- Local read-only descriptors report GET/SET and lengths 2 and 1 respectively.
  Selector `05` returned `0e`, consistent with Palm/L/V individually enabled.
  The existing probe did not read selector `1b`'s current value.

A follow-up read-only `linkctl-probe --ai` on October 15, 2026 returned XU9
`05`: INFO=`03`, LEN=`1`, CUR=`0e`; and `1b`: INFO=`03`, LEN=`2`, CUR=`d4 09`.
The SDK mapping interprets these as master enabled and Palm/L/V individually
enabled. This check made no writes and does not establish behavioral correctness.
The packaged helper's read-only `--auto-camera --settings` diagnostic also returned
all four gesture controls as readable/writable Boolean `1`, with no defaults or
per-control errors. The Swift tests cover decoding and failed-write state handling;
fake-USB tests cover masked writes, capability gates, and readback failures.

This establishes the SDK wire mapping, **not physical gesture verification** on
the connected firmware. Test with video active: switch the master off, confirm
readback, and verify palm/L/V no longer trigger actions; then test the individual
switches with the master on. Check reconnect and power-cycle behavior separately.
Do not infer persistence from USB readback. Gesture bindings may have been changed
by other software; these switches do not assign actions.

## Read-only hardware evidence

The connected Link 2 Pro (`2e1a:4c06`) returned a 61-byte mode buffer beginning
`00 03`: Normal with a stale substate. The implementation must not mistake that
stale `03` for a current whiteboard failure. The trailing state bytes are nonzero,
which reinforces the need to preserve them when changing modes.

It also exposes XU11 selector `0x02`, a tracking command documented by the independent
**Link 2** CLI. That register returned `00`, but there is no Pro-specific semantic
evidence for that mapping; it is **not used for Link 2 Pro tracking**.
The transcript is [link2pro-ai-probe.txt](../host/linkctl-probe/link2pro-ai-probe.txt).
These reads establish structural compatibility and a readable initial state;
they do not establish that tracking visibly follows a person on this model.

The read-only probe's `--ai` option reads selected mode/gesture/tracking candidates
only when their metadata and expected lengths match. It does not turn tracking on,
move the camera, or alter gesture settings. The local evidence is read-only;
visible behavior of these new modes on this particular camera remains to be tested.

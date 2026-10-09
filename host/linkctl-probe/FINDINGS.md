# Connected Insta360 Link 2 Pro findings

Observed by running this probe on the connected **VID `0x2e1a`, PID `0x4c06`**
camera. Every control transfer was a device-to-host read. The build passed with
`clang -std=c11 -Wall -Wextra -Werror`; the hardware run completed with zero
failed or short requests. The device-specific transcript is in
[`link2pro-probe.txt`](link2pro-probe.txt).

## Configuration and entities

- One configuration, active value **1**, total length **842 bytes**, five
  interfaces.
- VideoControl **interface 0**, alternate setting 0, **UVC 1.10**, VC descriptor
  total length **139 bytes**, clock 48 MHz.
- Camera Terminal **1**, terminal type `0x0201`, control mask **`20 7a 02`**.
- Processing Unit **5**, source **1**, control mask **`5f 14`**.
- Extension Unit **9**, source **5**, GUID bytes
  `2d 67 f1 fa 1b b7 93 47 8c 91 7b 1c 9b 7f 95 f8`.
- Extension Unit **10**, source **9**, GUID bytes
  `49 e6 07 e3 18 46 ff a3 82 fc 2d 8b 5f 21 67 73`.
- Extension Unit **11**, source **5**, GUID bytes
  `f2 5d bd a8 98 1a 4e 47 8d d0 d9 26 72 d1 94 fa`.
- Output Terminal **3**, USB-streaming type `0x0101`, source **10**.
- All three XUs advertise `bControlSize=4`, `bmControls=ff ff ff ff`, but
  `bNumControls=11`. This is inconsistent: 32 selector bits are set, not 11.
  The probe follows the explicit bitmap, and **all 32 selectors on each of
  XU9/XU10/XU11 answered both GET_INFO and GET_LEN**. Some lengths, particularly
  XU11's repeated ten-byte responses, may be generic firmware defaults; this is
  not a semantic catalog of 96 known commands.

## Existing backend mapping

| Existing operation | Address (VC interface 0) | Actual read response | Conclusion |
| --- | --- | --- | --- |
| Pan/tilt relative | XU9 selector `0x16` | GET_INFO `03`; GET_LEN `04 00` = 4 | Advertised readable/writable; legacy payload length matches |
| Gimbal center | XU9 selector `0x1a` | GET_INFO `03`; GET_LEN `08 00` = 8 | Advertised readable/writable; legacy payload length matches |
| Absolute zoom | CT1 selector `0x0b` | GET_INFO `03`; GET_CUR `64 00` = 100 | Standard UVC zoom is advertised and current-value read works |

`GET_INFO=03` means GET and SET capability bits are set; it does not mean a write
was attempted. Zoom's descriptor bitmap bit is **9** (selectors and bitmap
positions are not interchangeable for Camera Terminals). Zoom has the UVC-defined
two-byte payload; no unsupported CT `GET_LEN` request was attempted.

There were **194 successful class reads**: 96 advertised extension selectors ×
two metadata requests, plus zoom GET_INFO and GET_CUR. No proprietary GET_CUR
payloads were read. No motion, zoom write, stop, center, reset, stream, or
configuration change was performed.

## Recommended next step

The same unit IDs, selectors, VideoControl interface, and payload lengths are
present on Link 2 Pro. This is strong **structural compatibility evidence** for
adding PID `0x4c06` alongside original Link PID `0x4c01` and using descriptor /
GET_INFO / length checks during discovery. Standard zoom read can already be
used for GUI state.

Proprietary pan/tilt direction encoding, speed limits, center payload semantics,
and zoom min/max/resolution are not established by these reads. Before declaring
full motion compatibility, confirm proprietary semantics from a protocol source
or a separately authorized controlled hardware test. For zoom range, the next
read-only extension can use standard supported `GET_MIN`, `GET_MAX`, and
`GET_RES` on CT1 selector `0x0b` instead of inheriting the old fixed range.
# Follow-up daemon profile verification

The experimental daemon profile (`--link2-pro --status`) successfully validated
the advertised controls and read zoom **100**, minimum **100**, maximum **400**,
and resolution **1** on this connected camera.

A user-authorized write check requested zoom 110 and then restored 100. USB
returned success, but readback remained 100. A separate 200 ms pan at speed 3 and
explicit stop also returned success. The user confirmed the camera was in standby
and did not move. These results **do not verify functional zoom or motor control**;
repeat with an active video stream before promoting the profile beyond experimental.

## Active-stream retest

The user started live Link 2 Pro preview in the new SwiftUI app and confirmed video
worked. With that stream active, a camera-module diagnostic harness produced:

- Zoom write 110: success; subsequent `GET_CUR`: **110**.
- Restore zoom 100: success; subsequent `GET_CUR`: **100**.
- Rightward pan, speed 3, 200 ms: USB success.
- Explicit stop: USB success; **user confirmed visible movement and stop**.

The user subsequently launched the daemon from the GUI with the experimental
profile, confirmed both connection indicators, and successfully tested a GUI
right-jog at speed 3 and Apply Zoom (110%, then back to 100%). This verifies the
preview → GUI → WebSocket → daemon → camera path for these controls.

Tilt, center, all firmware versions, and recovery scenarios remain unverified.
The profile remains experimental. Start a video stream before controls; standby
acknowledgements are not proof of an applied change.

## AI/mode follow-up (read-only)

The `--ai` probe adds capability/length-gated current reads for selected candidates.
The [AI transcript](link2pro-ai-probe.txt) completed without failed requests.
XU9 selector `0x02` returned **61 bytes**, starting with `00 03` (Normal and a stale
substate). Several later bytes were nonzero: mode writes must preserve those bytes,
not create a zero-filled legacy command.

A published [Link 2 Pro hardware investigation](https://github.com/tagawa0525/insta360-link2pro-linux/blob/main/references/README.md)
documents this exact 61-byte mode interface for tracking, DeskView, overhead, and
whiteboard. It also records that the legacy framing selector can echo writes
without affecting framing on the Pro. The new app uses the model-specific mode
interface; it does not infer gesture, speed, or target controls from readable bytes.

The one-byte XU11 selector `0x02` returned `00`, but its use as tracking is only
established on **Link 2**, not Pro. It remains a diagnostic candidate and is not
used for the Pro mode controls. See [AI implementation and limits](../../docs/ai-camera-controls.md).

These local checks performed no mode writes. Frame-based mode behavior in the
external source is separate from local physical verification.

# Direct camera settings

## Product direction

Link Control is a local interface to the webcam's hardware controls. Image changes
are sent to the camera over USB, so they affect its video in other applications as
well as this app's preview. They are not SwiftUI image filters, and do not require
the Insta360 application to run.

The first settings panel uses recognized USB Video Class (UVC) Camera Terminal and
Processing Unit controls. Availability, current values, ranges, steps, defaults,
read/write capability, and automatic/manual state come from the connected camera.
Unsupported settings are not presented as working controls.

## Initial capabilities

The connected Link 2 Pro's descriptors advertise:

- Processing Unit 5, bitmap `5f 14`: brightness, contrast, hue, saturation,
  sharpness, white-balance temperature, automatic white balance, power-line frequency.
- Camera Terminal 1, bitmap `20 7a 02`: focus position and autofocus, in addition
  to existing zoom/position controls.
- Standard exposure mode/time controls are not advertised by this model's Camera
  Terminal. This does not mean the hardware lacks exposure control; its official
  software can use a proprietary interface instead.

Descriptor presence alone is not verification of functional setting changes. Each
control is queried before being enabled. Manual focus/white balance depend on
their corresponding automatic setting, and writes must obey the device's actual
range and step. Keep a video stream active when changing Link 2 Pro settings.

Read-only queries on the connected Link 2 Pro confirmed brightness, contrast,
saturation, and sharpness at 50 with ranges 0–100; hue at 0 with range −15–15;
focus at 79 with range 0–100; and white balance at 5430 K with range
2000–10000 K. All report step 1. Autofocus and automatic white balance were on,
so their manual controls were disabled. These values are an observation, not
fixed application defaults.

The camera returned power-line frequency `3` despite advertising a UVC version
whose standard options are only Off/50 Hz/60 Hz. This inconsistent control is
shown read-only with its reported value. No setting writes were performed during
these read-only hardware checks; write/readback validation uses a fake USB device
in automated tests until a live-camera change is explicitly tested.

## Protocol

The GUI's serialized WebSocket connection supports two settings commands:

```json
{"cmd":"settings_get"}
{"cmd":"setting_set","id":"brightness","value":50}
```

Both return a fresh `settings` snapshot. Writes use known setting IDs, not arbitrary
USB unit IDs, selectors, or byte strings. Values must be integral and valid for the
advertised control. A setting failure returns `settings_error` without pretending
the entire camera-control connection has failed.

```json
{"type":"settings","controls":[{"id":"brightness","label":"Brightness","group":"image","kind":"integer","value":50,"min":0,"max":100,"step":1,"default":50,"readable":true,"writable":true,"active":true,"unit":"","options":[],"error":null}]}
{"type":"settings_error","message":"Setting is not available in automatic mode"}
```

The example values above illustrate the wire format, not a universal webcam range.
Settings are fetched when a camera becomes available, on explicit refresh, and after
a write. Automatic preview/camera reconnection does not restore old settings onto
a newly connected camera.

## Beyond standard UVC

Exposure/ISO, HDR, gesture controls, tracking speed/framing/targets, presets,
and firmware management require separate model-specific verification. Existing
public interoperability work gives useful leads, but a mapping tested on Link or
Link 2 is not automatically verified on Link 2 Pro.

Link 2 Pro AI tracking, DeskView, overhead, and whiteboard auto-detection now use
the separately documented 61-byte Pro mode protocol. See
[AI camera controls](ai-camera-controls.md) for the mode schema, evidence, and
remaining unsupported features. The mode picker uses the same settings transport,
with ID `ai_mode` and group `ai`.

Research references (consulted September 2026):

- [WebCamControl](https://github.com/Daniel15/WebCamControl): an independent Linux
  webcam-control UI, including standard video controls and position presets.
- [Insta360 Link firmware analysis, USB control protocol section](https://github.com/schlarpc/insta360-link-firmware-re#7-usb-control-protocol):
  describes original-Link standard controls and vendor extension mappings. It lists
  XU9 selector `0x1d` for shutter time and `0x1e` for exposure mode. The Link 2 Pro
  probe reports matching lengths (2 and 1 bytes), but those semantics and ranges
  have not been verified on the Pro model.
- [Link 2 CLI](https://github.com/fmontes/insta360-link-cli#reverse-engineering-ai-tracking):
  reports tracking control at XU11 selector `0x02` on Link 2 (`2e1a:4c04`). Our Pro
  probe reports a one-byte control there; that is a lead for a future targeted
  hardware test, not proof that the same toggle works on Link 2 Pro. The implemented
  Pro mode control instead uses XU9 selector `0x02` with the complete 61-byte state.

No firmware modification or arbitrary vendor-register writes are exposed. Vendor
mode writes are restricted to the documented Pro mode values and preserve the rest
of the current state buffer.

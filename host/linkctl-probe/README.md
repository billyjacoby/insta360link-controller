# Read-only Link UVC probe (macOS)

Build with Apple's command-line tools; no third-party dependencies:

```sh
sh host/linkctl-probe/build.sh
host/linkctl-probe/build/linkctl-probe
# Also read selected AI/mode candidate registers (no writes):
host/linkctl-probe/build/linkctl-probe --ai
# Optionally select another exact VID/PID, e.g. the original Link:
host/linkctl-probe/build/linkctl-probe 0x2e1a 0x4c01
```

The default device is the **Insta360 Link 2 Pro, VID 0x2e1a / PID 0x4c06**.
Only one matching camera may be connected. No serial numbers, USB location IDs,
device strings, video, audio, or unrelated device/system information are output.

See [FINDINGS.md](FINDINGS.md) for the observed Link 2 Pro compatibility evidence
and [link2pro-probe.txt](link2pro-probe.txt) for its read-only hardware transcript.

## What it reads

- IOKit's cached configuration descriptors, including interface numbers, raw UVC
  VideoControl descriptors, entity IDs, extension GUID bytes, source links, and
  control masks.
- `GET_INFO` and `GET_LEN` for every **advertised** extension-unit selector in an
  active configuration's VideoControl alternate setting zero.
- `GET_INFO` and, if readable, `GET_CUR` for advertised Camera Terminal absolute
  zoom. Zoom is selector `0x0b` but **bitmap bit 9**, not bit 10. Its two-byte
  length is defined by UVC; Camera Terminal zoom does not specify `GET_LEN`.

The USB request helper accepts only device-to-host `GET_INFO`, `GET_LEN`, and
two-byte zoom `GET_CUR`. With `--ai`, it also reads selected mode/tracking/gesture
candidate registers, gated on their advertised capability and exact Pro lengths:
XU9 selectors `02` (61 bytes), `05` (1), `06` (5), `12` (1), `13` (1), `15` (16),
`18` (4), `1b` (2), plus readable one-byte XU11 selectors `01`–`05`. Unknown semantics stay
unknown; this is a diagnostic capture, not a list of working AI controls. See
[AI protocol findings](../../docs/ai-camera-controls.md).
There is no `SET_CUR`, movement, center, stop, reset,
configuration/alternate-setting change, stream start, exclusive open, or interface
claim. It coexists with the macOS UVC driver. Each class request has a timeout.
Unsupported selectors are reported from their masks and never queried.

`GET_INFO` exposes a control's GET/SET capability bits without setting anything.
Matching entity IDs/selectors and payload lengths can support a compatibility
hypothesis, but do **not** prove that a proprietary command has the same meaning.
In particular, absence from a descriptor means the legacy mapping cannot be
assumed supported; it does not prove that undocumented firmware commands cannot
exist.

Exit codes: `0` completed, `1` target absent, `2` invocation/descriptor/IOKit error,
`3` one or more supported-control reads failed or returned an unexpected length.
Individual query errors retain their IOReturn code and byte count in the output.

## Settings transport verification

For a settings-only USB read without starting a server or sending its shutdown
stop command, use the daemon's diagnostic mode:

```sh
host/linkctl-daemon/build/linkctl-daemon --auto-camera --settings
```

`verify-settings.py` is a separate diagnostic client for the image-settings API.
It launches its own private daemon and prints the discovered settings. It requires
`websocket-client`, installed in the daemon's test environment:

```sh
host/linkctl-daemon/build/test-env/bin/python host/linkctl-probe/verify-settings.py
```

To explicitly test a reversible hardware write, keep camera preview running and use:

```sh
host/linkctl-daemon/build/test-env/bin/python host/linkctl-probe/verify-settings.py --write-test
```

This changes brightness by one valid step, checks camera readback, and restores the
original brightness in a `finally` block. Restoration failures are reported. It
does not change zoom or send jog commands. The private daemon performs its normal
stop during shutdown. Use `--daemon /path/to/linkctl-daemon` to test a bundled helper.

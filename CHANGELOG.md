# Changelog

## 0.1.0 — experimental preview

- Native SwiftUI dashboard with a fixed, opt-in camera preview.
- Automatic bundled-helper startup, supported-camera discovery, and shutdown.
- Bounded pan/tilt jogs, inverted vertical controls, center, stop, and zoom.
- Device-discovered image controls with range validation and readback.
- Link 2 Pro tracking, DeskView, overhead, and automatic whiteboard mode controls.
- Link 2 Pro master and individual gesture switches using the official SDK protocol,
  preserving unrelated settings and checking camera readback (physical behavior pending).
- Persistent menu-bar controls and 13 bundled Raycast Script Commands.
- Validated URL automation with startup waiting and pending-action cancellation.
- Relocatable app/ZIP packaging with bundled runtime libraries.

This is a preview, not a claim of full compatibility with the official app.
Tracking speed/framing, gesture bindings, proprietary exposure/ISO, and HDR
are not implemented. See the app README for hardware verification and limitations.

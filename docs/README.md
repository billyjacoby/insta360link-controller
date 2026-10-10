# Documentation

## Using Link Control

- [Desktop app guide](../host/linkctl-gui/README.md): installation, preview,
  movement, settings, menu bar, supported hardware, and troubleshooting.
- [Raycast setup](../integrations/raycast/README.md): add the bundled scripts,
  assign hotkeys, and understand action delivery versus execution.
- [Build, install, and release](releasing.md): local acceptance checklist,
  fork publishing, draft releases, and signing/licensing limitations.
- [Changelog](../CHANGELOG.md): GUI preview features.

The desktop app owns its bundled camera helper. Users do not install or start
the standalone daemon. Camera permission is requested only for explicit preview
startup. Keep a video stream active when controlling Link 2 Pro.

## Developing and understanding the protocol

- [Desktop automation](desktop-automation.md): URL actions, process ownership,
  menu-bar lifecycle, and packaging of Raycast commands.
- [Camera image settings](camera-settings.md): capability discovery, validation,
  readback, and wire messages.
- [AI camera controls](ai-camera-controls.md): Link 2 Pro mode protocol,
  external research, local evidence, and unmapped controls.
- [Hardware findings](../host/linkctl-probe/FINDINGS.md) and
  [read-only USB probe](../host/linkctl-probe/README.md).
- [Packaging scripts](../host/linkctl-gui/scripts/README.md): versioned bundles,
  checksums, metadata, and local installation.
- [Original GUI implementation plan](native-macos-gui-plan.md): design history
  and milestones, not the current user guide.

For the standalone daemon, CLI, or ESP32 joystick setup, see the
[root README](../README.md#standalone-daemon-cli-and-joystick-setup).
These are separate from the app's private helper connection.

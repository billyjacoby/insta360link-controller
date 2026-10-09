# Local installation and preview releases

## Build and install locally first

From the repository root on a Mac with Xcode command-line tools and Homebrew:

```sh
brew install cmake libwebsockets cjson openssl@3 pkgconf
bash host/linkctl-gui/scripts/package-app.sh
LINKCTL_TEST_BUNDLE="$PWD/host/linkctl-gui/dist/Link Control.app" \
  swift test --package-path host/linkctl-gui
python3 integrations/tests/test_raycast.py
bash host/linkctl-gui/scripts/install-local.sh
open '/Applications/Link Control.app'
```

Quit an existing Link Control process before installing. The installer validates
the bundle and preserves an existing installation as a backup. It does not
automatically launch the app or start preview.

The local build is ad-hoc signed. Its minimum macOS version is derived from the
actual embedded binaries, not just the Swift package's macOS 13 source target.
Do not advertise a lower minimum than the packaged metadata. Homebrew is required
to build, not to run the packaged app.

### Local acceptance

1. Open the installed app and confirm Camera ready without choosing a helper.
2. Explicitly start preview and grant camera permission if prompted.
3. Check jog directions, Stop, center, and zoom with the video stream active.
4. Check one image setting and restore it; test camera modes with a suitable
   subject, returning to Normal afterward.
5. Close the dashboard; check the menu-bar menu and Open Dashboard.
6. Add the bundled Raycast directory and try Open Camera Dashboard, then a camera
   action. The app reports execution status; Raycast reports URL delivery.
7. Quit from the menu bar and confirm the app-owned helper stops.

Automated tests do not replace visual confirmation of physical camera behavior.

## Publish source to your fork

The intended fork is `billyjacoby/insta360link-controller`; retain
`jfwoods/insta360link-controller` as `upstream`. Push reviewed source and tests to
the fork's `main`. Build output, local app bundles, credentials, and caches do not
belong in Git. GitHub Actions supplies downloadable build artifacts after CI passes.

## Prepare a release

Update `CHANGELOG.md` and the source `Info.plist` version, verify CI, and tag the
tested commit with a numeric version such as `v0.1.0`. The release workflow embeds
the tag version in the app and creates a **draft** GitHub release with
architecture-specific archives, checksums, and build metadata.

Before publishing that draft:

- Resolve the upstream software-license status described in `NOTICE.md`.
- Verify the Apple Silicon and Intel builds separately and state each minimum OS.
- Include the known hardware limitations and the active-stream requirement.
- State that builds are ad-hoc signed and not notarized, unless the actual release
  has been signed using Developer ID and submitted to Apple's notary service.

No Developer ID or notarization credentials are configured by these scripts.
Do not put signing keys or credentials in the repository. A future signed release
must sign nested code and the app with hardened runtime, notarize, staple, and
validate the downloaded archive on another Mac before claiming notarization.

The standalone daemon and CLI archives are advanced-use packages; unlike the GUI
bundle, the daemon archive requires its Homebrew runtime dependencies.

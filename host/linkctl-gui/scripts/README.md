# App packaging

From `host/linkctl-gui`, run:

```sh
./scripts/package-app.sh
```

The script builds the release Swift UI and C daemon, embeds the daemon in the app, copies its non-system dynamic libraries, rewrites their install names to bundle-relative paths, copies available Homebrew license notices, ad-hoc signs the bundle, validates it from a sanitized environment and a relocated path, and writes `dist/Link Control.app`, `dist/Link Control.zip`, its `.sha256` checksum, and concise `.metadata.json` release metadata.

It also includes the Raycast Script Commands and their shared helper under
`Contents/Resources/Raycast`, with executable permissions. The menu-bar app opens
this folder for Raycast setup; the scripts route through the app's `linkcontrol://`
URL interface and do not run a separate camera daemon.

It also inspects every packaged Mach-O image with `vtool` and sets the bundle copy of `LSMinimumSystemVersion` to the highest embedded minimum OS. This avoids overstating compatibility when locally installed Homebrew libraries require a newer macOS. The source `Info.plist` is not changed; the selected architectures and resulting minimum OS are printed during packaging.

Use alternate destinations without touching an installed/running app:

```sh
OUTPUT="$PWD/dist-test/Link Control.app" \
ZIP_OUTPUT="$PWD/dist-test/Link Control.zip" \
./scripts/package-app.sh
```

Set `VERSION` to a numeric release version (`x.y.z`) and, if needed,
`BUILD_NUMBER` to a positive integer. Both otherwise retain the source plist's
values. For example: `VERSION=1.2.3 BUILD_NUMBER=42 ./scripts/package-app.sh`.

To install a verified local build without launching it:

```sh
./scripts/install-local.sh
# or: ./scripts/install-local.sh "$PWD/dist/Link Control.app"
```

The installer verifies the bundle ID and ad-hoc signature, refuses while Link
Control is running, and moves an existing `/Applications/Link Control.app` to a
timestamped backup. It never invokes `sudo`.

Homebrew and build tools are required only while packaging. The result is ad-hoc signed for local use; distribution outside local development requires Developer ID signing and notarization credentials, which this script intentionally does not provide.

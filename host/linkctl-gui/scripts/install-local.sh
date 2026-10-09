#!/bin/bash
# Install a locally packaged app without elevating privileges or launching it.
set -euo pipefail

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
SOURCE=${1:-"$ROOT/dist/Link Control.app"}
DESTINATION="/Applications/Link Control.app"
EXPECTED_BUNDLE_ID="org.linkctl.gui"

for command in codesign plutil ditto pgrep stat date; do
    command -v "$command" >/dev/null || { echo "install-local.sh: required command not found: $command" >&2; exit 1; }
done
[ -d "$SOURCE" ] || { echo "install-local.sh: app bundle not found: $SOURCE" >&2; exit 1; }
[ "$(plutil -extract CFBundleIdentifier raw "$SOURCE/Contents/Info.plist")" = "$EXPECTED_BUNDLE_ID" ] || {
    echo "install-local.sh: refusing bundle with an unexpected identifier" >&2; exit 1;
}
codesign --verify --deep --strict --verbose=2 "$SOURCE"

if pgrep -x LinkControlGUI >/dev/null; then
    echo "install-local.sh: Link Control is running. Quit it before installing." >&2
    exit 1
fi
[ -w /Applications ] || {
    echo "install-local.sh: /Applications is not writable; install manually or ask an administrator. This script does not use sudo." >&2
    exit 1
}

STAGING=$(mktemp -d /Applications/.Link-Control-install.XXXXXX)
cleanup() { rm -rf "$STAGING"; }
trap cleanup EXIT
ditto "$SOURCE" "$STAGING/Link Control.app"

if [ -e "$DESTINATION" ]; then
    timestamp=$(stat -f '%Sm' -t '%Y%m%d-%H%M%S' "$DESTINATION")
    backup="/Applications/Link Control.app.backup-$timestamp"
    suffix=1
    while [ -e "$backup" ]; do backup="/Applications/Link Control.app.backup-$timestamp-$suffix"; suffix=$((suffix + 1)); done
    mv "$DESTINATION" "$backup"
    echo "Existing app preserved at: $backup"
fi
mv "$STAGING/Link Control.app" "$DESTINATION"
codesign --verify --deep --strict --verbose=2 "$DESTINATION"
echo "Installed: $DESTINATION"
echo "Link Control was not launched."

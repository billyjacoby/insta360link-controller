#!/bin/bash
# install.sh — Install linkctl-daemon from a release archive.
# Run from inside the extracted release directory.
set -euo pipefail

INSTALL_DIR="/usr/local/bin"
BINARY="linkctl-daemon"
LABEL="com.linkctl.daemon"
DOMAIN="gui/$(id -u)"
PLIST_DIR="$HOME/Library/LaunchAgents"
PLIST="$PLIST_DIR/$LABEL.plist"
LOG_FILE="$HOME/Library/Logs/linkctl-daemon.log"

echo "linkctl-daemon installer"
echo "========================"
echo

# Check we're on macOS
if [ "$(uname)" != "Darwin" ]; then
    echo "Error: this installer is for macOS only."
    exit 1
fi

# Check the binary is present
if [ ! -f "$BINARY" ]; then
    echo "Error: $BINARY not found in current directory."
    echo "Run this script from inside the extracted release archive."
    exit 1
fi

# Check Homebrew dependencies
echo "Checking dependencies..."
MISSING=()
for dep in libwebsockets cjson openssl@3; do
    if ! brew list --formula "$dep" &>/dev/null; then
        MISSING+=("$dep")
    fi
done

if [ ${#MISSING[@]} -gt 0 ]; then
    echo "Missing Homebrew packages: ${MISSING[*]}"
    echo "Install with: brew install ${MISSING[*]}"
    exit 1
fi
echo "  All dependencies found."

# Install binary
echo "Installing $BINARY to $INSTALL_DIR..."
sudo install -m 755 "$BINARY" "$INSTALL_DIR/$BINARY"
echo "  Done."

# Create launchd plist
echo "Setting up launchd service..."
mkdir -p "$PLIST_DIR"
cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$INSTALL_DIR/$BINARY</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ThrottleInterval</key>
    <integer>10</integer>
    <key>StandardOutPath</key>
    <string>$LOG_FILE</string>
    <key>StandardErrorPath</key>
    <string>$LOG_FILE</string>
</dict>
</plist>
EOF

# Load service (unload first if already running)
launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
launchctl bootstrap "$DOMAIN" "$PLIST"
echo "  Service started."

echo
echo "Installation complete!"
echo "  Binary:  $INSTALL_DIR/$BINARY"
echo "  Service: $LABEL (running)"
echo "  Logs:    $LOG_FILE"
echo
echo "To uninstall:"
echo "  launchctl bootout $DOMAIN/$LABEL"
echo "  sudo rm $INSTALL_DIR/$BINARY"
echo "  rm $PLIST"

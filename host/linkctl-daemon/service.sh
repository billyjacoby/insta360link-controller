#!/bin/sh
# Helper script for managing the linkctl-daemon launchd service.
# Usage: service.sh [install <binary-path>|uninstall|restart]

set -e

LABEL="com.linkctl.daemon"
DOMAIN="gui/$(id -u)"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

case "$1" in
    install)
        BINARY="$2"
        cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>Label</key>
    <string>$LABEL</string>
    <key>ProgramArguments</key>
    <array>
        <string>$BINARY</string>
    </array>
    <key>RunAtLoad</key>
    <true/>
    <key>KeepAlive</key>
    <true/>
    <key>ThrottleInterval</key>
    <integer>10</integer>
    <key>StandardOutPath</key>
    <string>/tmp/linkctl-daemon.log</string>
    <key>StandardErrorPath</key>
    <string>/tmp/linkctl-daemon.log</string>
</dict>
</plist>
EOF
        launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
        launchctl bootstrap "$DOMAIN" "$PLIST"
        echo "Service installed and started (binary: $BINARY)."
        ;;
    uninstall)
        launchctl bootout "$DOMAIN/$LABEL" 2>/dev/null || true
        rm -f "$PLIST"
        echo "Service removed."
        ;;
    restart)
        launchctl kickstart -k "$DOMAIN/$LABEL" 2>/dev/null || launchctl kickstart "$DOMAIN/$LABEL"
        echo "Service restarted."
        ;;
    *)
        echo "Usage: $0 {install <binary-path>|uninstall|restart}" >&2
        exit 1
        ;;
esac

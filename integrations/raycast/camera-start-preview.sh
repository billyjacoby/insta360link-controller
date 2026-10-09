#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Start Camera Preview
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Explicitly start Link Control's preview to wake the camera. macOS may ask for camera permission on first use.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "start-preview"

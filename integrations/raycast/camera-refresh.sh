#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Refresh Camera Settings
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Read current camera settings, including changes made by gestures or other software.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "refresh"

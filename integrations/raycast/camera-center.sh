#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Center Camera
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Center the camera using Link Control. Keep a video stream active; select Normal mode before manual positioning.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "center"

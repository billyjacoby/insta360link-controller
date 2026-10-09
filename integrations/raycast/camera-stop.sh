#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Stop Camera Jog
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Stop manual motion and cancel queued automation. Use Camera Mode → Normal to disable AI tracking.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "stop"

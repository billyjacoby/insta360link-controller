#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Jog Camera Right
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Send one bounded rightward jog through Link Control.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "jog?direction=right"

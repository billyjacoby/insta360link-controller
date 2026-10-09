#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Jog Camera Up
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Send one bounded upward jog using the app's speed and inverted tilt mapping.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "jog?direction=up"

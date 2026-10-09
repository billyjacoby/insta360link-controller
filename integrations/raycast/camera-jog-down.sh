#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Jog Camera Down
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Send one bounded downward jog using the app's speed and inverted tilt mapping.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "jog?direction=down"

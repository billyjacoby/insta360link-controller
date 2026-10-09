#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Open Camera Dashboard
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Show the Link Control dashboard, including the latest automation result.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "dashboard"

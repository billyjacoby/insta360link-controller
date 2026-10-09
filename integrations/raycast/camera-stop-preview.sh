#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Stop Camera Preview
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.description Stop the preview owned by Link Control. Other applications' camera sessions are unaffected.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
link_control_no_arguments "$@"
link_control_send "stop-preview"

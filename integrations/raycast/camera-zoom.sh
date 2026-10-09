#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Set Camera Zoom
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.argument1 {"type":"text","placeholder":"Zoom percent (100–400)"}
# @raycast.description Set camera zoom in percent: 100 = 1×, 200 = 2×. Requires an active video stream.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
[[ "$#" -eq 1 ]] || link_control_error "Enter a zoom percentage from 100 to 400."
[[ "$1" =~ ^[1-4][0-9]{2}$ ]] || link_control_error "Zoom must be a whole number from 100 to 400."
(( $1 <= 400 )) || link_control_error "Zoom must be a whole number from 100 to 400."
link_control_send "zoom?value=$1"

#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Set Camera Mode
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.argument1 {"type":"dropdown","placeholder":"Camera mode","data":[{"title":"Normal / Tracking Off","value":"normal"},{"title":"AI Tracking","value":"tracking"},{"title":"DeskView","value":"deskview"},{"title":"Whiteboard Auto-detect","value":"whiteboard"},{"title":"Overhead","value":"overhead"}]}
# @raycast.description Change a supported Link 2 Pro camera mode. Keep preview running; tracking needs a person in frame.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
[[ "$#" -eq 1 ]] || link_control_error "Choose a camera mode."
case "$1" in
  normal|tracking|deskview|whiteboard|overhead) link_control_send "mode?value=$1" ;;
  *) link_control_error "Unknown camera mode." ;;
esac

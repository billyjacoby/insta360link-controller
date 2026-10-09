#!/bin/bash
# @raycast.schemaVersion 1
# @raycast.title Set Camera Image Setting
# @raycast.mode compact
# @raycast.packageName Link Control
# @raycast.icon 📷
# @raycast.argument1 {"type":"dropdown","placeholder":"Setting","data":[{"title":"Brightness","value":"brightness"},{"title":"Contrast","value":"contrast"},{"title":"Hue","value":"hue"},{"title":"Saturation","value":"saturation"},{"title":"Sharpness","value":"sharpness"},{"title":"Autofocus (0 off, 1 on)","value":"focus_auto"},{"title":"Manual Focus","value":"focus_absolute"},{"title":"Auto White Balance (0 off, 1 on)","value":"white_balance_auto"},{"title":"White Balance Temperature (K)","value":"white_balance_temperature"},{"title":"Power-line Frequency","value":"power_line_frequency"},{"title":"Exposure Mode","value":"exposure_mode"},{"title":"Exposure Time","value":"exposure_absolute"},{"title":"Variable Frame Rate","value":"exposure_auto_priority"},{"title":"Gamma","value":"gamma"},{"title":"Gain","value":"gain"},{"title":"Backlight Compensation","value":"backlight_compensation"}]}
# @raycast.argument2 {"type":"text","placeholder":"Integer value (see camera's range in app)"}
# @raycast.description Set a supported image control. Link Control checks the connected camera's range, step, and automatic/manual state.
set -euo pipefail
source "$(CDPATH= cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)/lib/link-control.sh"
[[ "$#" -eq 2 ]] || link_control_error "Choose a setting and enter its integer value."
case "$1" in
  brightness|contrast|hue|saturation|sharpness|focus_auto|focus_absolute|white_balance_auto|white_balance_temperature|power_line_frequency|exposure_mode|exposure_absolute|exposure_auto_priority|gamma|gain|backlight_compensation) ;;
  *) link_control_error "Unknown image setting." ;;
esac
[[ "$2" =~ ^(0|-?[1-9][0-9]{0,9})$ ]] || link_control_error "Enter a whole-number value without spaces or leading zeros."
(( $2 >= -32768 && $2 <= 4294967295 )) || link_control_error "Value is outside the camera protocol's integer range."
link_control_send "setting?id=$1&value=$2"

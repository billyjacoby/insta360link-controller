#!/bin/bash
# Shared by the Raycast Script Commands. No Python, Node, or Homebrew required.

link_control_error() {
  printf '%s\n' "$1" >&2
  exit 1
}

link_control_no_arguments() {
  [[ "$#" -eq 0 ]] || link_control_error "This command does not take arguments."
}

link_control_send() {
  local url="linkcontrol://action/$1"
  # Diagnostic/test mode validates real script inputs without opening an app.
  if [[ "${LINK_CONTROL_DRY_RUN:-0}" == "1" ]]; then
    printf '%s\n' "$url"
    return
  fi
  if ! /usr/bin/open -g -b org.linkctl.gui "$url"; then
    link_control_error "Could not open Link Control. Install the packaged app in Applications and open it once."
  fi
  printf '%s\n' "Sent to Link Control. Check the app or menu bar for the result."
}

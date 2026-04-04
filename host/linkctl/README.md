# linkctl

Command-line client for controlling the Insta360 Link camera via the `linkctl-daemon`.

## Prerequisites

- `linkctl-daemon` must be running (see `host/linkctl-daemon/`)
- No external dependencies — pure POSIX C

## Build

```bash
cd host/linkctl
mkdir -p build && cd build
cmake ..
make
```

## Install (optional)

Install to `/usr/local/bin` so `linkctl` is available system-wide:

```bash
sudo cmake --install . --prefix /usr/local
```

Or run directly from the build directory: `./build/linkctl <command>`.

## Usage

```
linkctl <command> [args]
```

### Commands

| Command | Description |
|---------|-------------|
| `linkctl help` | Show usage information |
| `linkctl status` | Show camera connection state and current zoom |
| `linkctl center` | Center the gimbal and reset zoom to 1.0x |
| `linkctl zoom <1.0-4.0>` | Set zoom level (e.g., `linkctl zoom 2.5`) |
| `linkctl zoom` | Show the current zoom level |
| `linkctl stop` | Stop all gimbal movement |
| `linkctl jog` | Enter interactive pan/tilt mode |

### Jog Mode

`linkctl jog` enters an interactive mode for real-time camera control using keyboard input.

**Controls:**

| Key | Action |
|-----|--------|
| `W` | Tilt up |
| `S` | Tilt down |
| `A` | Pan left |
| `D` | Pan right |
| `+` / `=` | Increase speed |
| `-` | Decrease speed |
| Space | Stop movement |
| `C` | Center gimbal |
| `Q` / ESC | Exit jog mode |

Movement continues while a key is held (via terminal key repeat) and automatically stops ~400ms after release.

Default speeds: pan=15, tilt=10. Speed range: 3–30 (pan), 3–20 (tilt).

## How It Works

`linkctl` connects to the daemon's TCP control socket at `localhost:9001` using a simple line-based text protocol. For one-shot commands it connects, sends, prints the result, and exits. Jog mode maintains the connection and uses `poll()` on both stdin and the socket for responsive keyboard-driven camera control.

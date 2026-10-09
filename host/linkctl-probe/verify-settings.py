#!/usr/bin/env python3
"""Check real settings transport; --write-test changes/restores one brightness step.

Requires websocket-client (the daemon's build/test-env has it). Keep live preview
running for a write test. Never uses the independently running app's service.
"""

import argparse
import json
from pathlib import Path
import selectors
import socket
import subprocess
import time

import websocket


def snapshot(ws, command):
    ws.send(json.dumps(command))
    while True:
        reply = json.loads(ws.recv())
        if reply.get("type") == "settings_error":
            raise RuntimeError(reply.get("message", "Settings request failed"))
        if reply.get("type") == "settings":
            return {control["id"]: control for control in reply["controls"]}
        if reply.get("type") != "status":
            raise RuntimeError(f"Unexpected reply: {reply}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--daemon", type=Path, default=Path(__file__).resolve().parents[1] / "linkctl-daemon/build/linkctl-daemon")
    parser.add_argument("--write-test", action="store_true", help="Change brightness one step, verify readback, then restore it")
    args = parser.parse_args()
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    process = subprocess.Popen(
        [str(args.daemon), "--auto-camera", "--loopback", "--no-control", "--no-mdns", "--port", str(port)],
        stdout=subprocess.PIPE, stderr=None,
    )
    ws = None
    try:
        # Read only this child pipe; a competing service cannot supply readiness.
        ready = f"[main] linkctl-daemon running on port {port}\n".encode()
        received = b""
        deadline = time.monotonic() + 20
        with selectors.DefaultSelector() as selector:
            selector.register(process.stdout, selectors.EVENT_READ)
            while ready not in received:
                if process.poll() is not None:
                    raise RuntimeError("Camera helper exited during startup")
                if time.monotonic() >= deadline:
                    raise TimeoutError("Camera helper did not become ready")
                for key, _ in selector.select(.1):
                    received = (received + key.fileobj.read1(4096))[-16384:]
        ws = websocket.create_connection(f"ws://127.0.0.1:{port}", subprotocols=["arduino"], timeout=15)
        controls = snapshot(ws, {"cmd": "settings_get"})
        print(json.dumps(list(controls.values()), indent=2))
        if not args.write_test:
            return

        control = controls.get("brightness")
        if not control or not control["active"] or not control["writable"] or control["value"] is None:
            raise RuntimeError("Brightness is not available for a reversible write test")
        original, step = control["value"], control["step"]
        target = original + step if original + step <= control["max"] else original - step
        if step <= 0 or target < control["min"] or target == original:
            raise RuntimeError("No alternate valid brightness step")
        try:
            changed = snapshot(ws, {"cmd": "setting_set", "id": "brightness", "value": target})
            actual = changed["brightness"]["value"]
            if actual != target:
                raise RuntimeError(f"Requested brightness {target}, read back {actual}; is preview running?")
            print(f"Brightness write/readback verified: {original} -> {actual}")
        finally:
            restored = snapshot(ws, {"cmd": "setting_set", "id": "brightness", "value": original})
            actual = restored["brightness"]["value"]
            if actual != original:
                raise RuntimeError(f"Brightness restoration failed: expected {original}, read back {actual}")
            print(f"Original brightness restored: {actual}")
    finally:
        if ws is not None:
            ws.close()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        process.stdout.close()


if __name__ == "__main__":
    main()

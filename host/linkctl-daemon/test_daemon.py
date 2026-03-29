#!/usr/bin/env python3
"""
Automated test suite for linkctl-daemon.

Tests direct camera control (--test mode) and WebSocket command interface.
Requires the Insta360 Link to be connected via USB.

Usage:
    python3 test_daemon.py                  # Run all tests
    python3 test_daemon.py --skip-motion    # Skip tests that move the camera
"""

import json
import os
import signal
import subprocess
import sys
import time

DAEMON_BIN = os.path.join(os.path.dirname(__file__), "build", "linkctl-daemon")
DAEMON_PORT = 19000  # Use non-default port to avoid conflicts
WS_URL = f"ws://localhost:{DAEMON_PORT}"
CONNECT_TIMEOUT = 5
RECV_TIMEOUT = 3

# --- Helpers ---

class Colors:
    GREEN = "\033[92m"
    RED = "\033[91m"
    YELLOW = "\033[93m"
    BOLD = "\033[1m"
    RESET = "\033[0m"


def log(msg):
    print(f"  {msg}")


def pass_test(name):
    print(f"{Colors.GREEN}  PASS{Colors.RESET} {name}")


def fail_test(name, reason):
    print(f"{Colors.RED}  FAIL{Colors.RESET} {name}: {reason}")
    return False


def skip_test(name, reason):
    print(f"{Colors.YELLOW}  SKIP{Colors.RESET} {name}: {reason}")


def section(name):
    print(f"\n{Colors.BOLD}=== {name} ==={Colors.RESET}")


# --- Test runner ---

class DaemonTestSuite:
    def __init__(self, skip_motion=False):
        self.skip_motion = skip_motion
        self.daemon_proc = None
        self.passed = 0
        self.failed = 0
        self.skipped = 0

    def assert_test(self, name, condition, fail_reason="assertion failed"):
        if condition:
            pass_test(name)
            self.passed += 1
            return True
        else:
            fail_test(name, fail_reason)
            self.failed += 1
            return False

    def skip(self, name, reason):
        skip_test(name, reason)
        self.skipped += 1

    # --- Daemon lifecycle ---

    def start_daemon(self, extra_args=None):
        args = [DAEMON_BIN, "--port", str(DAEMON_PORT), "--no-mdns"]
        if extra_args:
            args.extend(extra_args)
        self.daemon_proc = subprocess.Popen(
            args,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
        )
        # Wait for server to be ready
        time.sleep(1)

    def stop_daemon(self):
        if self.daemon_proc:
            self.daemon_proc.send_signal(signal.SIGTERM)
            try:
                self.daemon_proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                self.daemon_proc.kill()
                self.daemon_proc.wait()
            self.daemon_proc = None

    def get_daemon_output(self):
        if self.daemon_proc:
            # Non-blocking read of whatever's available
            import select
            output = []
            while select.select([self.daemon_proc.stdout], [], [], 0.1)[0]:
                line = self.daemon_proc.stdout.readline()
                if not line:
                    break
                output.append(line.strip())
            return output
        return []

    # --- WebSocket helpers ---

    def ws_connect(self):
        import websocket
        ws = websocket.create_connection(WS_URL, timeout=CONNECT_TIMEOUT)
        return ws

    def ws_send_recv(self, ws, cmd_dict):
        ws.send(json.dumps(cmd_dict))
        ws.settimeout(RECV_TIMEOUT)
        try:
            response = ws.recv()
            return json.loads(response)
        except Exception:
            return None

    def ws_recv(self, ws):
        ws.settimeout(RECV_TIMEOUT)
        try:
            response = ws.recv()
            return json.loads(response)
        except Exception:
            return None

    # --- Test groups ---

    def test_binary_exists(self):
        section("Prerequisites")
        self.assert_test(
            "Daemon binary exists",
            os.path.isfile(DAEMON_BIN),
            f"Not found: {DAEMON_BIN}. Run 'make' first."
        )

    def test_direct_camera(self):
        """Test --test mode: direct camera control without WebSocket."""
        section("Direct Camera Control (--test mode)")

        if self.skip_motion:
            self.skip("--test mode pan/stop", "--skip-motion flag set")
            return

        result = subprocess.run(
            [DAEMON_BIN, "--test"],
            capture_output=True, text=True, timeout=30
        )

        output = result.stdout + result.stderr
        log(f"Exit code: {result.returncode}")
        for line in output.strip().split("\n"):
            log(f"  {line}")

        self.assert_test(
            "--test mode exits cleanly",
            result.returncode == 0,
            f"Exit code {result.returncode}"
        )
        self.assert_test(
            "--test mode finds camera",
            "Camera connected" in output or "opened" in output.lower(),
            "Camera connection message not found in output"
        )
        self.assert_test(
            "--test mode completes",
            "Test complete" in output or "complete" in output.lower(),
            "Completion message not found in output"
        )

    def test_server_startup(self):
        """Test that the daemon starts and listens on the WebSocket port."""
        section("Server Startup")

        self.start_daemon()
        alive = self.daemon_proc.poll() is None
        self.assert_test("Daemon process is running", alive, "Process exited early")

        if not alive:
            out = self.daemon_proc.stdout.read()
            log(f"Daemon output: {out}")
            self.daemon_proc = None
            return

    def test_ws_connect_and_initial_status(self):
        """Test WebSocket connection and initial status message."""
        section("WebSocket Connection")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("WebSocket connect", "Daemon not running")
            return

        try:
            ws = self.ws_connect()
            self.assert_test("WebSocket connects", True)

            # Should receive initial status on connect
            status = self.ws_recv(ws)
            self.assert_test(
                "Receives initial status",
                status is not None and status.get("type") == "status",
                f"Got: {status}"
            )
            if status:
                self.assert_test(
                    "Status has camera_connected field",
                    "camera_connected" in status,
                    f"Missing camera_connected: {status}"
                )
                self.assert_test(
                    "Camera is connected",
                    status.get("camera_connected") is True,
                    f"camera_connected={status.get('camera_connected')}"
                )
                self.assert_test(
                    "Status has zoom field",
                    "zoom" in status,
                    f"Missing zoom: {status}"
                )
                zoom = status.get("zoom", 0)
                self.assert_test(
                    "Zoom is in valid range (100-400)",
                    100 <= zoom <= 400,
                    f"zoom={zoom}"
                )
                log(f"Initial zoom level: {zoom}")

            ws.close()
        except Exception as e:
            fail_test("WebSocket connect", str(e))
            self.failed += 1

    def test_status_command(self):
        """Test the status query command."""
        section("Status Command")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Status command", "Daemon not running")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)  # Consume initial status

            resp = self.ws_send_recv(ws, {"cmd": "status"})
            self.assert_test(
                "Status command returns response",
                resp is not None,
                "No response"
            )
            if resp:
                self.assert_test(
                    "Status response type is 'status'",
                    resp.get("type") == "status",
                    f"type={resp.get('type')}"
                )
                self.assert_test(
                    "Status shows camera connected",
                    resp.get("camera_connected") is True,
                    f"camera_connected={resp.get('camera_connected')}"
                )

            ws.close()
        except Exception as e:
            fail_test("Status command", str(e))
            self.failed += 1

    def test_pan_tilt(self):
        """Test pan/tilt commands."""
        section("Pan/Tilt Commands")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Pan/tilt", "Daemon not running")
            return

        if self.skip_motion:
            self.skip("Pan/tilt", "--skip-motion flag set")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)  # Consume initial status

            # Pan right slowly
            log("Sending: pan right, speed 8...")
            ws.send(json.dumps({
                "cmd": "pan_tilt",
                "pan_dir": 1, "pan_speed": 8,
                "tilt_dir": 0, "tilt_speed": 0
            }))
            time.sleep(1)

            # No error response means success (commands don't echo back)
            # Check daemon is still running
            self.assert_test(
                "Pan right accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            # Pan left slowly
            log("Sending: pan left, speed 8...")
            ws.send(json.dumps({
                "cmd": "pan_tilt",
                "pan_dir": 255, "pan_speed": 8,
                "tilt_dir": 0, "tilt_speed": 0
            }))
            time.sleep(1)
            self.assert_test(
                "Pan left accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            # Tilt up slowly
            log("Sending: tilt up, speed 5...")
            ws.send(json.dumps({
                "cmd": "pan_tilt",
                "pan_dir": 0, "pan_speed": 0,
                "tilt_dir": 1, "tilt_speed": 5
            }))
            time.sleep(1)
            self.assert_test(
                "Tilt up accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            # Combined pan+tilt
            log("Sending: pan right + tilt down...")
            ws.send(json.dumps({
                "cmd": "pan_tilt",
                "pan_dir": 1, "pan_speed": 5,
                "tilt_dir": 255, "tilt_speed": 5
            }))
            time.sleep(1)
            self.assert_test(
                "Combined pan+tilt accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            ws.close()
        except Exception as e:
            fail_test("Pan/tilt", str(e))
            self.failed += 1

    def test_stop(self):
        """Test the stop command."""
        section("Stop Command")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Stop", "Daemon not running")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)

            log("Sending: stop")
            ws.send(json.dumps({"cmd": "stop"}))
            time.sleep(0.5)
            self.assert_test(
                "Stop command accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            ws.close()
        except Exception as e:
            fail_test("Stop", str(e))
            self.failed += 1

    def test_center(self):
        """Test the center command."""
        section("Center Command")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Center", "Daemon not running")
            return

        if self.skip_motion:
            self.skip("Center", "--skip-motion flag set")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)

            log("Sending: center (gimbal will move to home position)")
            ws.send(json.dumps({"cmd": "center"}))
            time.sleep(3)  # Give gimbal time to center
            self.assert_test(
                "Center command accepted (no crash)",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            ws.close()
        except Exception as e:
            fail_test("Center", str(e))
            self.failed += 1

    def test_zoom(self):
        """Test zoom commands."""
        section("Zoom Commands")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Zoom", "Daemon not running")
            return

        if self.skip_motion:
            self.skip("Zoom", "--skip-motion flag set")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)

            # Get initial zoom
            resp = self.ws_send_recv(ws, {"cmd": "status"})
            initial_zoom = resp.get("zoom", 100) if resp else 100
            log(f"Initial zoom: {initial_zoom}")

            # Zoom in
            target_zoom = 200
            log(f"Sending: zoom to {target_zoom}")
            ws.send(json.dumps({"cmd": "zoom", "value": target_zoom}))
            time.sleep(2)  # Camera needs time to apply zoom

            resp = self.ws_send_recv(ws, {"cmd": "status"})
            if resp:
                new_zoom = resp.get("zoom", 0)
                log(f"Zoom after set: {new_zoom}")
                self.assert_test(
                    f"Zoom changed to {target_zoom}",
                    abs(new_zoom - target_zoom) <= 5,  # Allow small tolerance
                    f"Expected ~{target_zoom}, got {new_zoom}"
                )

            # Zoom back to 1x
            log("Sending: zoom back to 100 (1x)")
            ws.send(json.dumps({"cmd": "zoom", "value": 100}))
            time.sleep(1)

            resp = self.ws_send_recv(ws, {"cmd": "status"})
            if resp:
                reset_zoom = resp.get("zoom", 0)
                self.assert_test(
                    "Zoom reset to 100",
                    abs(reset_zoom - 100) <= 5,
                    f"Expected ~100, got {reset_zoom}"
                )

            ws.close()
        except Exception as e:
            fail_test("Zoom", str(e))
            self.failed += 1

    def test_error_handling(self):
        """Test error responses for invalid commands."""
        section("Error Handling")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Error handling", "Daemon not running")
            return

        try:
            ws = self.ws_connect()
            _ = self.ws_recv(ws)

            # Invalid JSON
            ws.send("not json at all")
            resp = self.ws_recv(ws)
            self.assert_test(
                "Invalid JSON returns error",
                resp is not None and resp.get("type") == "error",
                f"Got: {resp}"
            )

            # Missing cmd field
            resp = self.ws_send_recv(ws, {"foo": "bar"})
            self.assert_test(
                "Missing cmd field returns error",
                resp is not None and resp.get("type") == "error",
                f"Got: {resp}"
            )

            # Unknown command
            resp = self.ws_send_recv(ws, {"cmd": "nonexistent"})
            self.assert_test(
                "Unknown command returns error",
                resp is not None and resp.get("type") == "error",
                f"Got: {resp}"
            )

            # Zoom missing value
            resp = self.ws_send_recv(ws, {"cmd": "zoom"})
            self.assert_test(
                "Zoom without value returns error",
                resp is not None and resp.get("type") == "error",
                f"Got: {resp}"
            )

            # Daemon should still be alive after all invalid inputs
            self.assert_test(
                "Daemon survives invalid inputs",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )

            ws.close()
        except Exception as e:
            fail_test("Error handling", str(e))
            self.failed += 1

    def test_multiple_clients(self):
        """Test multiple simultaneous WebSocket connections."""
        section("Multiple Clients")

        if not self.daemon_proc or self.daemon_proc.poll() is not None:
            self.skip("Multiple clients", "Daemon not running")
            return

        try:
            ws1 = self.ws_connect()
            ws2 = self.ws_connect()

            s1 = self.ws_recv(ws1)
            s2 = self.ws_recv(ws2)

            self.assert_test(
                "Both clients receive initial status",
                s1 is not None and s2 is not None,
                f"ws1={s1}, ws2={s2}"
            )

            # Send status from client 1
            resp = self.ws_send_recv(ws1, {"cmd": "status"})
            self.assert_test(
                "Client 1 can send commands",
                resp is not None and resp.get("type") == "status",
                f"Got: {resp}"
            )

            # Send status from client 2
            resp = self.ws_send_recv(ws2, {"cmd": "status"})
            self.assert_test(
                "Client 2 can send commands",
                resp is not None and resp.get("type") == "status",
                f"Got: {resp}"
            )

            ws1.close()
            ws2.close()

            self.assert_test(
                "Daemon survives client disconnections",
                self.daemon_proc.poll() is None,
                "Daemon crashed"
            )
        except Exception as e:
            fail_test("Multiple clients", str(e))
            self.failed += 1

    # --- Main ---

    def run(self):
        print(f"\n{Colors.BOLD}linkctl-daemon Test Suite{Colors.RESET}")
        print(f"Daemon: {DAEMON_BIN}")
        print(f"Port: {DAEMON_PORT}")
        if self.skip_motion:
            print(f"{Colors.YELLOW}Motion tests disabled (--skip-motion){Colors.RESET}")

        try:
            self.test_binary_exists()
            self.test_direct_camera()
            self.test_server_startup()
            self.test_ws_connect_and_initial_status()
            self.test_status_command()
            self.test_pan_tilt()
            self.test_stop()
            self.test_center()
            self.test_zoom()
            self.test_error_handling()
            self.test_multiple_clients()
        finally:
            self.stop_daemon()

        # Summary
        total = self.passed + self.failed + self.skipped
        print(f"\n{Colors.BOLD}Results: {total} tests{Colors.RESET}")
        print(f"  {Colors.GREEN}{self.passed} passed{Colors.RESET}")
        if self.failed:
            print(f"  {Colors.RED}{self.failed} failed{Colors.RESET}")
        if self.skipped:
            print(f"  {Colors.YELLOW}{self.skipped} skipped{Colors.RESET}")

        return 0 if self.failed == 0 else 1


if __name__ == "__main__":
    skip_motion = "--skip-motion" in sys.argv
    suite = DaemonTestSuite(skip_motion=skip_motion)
    sys.exit(suite.run())

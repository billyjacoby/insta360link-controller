#!/usr/bin/env python3
"""Status-only GUI transport regression tests. Requires websocket-client.

Starts its own daemon on a temporary WebSocket port. Sends no movement commands;
the daemon still performs its normal camera stop during shutdown.
The existing TCP service uses port 9001; stop other daemons before running.
"""

import concurrent.futures
import json
from pathlib import Path
import socket
import subprocess
import time
import unittest

import websocket


class ReadOnlyModeTests(unittest.TestCase):
    def test_invalid_helper_options_are_rejected_before_camera_access(self):
        binary = Path(__file__).parent / "build" / "linkctl-daemon"
        cases = [
            ["--auto-camera", "--link2-pro"],
            ["--port", "0"], ["--port", "65536"],
            ["--port", "9000garbage"], ["--port", "-1"],
        ]
        for arguments in cases:
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [str(binary), *arguments], capture_output=True, text=True, timeout=3
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("[camera]", result.stdout)

    def test_status_cannot_be_combined_with_motion_test(self):
        binary = Path(__file__).parent / "build" / "linkctl-daemon"
        for arguments in (["--status", "--test"], ["--test", "--status"],
                          ["--settings", "--test"], ["--test", "--settings"],
                          ["--settings", "--status"]):
            with self.subTest(arguments=arguments):
                result = subprocess.run(
                    [str(binary), *arguments], capture_output=True, text=True, timeout=3
                )
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("cannot be combined", result.stderr)
                self.assertNotIn("[camera]", result.stdout)


class GUITransportTests(unittest.TestCase):
    def setUp(self):
        with socket.socket() as probe:
            try:
                probe.bind(("127.0.0.1", 9001))
            except OSError:
                self.skipTest("TCP port 9001 already in use; stop the existing daemon")
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        binary = Path(__file__).parent / "build" / "linkctl-daemon"
        self.process = subprocess.Popen(
            [str(binary), "--no-mdns", "--port", str(port)],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
        )
        self.url = f"ws://127.0.0.1:{port}"
        self.addCleanup(self.cleanup)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if self.process.poll() is not None:
                self.fail(self.process.stderr.read().decode())
            try:
                with socket.create_connection(("127.0.0.1", port), timeout=.2):
                    return
            except OSError:
                time.sleep(.05)
        self.fail("Daemon did not start within five seconds")

    def cleanup(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait()
        self.process.stderr.close()

    def connect(self):
        ws = websocket.create_connection(self.url, subprotocols=["arduino"], timeout=3)
        self.assertEqual(json.loads(ws.recv())["type"], "status")
        return ws

    def test_burst_responses_are_not_overwritten(self):
        ws = self.connect()
        try:
            # Alternate message types so ordering and dropped replies are observable.
            for _ in range(10):
                ws.send('{"cmd":"status"}')
                ws.send('{"cmd":"unknown"}')
            for _ in range(10):
                self.assertEqual(json.loads(ws.recv())["type"], "status")
                self.assertEqual(json.loads(ws.recv())["type"], "error")
        finally:
            ws.close()

    def test_concurrent_gui_and_cli_status(self):
        def gui():
            ws = self.connect()
            try:
                for _ in range(20):
                    ws.send('{"cmd":"status"}')
                    status = json.loads(ws.recv())
                    self.assertIsInstance(status["camera_connected"], bool)
            finally:
                ws.close()

        def cli():
            with socket.create_connection(("127.0.0.1", 9001), timeout=3) as tcp:
                with tcp.makefile("rb") as reader:
                    for _ in range(20):
                        tcp.sendall(b"status\n")
                        self.assertTrue(reader.readline().startswith(b"status:"))

        with concurrent.futures.ThreadPoolExecutor(max_workers=3) as pool:
            jobs = [pool.submit(gui), pool.submit(gui), pool.submit(cli)]
            for job in jobs:
                job.result(timeout=10)

    def test_idle_shutdown(self):
        # With no WebSocket traffic lws_service must still wake for shutdown.
        time.sleep(.1)
        self.process.terminate()
        self.assertEqual(self.process.wait(timeout=2), 0)


class BundledServiceTests(unittest.TestCase):
    def test_private_helper_uses_only_its_loopback_port(self):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            port = probe.getsockname()[1]
        binary = Path(__file__).parent / "build" / "linkctl-daemon"
        process = subprocess.Popen(
            [str(binary), "--no-mdns", "--no-control", "--loopback", "--port", str(port)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        try:
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                self.assertIsNone(process.poll(), "Private helper exited during startup")
                try:
                    ws = websocket.create_connection(
                        f"ws://127.0.0.1:{port}", subprotocols=["arduino"], timeout=.2
                    )
                    break
                except (OSError, websocket.WebSocketException):
                    time.sleep(.05)
            else:
                self.fail("Private helper did not start")
            try:
                self.assertEqual(json.loads(ws.recv())["type"], "status")
                listeners = subprocess.check_output(
                    ["/usr/sbin/lsof", "-nP", "-a", "-p", str(process.pid),
                     "-iTCP", "-sTCP:LISTEN", "-Fn"], text=True,
                )
                names = [line for line in listeners.splitlines() if line.startswith("n")]
                self.assertEqual(names, [f"n127.0.0.1:{port}"])
            finally:
                ws.close()
            process.terminate()
            output, _ = process.communicate(timeout=3)
            self.assertEqual(process.returncode, 0, output)
            self.assertNotIn("TCP control socket listening", output)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait()
            process.stdout.close()


if __name__ == "__main__":
    unittest.main()

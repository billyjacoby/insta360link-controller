#!/usr/bin/env python3
"""Real WebSocket protocol tests against an IOKit-free fake camera server."""

import json
from pathlib import Path
import socket
import subprocess
import sys
import time
import unittest

import websocket


BINARY = Path(sys.argv.pop(1)) if len(sys.argv) > 1 else Path(__file__).parent / "build" / "test-settings-server"


class SettingsWebSocketTests(unittest.TestCase):
    def start(self, disconnected=False, scenario=None):
        with socket.socket() as probe:
            probe.bind(("127.0.0.1", 0))
            self.port = probe.getsockname()[1]
        self.process = subprocess.Popen(
            [str(BINARY), str(self.port), *([scenario] if scenario else ["disconnected"] if disconnected else [])],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
        )
        self.addCleanup(self.cleanup)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            try:
                with socket.create_connection(("127.0.0.1", self.port), timeout=.1):
                    return
            except OSError:
                time.sleep(.02)
        self.fail("Fake server failed to start")

    def cleanup(self):
        if self.process.poll() is None:
            self.process.terminate()
            try:
                self.process.communicate(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.communicate()

    def finish(self, writes):
        self.process.terminate()
        output, _ = self.process.communicate(timeout=3)
        self.assertEqual(self.process.returncode, 0, output)
        self.assertIn(f"USB writes: {writes}; PTZ writes: 0", output)

    def connect(self):
        ws = websocket.create_connection(
            f"ws://127.0.0.1:{self.port}", subprotocols=["arduino"], timeout=2
        )
        self.addCleanup(ws.close)
        self.assertEqual(json.loads(ws.recv())["type"], "status")
        return ws

    def request(self, ws, request):
        ws.send(json.dumps(request))
        return json.loads(ws.recv())

    def test_malformed_and_unsupported_requests_never_write(self):
        self.start()
        ws = self.connect()
        requests = [
            {"cmd": "setting_set"},
            {"cmd": "setting_set", "id": "brightness"},
            *({"cmd": "setting_set", "id": "brightness", "value": value}
              for value in [None, True, False, 1.5, "4", [], {}, 1e100, -32769, 4294967296, 11, -3]),
            {"cmd": "setting_set", "id": 3, "value": 0},
            {"cmd": "setting_set", "id": "", "value": 0},
            {"cmd": "setting_set", "id": "unknown", "value": 0},
            {"cmd": "setting_set", "id": "ai_mode", "value": 1},
            {"cmd": "setting_set", "id": "ai_tracking", "value": 1},
            {"cmd": "setting_set", "id": "exposure_absolute", "value": 100},
            {"cmd": "setting_set", "id": "focus_absolute", "value": 30},
            {"cmd": "setting_set", "id": "white_balance_temperature", "value": 4000},
            {"cmd": "setting_set", "id": "power_line_frequency", "value": 3},
        ]
        for request in requests:
            with self.subTest(request=request):
                response = self.request(ws, request)
                self.assertEqual(response["type"], "settings_error")
                self.assertTrue(response["message"])
        ws.send('{"cmd":"setting_set","id":"brightness","value":0,"value":2}')
        self.assertEqual(json.loads(ws.recv())["type"], "settings_error")
        for raw in [
            '{"cmd":"setting_set","id":"brightness","value":0}trailing',
            '{"cmd":"setting_set","id":"brightness\\u0000unknown","value":0}',
            '{"cmd":"setting_set","id":"brightness","value":0}\x00',
        ]:
            ws.send(raw)
            self.assertEqual(json.loads(ws.recv())["type"], "settings_error")
        ws.send_frame(websocket.ABNF.create_frame(
            '{"cmd":"setting_set","id":"brightness","value":0}',
            websocket.ABNF.OPCODE_TEXT, fin=0,
        ))
        ws.send_frame(websocket.ABNF.create_frame(
            'trailing', websocket.ABNF.OPCODE_CONT, fin=1,
        ))
        self.assertEqual(json.loads(ws.recv())["type"], "settings_error")
        self.assertTrue(self.request(ws, {"cmd": "status"})["camera_connected"])
        self.assertEqual(self.request(ws, {"cmd": "settings_get"})["type"], "settings")
        ws.close()
        self.finish(0)

    def test_snapshot_readback_and_no_unsolicited_broadcast(self):
        self.start()
        ws, observer = self.connect(), self.connect()
        snapshot = self.request(ws, {"cmd": "settings_get"})
        self.assertEqual(snapshot["type"], "settings")
        self.assertEqual(len(snapshot["controls"]), 10)
        result = self.request(ws, {"cmd": "setting_set", "id": "brightness", "value": -6})
        self.assertEqual(result["type"], "settings")
        self.assertEqual(next(c for c in result["controls"] if c["id"] == "brightness")["value"], -6)
        self.assertEqual(self.request(ws, {"cmd": "status"})["type"], "status")
        ws.send_frame(websocket.ABNF.create_frame(
            '{"cmd":"settings_', websocket.ABNF.OPCODE_TEXT, fin=0,
        ))
        ws.send_frame(websocket.ABNF.create_frame(
            'get"}', websocket.ABNF.OPCODE_CONT, fin=1,
        ))
        self.assertEqual(json.loads(ws.recv())["type"], "settings")
        observer.settimeout(.15)
        with self.assertRaises(websocket.WebSocketTimeoutException):
            observer.recv()
        ws.close()
        observer.close()
        self.finish(1)

    def test_no_camera_errors_are_settings_scoped(self):
        self.start(disconnected=True)
        ws = self.connect()
        for request in [{"cmd": "settings_get"}, {"cmd": "setting_set", "id": "brightness", "value": 0}]:
            self.assertEqual(self.request(ws, request)["type"], "settings_error")
        self.assertFalse(self.request(ws, {"cmd": "status"})["camera_connected"])
        ws.close()
        self.finish(0)

    def test_pro_modes_and_normal_intermediate(self):
        self.start(scenario="modes")
        ws = self.connect()
        snapshot = self.request(ws, {"cmd": "settings_get"})
        mode = next(c for c in snapshot["controls"] if c["id"] == "ai_mode")
        self.assertEqual(mode["kind"], "enum")
        self.assertEqual(mode["group"], "ai")
        self.assertEqual([o["value"] for o in mode["options"]], [0, 1, 4, 5, 6])
        self.assertIsNone(mode["default"])
        self.assertIsNone(mode["error"])  # Normal ignores stale whiteboard failure flag.
        self.assertFalse(mode["pending"])
        self.assertTrue(mode["detail"])
        writes = 0
        for value in [1, 4, 5, 6]:
            for target in [value, 0]:
                result = self.request(ws, {"cmd": "setting_set", "id": "ai_mode", "value": target})
                self.assertEqual(result["type"], "settings")
                mode = next(c for c in result["controls"] if c["id"] == "ai_mode")
                self.assertEqual(mode["value"], target)
                self.assertEqual(mode["pending"], target == 4)
                writes += 1
                if target:
                    error = self.request(ws, {"cmd": "setting_set", "id": "ai_mode", "value": 6 if target != 6 else 1})
                    self.assertEqual(error["type"], "settings_error")
                    self.assertIn("Normal", error["message"])
        ws.close()
        self.finish(writes)

    def test_pro_mode_transition_is_actual_readonly_snapshot(self):
        self.start(scenario="mode-transition")
        ws = self.connect()
        result = self.request(ws, {"cmd": "setting_set", "id": "ai_mode", "value": 1})
        mode = next(c for c in result["controls"] if c["id"] == "ai_mode")
        self.assertEqual(mode["value"], 255)
        self.assertTrue(mode["pending"])
        self.assertFalse(mode["writable"])
        self.assertIn("Changing mode", mode["detail"])
        result = self.request(ws, {"cmd": "setting_set", "id": "ai_mode", "value": 0})
        self.assertEqual(result["type"], "settings_error")
        result = self.request(ws, {"cmd": "setting_set", "id": "brightness", "value": -6})
        self.assertEqual(result["type"], "settings")
        ws.close()
        self.finish(2)

    def test_pro_unchanged_readback_does_not_echo_requested_mode(self):
        self.start(scenario="mode-unchanged")
        ws = self.connect()
        result = self.request(ws, {"cmd": "setting_set", "id": "ai_mode", "value": 1})
        self.assertEqual(result["type"], "settings")
        mode = next(c for c in result["controls"] if c["id"] == "ai_mode")
        self.assertEqual(mode["value"], 0)
        self.assertFalse(mode["pending"])
        self.assertTrue(self.request(ws, {"cmd": "status"})["camera_connected"])
        ws.close()
        self.finish(1)


if __name__ == "__main__":
    unittest.main()

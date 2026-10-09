#!/usr/bin/env python3
"""Exercise the real Raycast scripts without launching apps or accessing hardware."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SCRIPTS = Path(os.environ.get(
    "LINK_CONTROL_SCRIPT_DIR", str(Path(__file__).resolve().parents[1] / "raycast")
))


class RaycastScriptTests(unittest.TestCase):
    def run_script(self, name, *arguments, directory=SCRIPTS):
        return subprocess.run(
            ["/bin/bash", str(directory / name), *arguments],
            env={**os.environ, "LINK_CONTROL_DRY_RUN": "1"},
            capture_output=True, text=True, timeout=5,
        )

    def test_static_actions_generate_the_exact_app_urls(self):
        actions = {
            "camera-center.sh": "center",
            "camera-stop.sh": "stop",
            "camera-start-preview.sh": "start-preview",
            "camera-stop-preview.sh": "stop-preview",
            "camera-dashboard.sh": "dashboard",
            "camera-refresh.sh": "refresh",
            **{f"camera-jog-{direction}.sh": f"jog?direction={direction}"
               for direction in ["up", "down", "left", "right"]},
        }
        for name, action in actions.items():
            with self.subTest(name=name):
                result = self.run_script(name)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.strip(), f"linkcontrol://action/{action}")
                self.assertNotEqual(self.run_script(name, "unexpected").returncode, 0)

    def test_metadata_is_valid_and_all_dropdown_options_are_routable(self):
        for script in SCRIPTS.glob("*.sh"):
            with self.subTest(script=script.name):
                self.assertTrue(os.access(script, os.X_OK), "Raycast commands must be executable")
                metadata = {}
                for line in script.read_text().splitlines():
                    if line.startswith("# @raycast."):
                        key, value = line.removeprefix("# @raycast.").split(" ", 1)
                        metadata[key] = value
                self.assertEqual(metadata["schemaVersion"], "1")
                self.assertEqual(metadata["mode"], "compact")
                self.assertEqual(metadata["packageName"], "Link Control")
                self.assertTrue(metadata["title"])
                for key, value in metadata.items():
                    if not key.startswith("argument"):
                        continue
                    argument = json.loads(value)
                    self.assertTrue(argument["placeholder"])
                    if argument["type"] == "dropdown":
                        options = argument["data"]
                        self.assertEqual(len({option["value"] for option in options}), len(options))
                        for option in options:
                            self.assertTrue(option["title"])
                            extra = ["0"] if script.name == "camera-setting.sh" else []
                            result = self.run_script(script.name, option["value"], *extra)
                            self.assertEqual(result.returncode, 0, result.stderr)
                            expected = (f"setting?id={option['value']}&value=0" if extra
                                        else f"mode?value={option['value']}")
                            self.assertEqual(result.stdout.strip(), f"linkcontrol://action/{expected}")

    def test_numeric_bounds_and_query_injection_are_rejected_before_dispatch(self):
        for value in ["100", "150", "400"]:
            result = self.run_script("camera-zoom.sh", value)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), f"linkcontrol://action/zoom?value={value}")
        for value in ["99", "401", "999999999999999999999", "100.5", "0100", " 100", "-100",
                      "100&value=400", "100#fragment", "100\n400", "$(echo 100)"]:
            with self.subTest(value=value):
                result = self.run_script("camera-zoom.sh", value)
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn("linkcontrol://", result.stdout)
        for value in ["-32768", "-15", "0", "5430", "4294967295"]:
            result = self.run_script("camera-setting.sh", "brightness", value)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), f"linkcontrol://action/setting?id=brightness&value={value}")
        for value in ["-32769", "4294967296", "99999999999999999", "1.0", "01", "-0", "1&cmd=stop", "true"]:
            result = self.run_script("camera-setting.sh", "brightness", value)
            self.assertNotEqual(result.returncode, 0)
            self.assertNotIn("linkcontrol://", result.stdout)
        for name, args in [
            ("camera-mode.sh", ["tracking&value=overhead"]),
            ("camera-setting.sh", ["unknown", "1"]),
            ("camera-setting.sh", ["ai_mode", "5"]),
            ("camera-setting.sh", ["brightness&value=1", "1"]),
            ("camera-zoom.sh", []), ("camera-zoom.sh", ["100", "extra"]),
            ("camera-mode.sh", []), ("camera-mode.sh", ["normal", "extra"]),
            ("camera-setting.sh", ["brightness"]),
        ]:
            self.assertNotEqual(self.run_script(name, *args).returncode, 0)

    def test_relocated_scripts_resolve_helper_in_paths_with_spaces(self):
        with tempfile.TemporaryDirectory(prefix="Link Control Raycast ") as temporary:
            target = Path(temporary) / "App Resources" / "Raycast"
            shutil.copytree(SCRIPTS, target)
            result = self.run_script("camera-zoom.sh", "200", directory=target)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(result.stdout.strip(), "linkcontrol://action/zoom?value=200")


if __name__ == "__main__":
    unittest.main()

"""Exercise the actual ESPHome validation pipeline, including final validation."""

import copy
import os
from pathlib import Path
import subprocess
import tempfile
import unittest

import yaml

ROOT = Path(__file__).resolve().parents[1]
BASE = yaml.safe_load((ROOT / "tests/test-arduino-discovery.yaml").read_text())
BASE["external_components"][0]["source"]["path"] = str(ROOT / "components")


class DiscoveryConfigTest(unittest.TestCase):
    def validate(self, config, valid, message=""):
        with tempfile.TemporaryDirectory(prefix="fastcon-config-") as directory:
            path = Path(directory) / "test.yaml"
            path.write_text(yaml.safe_dump(config))
            result = subprocess.run(
                [os.environ.get("ESPHOME", "esphome"), "config", str(path)],
                text=True, capture_output=True, check=False,
            )
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode == 0, valid, output)
        if message:
            self.assertIn(message, output)

    def test_defaults(self):
        config = copy.deepcopy(BASE)
        del config["fastcon"]["discovery"]
        self.validate(config, True)

    def test_all_existing_options(self):
        config = copy.deepcopy(BASE)
        config["fastcon"].update({
            "adv_interval_min": 32, "adv_interval_max": 64,
            "adv_duration": 100, "adv_gap": 20, "max_queue_size": 1,
            "repeat_count": 5, "tx_power": 9,
        })
        self.validate(config, True)

    def test_ranges(self):
        for field, value in (("max_lights", 0), ("max_lights", 17),
                             ("scan_timeout_s", 0), ("scan_timeout_s", 601)):
            with self.subTest(field=field, value=value):
                config = copy.deepcopy(BASE)
                config["fastcon"]["discovery"][field] = value
                self.validate(config, False)

    def test_slot_bounds_and_duplicates(self):
        for slot in (0, 5, 17, 2):
            with self.subTest(slot=slot):
                config = copy.deepcopy(BASE)
                config["light"][0]["slot"] = slot
                self.validate(config, False)

    def test_slot_is_exclusive(self):
        for field in ("light_id", "group_id"):
            with self.subTest(field=field):
                config = copy.deepcopy(BASE)
                config["light"][0][field] = 1
                self.validate(config, False)

    def test_explicit_key_does_not_enable_discovery(self):
        config = copy.deepcopy(BASE)
        config["fastcon"]["mesh_key"] = "12345678"
        self.validate(config, False, "discovery requires mesh_key: auto")
        del config["fastcon"]["discovery"]
        self.validate(config, False, "slot requires a controller with mesh_key: auto")
        for entry in config["light"]:
            entry["light_id"] = entry.pop("slot")
        self.validate(config, True)

    def test_continuous_scanning_is_rejected(self):
        config = copy.deepcopy(BASE)
        config["esp32_ble_tracker"]["scan_parameters"]["continuous"] = True
        self.validate(config, False, "continuous: false")

    def test_missing_tracker_is_rejected(self):
        config = copy.deepcopy(BASE)
        del config["esp32_ble_tracker"]
        self.validate(config, False)

    def test_slot_group_members(self):
        config = copy.deepcopy(BASE)
        config["light"][0]["id"] = "first_slot"
        config["light"].append({
            "platform": "fastcon", "name": "Slot group", "group_id": 1,
            "members": ["first_slot"],
        })
        self.validate(config, True)

    def test_action_key_is_validated(self):
        for key in ("1234567g", "-1234567", "+1234567", "123_5678"):
            with self.subTest(key=key):
                config = copy.deepcopy(BASE)
                config["button"][0]["on_press"][0]["fastcon.set_mesh_key"]["mesh_key"] = key
                self.validate(config, False)


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""树莓派主机预检的纯判定测试。"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

DEPLOYMENT_ROOT = Path(__file__).resolve().parents[1]
SCRIPT = DEPLOYMENT_ROOT / "healthcheck/check_pi_host.py"
SPEC = importlib.util.spec_from_file_location("check_pi_host", SCRIPT)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
sys.modules[SPEC.name] = MODULE
SPEC.loader.exec_module(MODULE)

GIB = MODULE.GIB
PiFacts = MODULE.PiFacts
evaluate = MODULE.evaluate


def baseline(**overrides):
    values = {
        "arch": "aarch64",
        "model": "Raspberry Pi 5 Model B Rev 1.1",
        "os_id": "debian",
        "version_id": "13",
        "codename": "trixie",
        "memory_kib": 8 * 1024 ** 2,
        "root_bytes": 59 * GIB,
        "docker_available": True,
        "compose_available": True,
        "devices": frozenset({"/dev/i2c-1", "/dev/ttyAMA0"}),
        "registered_devices": frozenset({"/dev/i2c-1", "/dev/ttyAMA0"}),
        "serial_console_args": (),
    }
    values.update(overrides)
    return PiFacts(**values)


class TestBaseStage(unittest.TestCase):
    def test_confirmed_reference_platform_passes(self):
        self.assertTrue(all(item.ok for item in evaluate(baseline(), "base", "car")))

    def test_current_32gb_card_is_rejected(self):
        results = evaluate(baseline(root_bytes=29 * GIB), "base", "car")
        self.assertFalse(next(item for item in results if item.name == "系统盘").ok)

    def test_wrong_architecture_is_rejected(self):
        results = evaluate(baseline(arch="x86_64"), "base", "car")
        self.assertFalse(next(item for item in results if item.name == "架构").ok)

    def test_debian_12_is_not_the_new_baseline(self):
        facts = baseline(version_id="12", codename="bookworm")
        results = evaluate(facts, "base", "car")
        self.assertFalse(next(item for item in results if item.name == "宿主系统").ok)

    def test_lower_memory_variant_is_rejected(self):
        results = evaluate(baseline(memory_kib=4 * 1024 ** 2), "base", "car")
        self.assertFalse(next(item for item in results if item.name == "内存").ok)


class TestDeployStage(unittest.TestCase):
    def test_base_stage_does_not_require_docker_yet(self):
        facts = baseline(docker_available=False, compose_available=False)
        self.assertTrue(all(item.ok for item in evaluate(facts, "base", "car")))

    def test_deploy_stage_requires_docker_and_compose(self):
        facts = baseline(docker_available=False, compose_available=False)
        results = evaluate(facts, "deploy", "drone")
        self.assertFalse(next(item for item in results if item.name == "Docker Engine").ok)
        self.assertFalse(next(item for item in results if item.name == "Docker Compose").ok)

    def test_car_requires_external_i2c_bus(self):
        results = evaluate(
            baseline(
                devices=frozenset({"/dev/ttyAMA0"}),
                registered_devices=frozenset({"/dev/ttyAMA0"}),
            ),
            "deploy",
            "car",
        )
        check = next(item for item in results if item.name == "车机 I²C")
        self.assertFalse(check.ok)
        self.assertIn("启用 dtparam=i2c_arm=on", check.detail)

    def test_drone_does_not_assume_car_i2c_contract(self):
        results = evaluate(
            baseline(devices=frozenset({"/dev/ttyAMA0"})), "deploy", "drone"
        )
        self.assertNotIn("车机 I²C", {item.name for item in results})
        self.assertTrue(all(item.ok for item in results))

    def test_debug_uart_does_not_satisfy_40_pin_header_contract(self):
        facts = baseline(
            devices=frozenset({"/dev/i2c-1", "/dev/ttyAMA10"}),
            registered_devices=frozenset({"/dev/i2c-1"}),
        )
        results = evaluate(facts, "deploy", "car")
        self.assertFalse(next(item for item in results if item.name == "40 针排针 UART").ok)

    def test_registered_but_hidden_devices_report_mount_boundary(self):
        facts = baseline(devices=frozenset())
        results = evaluate(facts, "deploy", "car")
        for name in ("车机 I²C", "40 针排针 UART"):
            check = next(item for item in results if item.name == name)
            self.assertFalse(check.ok)
            self.assertIn("内核已注册", check.detail)
            self.assertIn("mount namespace", check.detail)

    def test_serial_console_is_rejected_at_deploy_stage(self):
        facts = baseline(serial_console_args=("console=serial0,115200",))
        results = evaluate(facts, "deploy", "car")
        check = next(item for item in results if item.name == "串口登录控制台")
        self.assertFalse(check.ok)
        self.assertIn("console=serial0,115200", check.detail)

    def test_base_stage_does_not_require_serial_console_removal(self):
        facts = baseline(serial_console_args=("console=ttyAMA10,115200",))
        self.assertTrue(all(item.ok for item in evaluate(facts, "base", "car")))


class TestSerialConsoleParsing(unittest.TestCase):
    def test_only_serial_console_arguments_are_returned(self):
        cmdline = (
            "console=tty1 root=/dev/mmcblk0p2 console=serial0,115200 "
            "console=ttyAMA0,9600 console=ttyS1"
        )
        self.assertEqual(
            MODULE.find_serial_console_args(cmdline),
            ("console=serial0,115200", "console=ttyAMA0,9600", "console=ttyS1"),
        )


class TestJsonOutput(unittest.TestCase):
    def test_device_sets_become_sorted_lists(self):
        facts = baseline(
            devices=frozenset({"/dev/ttyAMA0", "/dev/i2c-1"}),
            registered_devices=frozenset({"/dev/ttyAMA0", "/dev/i2c-1"}),
        )
        values = MODULE.facts_for_json(facts)
        expected = ["/dev/i2c-1", "/dev/ttyAMA0"]
        self.assertEqual(values["devices"], expected)
        self.assertEqual(values["registered_devices"], expected)


class TestHostUserContract(unittest.TestCase):
    def test_installer_resolves_numeric_uid_instead_of_username(self):
        text = (DEPLOYMENT_ROOT / "install.sh").read_text(encoding="utf-8")
        self.assertIn("HOST_UID=1000", text)
        self.assertIn('getent passwd "${HOST_UID}"', text)
        self.assertIn('-o "${HOST_UID}"', text)
        self.assertNotIn("-o airground", text)

    def test_edge_and_health_units_use_numeric_uid(self):
        units = (
            "air-ground-car-edge.service",
            "air-ground-drone-edge.service",
            "air-ground-healthcheck.service",
        )
        for filename in units:
            with self.subTest(unit=filename):
                text = (DEPLOYMENT_ROOT / "systemd" / filename).read_text(
                    encoding="utf-8"
                )
                self.assertIn("User=1000", text)
                self.assertNotIn("User=airground", text)


if __name__ == "__main__":
    unittest.main()

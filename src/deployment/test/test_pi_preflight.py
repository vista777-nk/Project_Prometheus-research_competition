#!/usr/bin/env python3
"""树莓派主机预检的纯判定测试。"""

from __future__ import annotations

import importlib.util
import sys
import unittest
from pathlib import Path

SCRIPT = Path(__file__).resolve().parents[1] / "healthcheck/check_pi_host.py"
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
        "devices": frozenset({"/dev/i2c-1"}),
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
        results = evaluate(baseline(devices=frozenset()), "deploy", "car")
        self.assertFalse(next(item for item in results if item.name == "车机 I²C").ok)

    def test_drone_does_not_assume_car_i2c_contract(self):
        results = evaluate(baseline(devices=frozenset()), "deploy", "drone")
        self.assertNotIn("车机 I²C", {item.name for item in results})
        self.assertTrue(all(item.ok for item in results))


if __name__ == "__main__":
    unittest.main()

#!/usr/bin/env python3
"""验证容器入口的 real/mock/sim 路由与失败关闭语义。

测试运行真实 ``entrypoint.sh``，只替换 ROS 环境脚本和两个外部命令。
被测对象仍是生产入口本身；替身只替环境（ADR-0011 §方案 G）。
"""

from __future__ import annotations

import os
import stat
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[3]
ENTRYPOINT = REPO_ROOT / "src/deployment/docker/entrypoint.sh"
CAR_PACKAGE = REPO_ROOT / "src/air_ground_car_bringup"
REAL_LAUNCH = CAR_PACKAGE / "launch/car_edge_real.launch"


class TestEntrypointModes(unittest.TestCase):
    """真实执行 entrypoint，只把 rospack/roslaunch 替换成记录器。"""

    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="ag-entrypoint-")
        self.root = Path(self.temp.name)
        self.bin_dir = self.root / "bin"
        self.bin_dir.mkdir()
        self.capture = self.root / "roslaunch.args"
        self.state_dir = self.root / "state"

        self._write_executable(
            "rospack",
            """#!/bin/sh
if [ "$1" = "find" ] && [ -n "${AIR_GROUND_TEST_PACKAGE:-}" ]; then
    printf '%s\\n' "$AIR_GROUND_TEST_PACKAGE"
    exit 0
fi
exit 1
""",
        )
        self._write_executable(
            "roslaunch",
            """#!/bin/sh
printf '%s\\n' "$@" > "$AIR_GROUND_TEST_CAPTURE"
""",
        )

    def tearDown(self) -> None:
        self.temp.cleanup()

    def _write_executable(self, name: str, content: str) -> None:
        path = self.bin_dir / name
        path.write_text(content, encoding="utf-8")
        path.chmod(path.stat().st_mode | stat.S_IXUSR)

    def _run(
        self,
        mode: str,
        role: str = "car",
        package: Path = CAR_PACKAGE,
        ros_ip: str | None = "192.0.2.10",
    ) -> subprocess.CompletedProcess:
        env = os.environ.copy()
        env.update(
            {
                "PATH": str(self.bin_dir) + os.pathsep + env.get("PATH", ""),
                "AIR_GROUND_ROLE": role,
                "AIR_GROUND_CHASSIS": "diff",
                "EDGE_MODE": mode,
                "AIR_GROUND_ROS_SETUP": "/dev/null",
                "AIR_GROUND_WS_SETUP": "/dev/null",
                "AIR_GROUND_STATE_DIR": str(self.state_dir),
                "AIR_GROUND_TEST_PACKAGE": str(package),
                "AIR_GROUND_TEST_CAPTURE": str(self.capture),
            }
        )
        if ros_ip is None:
            env.pop("ROS_IP", None)
        else:
            env["ROS_IP"] = ros_ip
        return subprocess.run(
            ["bash", str(ENTRYPOINT)],
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
            check=False,
        )

    def _args(self) -> list[str]:
        return self.capture.read_text(encoding="utf-8").splitlines()

    def _state(self) -> str:
        return (self.state_dir / "edge-state.env").read_text(encoding="utf-8")

    def test_real_mode_passes_real_backend_and_is_not_degraded(self) -> None:
        result = self._run("real")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self._args(),
            [
                "air_ground_car_bringup",
                "car_edge_real.launch",
                "default_chassis:=diff",
                "backend:=real",
            ],
        )
        self.assertIn("AIR_GROUND_DEGRADED=0", self._state())
        self.assertIn("AIR_GROUND_SENSOR_BACKEND=real", self._state())

    def test_mock_mode_is_explicitly_degraded(self) -> None:
        result = self._run("mock")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self._args()[-1], "backend:=mock")
        state = self._state()
        self.assertIn("AIR_GROUND_DEGRADED=1", state)
        self.assertIn("EDGE_MODE=mock", state)
        self.assertIn("不得作为实机证据", state)

    def test_sim_mode_uses_simulation_launch_without_sensor_backend(self) -> None:
        result = self._run("sim")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self._args(),
            [
                "air_ground_car_bringup",
                "car_edge.launch",
                "default_chassis:=diff",
            ],
        )
        self.assertIn("AIR_GROUND_DEGRADED=0", self._state())

    def test_unknown_car_mode_is_rejected_before_roslaunch(self) -> None:
        result = self._run("production")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("只接受 real、mock 或 sim", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_missing_ros_ip_is_rejected_before_roslaunch(self) -> None:
        result = self._run("real", ros_ip=None)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("ROS_IP 未设置", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_real_mode_never_falls_back_when_launch_is_missing(self) -> None:
        empty_package = self.root / "empty-package"
        empty_package.mkdir()
        result = self._run("real", package=empty_package)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("不会自动降级", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_drone_rejects_mock_mode(self) -> None:
        result = self._run("mock", role="drone")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("不支持 EDGE_MODE=mock", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_drone_real_uses_hardware_launch_and_d435i(self) -> None:
        result = self._run("real", role="drone", package=CAR_PACKAGE)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(
            self._args(),
            [
                "air_ground_drone_bringup",
                "drone-edge-real.launch",
                "vision_mode:=d435i",
            ],
        )
        self.assertIn("AIR_GROUND_DRONE_VISION=d435i", self._state())

    def test_drone_pi_dual_fails_closed_until_camera_profile_is_known(self) -> None:
        old = os.environ.get("DRONE_VISION")
        os.environ["DRONE_VISION"] = "pi_dual"
        try:
            result = self._run("real", role="drone", package=CAR_PACKAGE)
        finally:
            if old is None:
                os.environ.pop("DRONE_VISION", None)
            else:
                os.environ["DRONE_VISION"] = old
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("尚未具备可验证的实机驱动配置", result.stderr)
        self.assertFalse(self.capture.exists())

    def test_real_launch_defaults_to_real_backend(self) -> None:
        root = ET.parse(str(REAL_LAUNCH)).getroot()
        backend = root.find("arg[@name='backend']")
        self.assertIsNotNone(backend)
        self.assertEqual(backend.attrib.get("default"), "real")


if __name__ == "__main__":
    unittest.main()

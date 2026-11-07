#!/usr/bin/env python3
"""验证实验室服务器常驻入口、健康判定与安全默认值。"""

import importlib.util
import os
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from pathlib import Path
from unittest import mock


REPO_ROOT = Path(__file__).resolve().parents[3]
DEPLOYMENT = REPO_ROOT / "src/deployment"
HEALTH_SCRIPT = DEPLOYMENT / "server/check_lab_server.py"
LAUNCH_SCRIPT = DEPLOYMENT / "server/launch_lab_server.sh"
SERVER_LAUNCH = REPO_ROOT / "src/air_ground_bringup/launch/lab-server-real.launch"
SERVER_CONFIG = REPO_ROOT / "src/air_ground_com_bridge/config/network_server.yaml"
SERVICE = DEPLOYMENT / "systemd/air-ground-lab-server@.service"
USER_SERVICE = DEPLOYMENT / "systemd/user/air-ground-lab-server.service"


def load_health_module():
    """按文件路径加载健康检查，测试生产脚本本身。"""
    spec = importlib.util.spec_from_file_location("check_lab_server", HEALTH_SCRIPT)
    module = importlib.util.module_from_spec(spec)
    assert spec.loader is not None
    spec.loader.exec_module(module)
    return module


health = load_health_module()


class ServerHealthTest(unittest.TestCase):
    """验证节点集合与 TCP 状态的联合判定。"""

    def test_expected_nodes_and_tcp_are_healthy(self) -> None:
        result = health.evaluate(health.REQUIRED_NODES, True)
        self.assertTrue(result["ok"])
        self.assertEqual(result["missing_nodes"], [])
        self.assertEqual(result["forbidden_nodes"], [])

    def test_missing_node_fails(self) -> None:
        nodes = health.REQUIRED_NODES - {"/tcp_server"}
        result = health.evaluate(nodes, True)
        self.assertFalse(result["ok"])
        self.assertEqual(result["missing_nodes"], ["/tcp_server"])

    def test_local_edge_client_is_rejected(self) -> None:
        nodes = health.REQUIRED_NODES | {"/edge_server_bridge"}
        result = health.evaluate(nodes, True)
        self.assertFalse(result["ok"])
        self.assertEqual(result["forbidden_nodes"], ["/edge_server_bridge"])

    def test_check_once_combines_real_probes(self) -> None:
        with mock.patch.object(
            health, "registered_nodes", return_value=health.REQUIRED_NODES
        ), mock.patch.object(health, "tcp_accepts", return_value=True):
            result = health.check_once(
                "http://127.0.0.1:11311", "127.0.0.1", 9090
            )
        self.assertTrue(result["ok"])
        self.assertEqual(result["tcp_endpoint"], "127.0.0.1:9090")


class ServerDeploymentContractTest(unittest.TestCase):
    """锁住跨校区安全默认、required 节点和 systemd 恢复语义。"""

    def test_server_network_defaults_to_loopback(self) -> None:
        text = SERVER_CONFIG.read_text(encoding="utf-8")
        self.assertIn("server_ip: 127.0.0.1", text)
        self.assertIn("server_bind_ip: 127.0.0.1", text)
        self.assertNotIn("server_bind_ip: 0.0.0.0", text)

    def test_real_launch_requires_all_server_nodes(self) -> None:
        root = ET.parse(str(SERVER_LAUNCH)).getroot()
        include = root.find("include")
        self.assertIsNotNone(include)
        required = include.find("arg[@name='required_nodes']")
        self.assertIsNotNone(required)
        self.assertEqual(required.attrib["value"], "true")

    def test_systemd_restarts_and_post_checks(self) -> None:
        text = SERVICE.read_text(encoding="utf-8")
        self.assertIn("Restart=always", text)
        self.assertIn("ExecStartPost=/usr/local/lib/air-ground/check_lab_server.py", text)
        self.assertIn("ReadWritePaths=/data2/air-ground-server", text)

    def test_user_systemd_has_same_recovery_contract(self) -> None:
        text = USER_SERVICE.read_text(encoding="utf-8")
        self.assertIn("Restart=always", text)
        self.assertIn("ExecStartPost=%h/.local/lib/air-ground/check_lab_server.py", text)
        self.assertNotIn("User=", text)

    def test_launcher_rejects_remote_ros_by_default(self) -> None:
        with tempfile.TemporaryDirectory(prefix="ag-server-launch-") as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "src/air_ground_com_bridge/config").mkdir(parents=True)
            (root / "src/air_ground_lab_server/config").mkdir(parents=True)
            (root / "scripts/setup_runtime.sh").write_text("true\n", encoding="utf-8")
            (root / "src/air_ground_com_bridge/config/network_server.yaml").write_text(
                "edge_server_tcp: {}\n", encoding="utf-8"
            )
            (root / "src/air_ground_lab_server/config/server_params.yaml").write_text(
                "tcp: {}\n", encoding="utf-8"
            )
            log_dir = root / "logs"
            log_dir.mkdir()
            env = os.environ.copy()
            env.update(
                {
                    "AIR_GROUND_REPO": str(root),
                    "ROS_IP": "192.0.2.10",
                    "ROS_MASTER_URI": "http://192.0.2.10:11311",
                    "ROS_LOG_DIR": str(log_dir),
                }
            )
            result = subprocess.run(
                ["bash", str(LAUNCH_SCRIPT)],
                env=env,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                text=True,
                check=False,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("默认拒绝暴露 ROS", result.stderr)


if __name__ == "__main__":
    unittest.main()

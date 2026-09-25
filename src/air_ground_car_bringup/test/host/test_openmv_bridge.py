#!/usr/bin/env python3
"""OpenMV 桥接骨架单元测试 —— 无 ROS、无 roscore、无硬件。"""

import json
import unittest

from fixtures import configure
from mock_hardware import MockUART
from openmv_bridge import OpenMVBridge, parse_detection_line, sanitize_object

VALID_OBJECT = {
    "label": "red_ball", "x": 120, "y": 80, "w": 30, "h": 30, "conf": 0.95,
}


def encode(objects) -> bytes:
    """按约定协议打包一行（含帧分隔符）。"""
    return (json.dumps({"objects": objects}) + "\n").encode("utf-8")


class SanitizeObjectTest(unittest.TestCase):
    """字段约定见 ICD 对齐说明：label/x/y/w/h/conf，图像左上角为原点。"""

    def test_accepts_well_formed_object(self):
        self.assertEqual(sanitize_object(dict(VALID_OBJECT)), VALID_OBJECT)

    def test_rejects_missing_field(self):
        broken = dict(VALID_OBJECT)
        del broken["conf"]
        self.assertIsNone(sanitize_object(broken))

    def test_rejects_out_of_unit_confidence(self):
        for value in (-0.1, 1.5):
            broken = dict(VALID_OBJECT, conf=value)
            self.assertIsNone(sanitize_object(broken))

    def test_rejects_degenerate_box(self):
        self.assertIsNone(sanitize_object(dict(VALID_OBJECT, w=0)))
        self.assertIsNone(sanitize_object(dict(VALID_OBJECT, h=-5)))

    def test_rejects_empty_label(self):
        self.assertIsNone(sanitize_object(dict(VALID_OBJECT, label="")))

    def test_rejects_non_numeric_coordinates(self):
        self.assertIsNone(sanitize_object(dict(VALID_OBJECT, x="left")))


class ParseDetectionLineTest(unittest.TestCase):
    """整行不可解析返回 None；单个目标坏掉只剔除它自己。"""

    def test_parses_object_list(self):
        payload = parse_detection_line(json.dumps(
            {"objects": [VALID_OBJECT]}).encode("utf-8"))
        self.assertEqual(payload, {"objects": [VALID_OBJECT]})

    def test_drops_only_the_broken_object(self):
        raw = json.dumps({"objects": [VALID_OBJECT, {"label": "x"}]})
        payload = parse_detection_line(raw.encode("utf-8"))
        self.assertEqual(payload, {"objects": [VALID_OBJECT]})

    def test_rejects_non_json(self):
        self.assertIsNone(parse_detection_line(b"not json at all"))

    def test_rejects_wrong_shape(self):
        self.assertIsNone(parse_detection_line(b'{"objects": 3}'))
        self.assertIsNone(parse_detection_line(b'[1, 2, 3]'))

    def test_rejects_invalid_utf8(self):
        self.assertIsNone(parse_detection_line(b'{"objects": [\xff\xfe]}'))


class OpenMVBridgeTest(unittest.TestCase):
    """桥接整体：参数取自 real_sensors.yaml，字节取自 MockUART。"""

    def setUp(self):
        self.rospy = configure()

    def test_publishes_one_message_per_line(self):
        stream = encode([VALID_OBJECT]) * 3
        bridge = OpenMVBridge(MockUART(stream))
        bridge.read_chunk = len(stream)
        messages = bridge.step()
        self.assertEqual(len(messages), 3)
        self.assertEqual(
            json.loads(messages[0].data), {"objects": [VALID_OBJECT]}
        )

    def test_partial_line_waits_for_delimiter(self):
        """一行跨多次 read 到达时，前半行不能被当成一帧丢掉。"""
        line = encode([VALID_OBJECT])
        uart = MockUART(line[:10])
        bridge = OpenMVBridge(uart)
        bridge.read_chunk = 10
        self.assertEqual(bridge.step(), [])
        uart.feed(line[10:])
        messages = []
        for _ in range(len(line) // 10 + 2):
            messages.extend(bridge.step())
        self.assertEqual(len(messages), 1)
        self.assertEqual(
            json.loads(messages[0].data), {"objects": [VALID_OBJECT]}
        )

    def test_topic_name_from_config(self):
        bridge = OpenMVBridge(MockUART())
        self.assertEqual(bridge.publisher.name, "/car/openmv/detections")

    def test_unbounded_garbage_does_not_grow_the_buffer(self):
        """对端乱发且不带分隔符时，缓冲区必须封顶 —— 否则树莓派会被吃光内存。"""
        garbage = b"x" * (4096 + 512)
        bridge = OpenMVBridge(MockUART(garbage))
        bridge.read_chunk = len(garbage)
        self.assertEqual(bridge.step(), [])
        self.assertEqual(bridge._buffer, b"")
        self.assertTrue(
            any("丢弃缓冲" in message for _level, message in self.rospy.logs)
        )

    def test_bad_line_is_dropped_not_fatal(self):
        stream = b"garbage\n" + encode([VALID_OBJECT])
        bridge = OpenMVBridge(MockUART(stream))
        bridge.read_chunk = len(stream)
        messages = bridge.step()
        self.assertEqual(len(messages), 1)
        self.assertTrue(
            any("无法解析" in message for _level, message in self.rospy.logs)
        )

    def test_open_failure_does_not_raise(self):
        class FailingUART(MockUART):
            def open(self, port, baudrate):
                return False

        bridge = OpenMVBridge(FailingUART())
        self.assertEqual(bridge.step(), [])

    def test_runtime_read_failure_triggers_reconnect(self):
        """运行中串口异常只能断开并退避，不能杀掉整个节点。"""

        class UnpluggedUART(MockUART):
            def read(self, n, timeout_ms=100.0):
                raise OSError(5, "Input/output error")

        uart = UnpluggedUART()
        bridge = OpenMVBridge(uart)
        self.assertTrue(bridge.connect())
        self.assertEqual(bridge.step(), [])
        self.assertFalse(uart.is_open)
        self.assertTrue(
            any("读取失败" in message for _level, message in self.rospy.logs)
        )

    def test_stall_triggers_reconnect(self):
        uart = MockUART()
        bridge = OpenMVBridge(uart)
        self.assertTrue(bridge.connect())
        bridge._last_data_at -= bridge.data_timeout + 1.0
        self.assertEqual(bridge.step(), [])
        self.assertFalse(uart.is_open)


if __name__ == "__main__":
    unittest.main()

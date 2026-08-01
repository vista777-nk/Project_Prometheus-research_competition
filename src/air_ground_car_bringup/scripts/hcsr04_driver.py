#!/usr/bin/env python3
"""HC-SR04 超声波驱动骨架 (4 路) —— 接口层与量纲换算完整，GPIO 时序待实机填。

发布: /car/ultrasonic/{front,rear,left,right} (sensor_msgs/LaserScan)
配置: config/real_sensors.yaml §hcsr04

**消息类型是 LaserScan 不是 Range**：仿真侧四路超声波是用
`libgazebo_ros_laser.so` 的单束射线实现的，`car_preprocessor.py` 订的也是
LaserScan。ROS 订阅端类型不匹配时不会报错，只是**永远连不上** ——
实机上表现为 Observation 里恒定没有 ultrasonic 模态。语义上 Range 更贴切，
但那要连仿真 URDF 和预处理器一起改，不属于驱动骨架的范围（见 ADR-0009 §待决）。

四路**轮流**触发，不并发：HC-SR04 的 40kHz 脉冲互相听得见，同时发射会让
相邻两路各自收到对方的回波（串扰），量出的距离偏小且不稳定。
"""

import os
import sys
from typing import Dict, List, Optional, Tuple

import rospy
from sensor_msgs.msg import LaserScan

# 见 rplidar_driver.py 同处注释：避开 catkin devel relay 的同名自导入。
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from hardware_interface import GPIOInterface, create_gpio  # noqa: E402
from sensor_config import (  # noqa: E402
    load_section,
    positive_float,
    require_keys,
    resolve_backend,
)

SECTION = "hcsr04"
REQUIRED_KEYS = (
    "topic_prefix", "frame_id_template", "directions", "pins",
    "update_rate", "min_range", "max_range",
    "trigger_pulse_us", "echo_timeout_us", "speed_of_sound",
)


def echo_to_distance(duration_us: float, speed_of_sound: float) -> float:
    """回波宽度 (μs) → 距离 (m)。超时 (负值) 返回 inf。

    声波走的是往返，所以要除以 2。

    Args:
        duration_us: 回波高电平持续时间，负值表示超时。
        speed_of_sound: 声速 (m/s)。随温度变化约 0.6 m/s/°C，
            20°C 时 343 m/s。实机标定见 task-15。

    Returns:
        距离 (m)；超时返回 float('inf')。
    """
    if duration_us < 0.0:
        return float("inf")
    return duration_us * 1e-6 * speed_of_sound / 2.0


class HCSR04Driver:
    """HC-SR04 ×4 驱动。通过依赖注入 GPIOInterface 与硬件解耦。"""

    def __init__(self, gpio: GPIOInterface) -> None:
        """读取 `~hcsr04` 配置段、建立 4 个发布器。"""
        config = load_section(SECTION)
        require_keys(config, REQUIRED_KEYS, "~" + SECTION)

        self.gpio = gpio
        self.directions: List[str] = [str(d) for d in config["directions"]]
        if not self.directions:
            raise ValueError("~hcsr04/directions must not be empty")
        pins = dict(config["pins"])
        missing = sorted(set(self.directions) - set(pins))
        if missing:
            raise ValueError(f"~hcsr04/pins is missing: {', '.join(missing)}")
        self.pins: Dict[str, Dict[str, int]] = {
            name: {"trig": int(pins[name]["trig"]), "echo": int(pins[name]["echo"])}
            for name in self.directions
        }
        self.min_range = positive_float(config["min_range"], "hcsr04/min_range")
        self.max_range = positive_float(config["max_range"], "hcsr04/max_range")
        if self.max_range <= self.min_range:
            raise ValueError("~hcsr04/max_range must exceed min_range")
        self.update_rate = positive_float(config["update_rate"], "hcsr04/update_rate")
        self.trigger_pulse_us = positive_float(
            config["trigger_pulse_us"], "hcsr04/trigger_pulse_us"
        )
        self.echo_timeout_us = positive_float(
            config["echo_timeout_us"], "hcsr04/echo_timeout_us"
        )
        self.speed_of_sound = positive_float(
            config["speed_of_sound"], "hcsr04/speed_of_sound"
        )
        self.frame_id_template = str(config["frame_id_template"])

        prefix = str(config["topic_prefix"]).rstrip("/")
        self.publishers = {
            name: rospy.Publisher(f"{prefix}/{name}", LaserScan, queue_size=5)
            for name in self.directions
        }
        self._cursor = 0
        self._configured = False

    def setup_pins(self) -> None:
        """配置 8 个引脚方向。Trig 拉低作为初始态。"""
        for name in self.directions:
            trig = self.pins[name]["trig"]
            echo = self.pins[name]["echo"]
            self.gpio.setup(trig, "out")
            self.gpio.setup(echo, "in")
            self.gpio.write(trig, 0)
        self._configured = True
        rospy.loginfo(
            "[hcsr04_driver] %d 路超声波就绪: %s",
            len(self.directions), ", ".join(self.directions),
        )

    def measure(self, name: str) -> float:
        """测一路距离 (m)。超时或越界返回 inf。"""
        pins = self.pins[name]
        duration_us = self.gpio.trigger_and_measure(
            pins["trig"], pins["echo"],
            self.trigger_pulse_us, self.echo_timeout_us,
        )
        distance = echo_to_distance(duration_us, self.speed_of_sound)
        # 越界读数记为 inf 而不是夹到边界值: HC-SR04 在 4m 外返回的是噪声,
        # 夹成 4.0 会让下游把"什么都没测到"当成"4 米处有墙"。
        if not self.min_range <= distance <= self.max_range:
            return float("inf")
        return distance

    def build_scan(self, name: str, distance: float) -> LaserScan:
        """把单束读数装配为 LaserScan（与仿真侧单束射线一致）。"""
        scan = LaserScan()
        scan.header.stamp = rospy.Time.now()
        scan.header.frame_id = self.frame_id_template.format(name=name)
        scan.angle_min = 0.0
        scan.angle_max = 0.0
        scan.angle_increment = 0.0
        scan.time_increment = 0.0
        scan.scan_time = 1.0 / self.update_rate
        scan.range_min = self.min_range
        scan.range_max = self.max_range
        scan.ranges = [distance]
        scan.intensities = []
        return scan

    def step(self) -> Optional[Tuple[str, LaserScan]]:
        """轮流触发下一路，返回 (方向, 消息)。"""
        if not self._configured:
            self.setup_pins()
        name = self.directions[self._cursor % len(self.directions)]
        self._cursor += 1
        try:
            distance = self.measure(name)
        except (OSError, RuntimeError) as error:
            rospy.logwarn_throttle(
                5.0, "[hcsr04_driver] %s 路测距失败: %s", name, error
            )
            return None
        return name, self.build_scan(name, distance)

    def run(self) -> None:
        """阻塞式主循环。循环频率 = 单路频率 × 路数（轮流触发）。"""
        rate = rospy.Rate(self.update_rate * len(self.directions))
        while not rospy.is_shutdown():
            result = self.step()
            if result is not None:
                name, scan = result
                self.publishers[name].publish(scan)
            rate.sleep()


def main() -> None:
    """启动 HC-SR04 驱动节点。"""
    rospy.init_node("hcsr04_driver")
    driver = HCSR04Driver(create_gpio(resolve_backend()))
    driver.run()


if __name__ == "__main__":
    main()

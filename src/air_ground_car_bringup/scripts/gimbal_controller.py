#!/usr/bin/env python3
"""Clamp stable gimbal commands and replay them across chassis swaps."""

import math
import threading
from typing import Tuple

import rospy
from std_msgs.msg import Float64


def validate_limits(lower: float, upper: float) -> Tuple[float, float]:
    """Return finite ordered limits or raise ValueError."""
    if not all(math.isfinite(value) for value in (lower, upper)):
        raise ValueError("gimbal limits must be finite")
    if lower >= upper:
        raise ValueError("gimbal lower limit must be less than upper limit")
    return lower, upper


def clamp_command(value: float, lower: float, upper: float) -> float:
    """Clamp a finite gimbal command to an ordered interval."""
    validate_limits(lower, upper)
    if not math.isfinite(value):
        raise ValueError("gimbal command must be finite")
    return max(lower, min(upper, value))


class GimbalController:
    """Publish bounded pan and tilt targets continuously."""

    def __init__(self) -> None:
        self.pan_limits = validate_limits(
            float(rospy.get_param("~gimbal/pan_min", -math.pi / 2.0)),
            float(rospy.get_param("~gimbal/pan_max", math.pi / 2.0)),
        )
        self.tilt_limits = validate_limits(
            float(rospy.get_param("~gimbal/tilt_min", -math.pi / 4.0)),
            float(rospy.get_param("~gimbal/tilt_max", math.pi / 4.0)),
        )
        publish_rate = float(
            rospy.get_param("~gimbal/publish_rate", 20.0)
        )
        if not math.isfinite(publish_rate) or publish_rate <= 0.0:
            raise ValueError("gimbal publish rate must be positive")

        self.lock = threading.Lock()
        self.pan_target = 0.0
        self.tilt_target = 0.0
        self.pan_publisher = rospy.Publisher(
            "/car/gimbal_pan_controller/command",
            Float64,
            queue_size=10,
        )
        self.tilt_publisher = rospy.Publisher(
            "/car/gimbal_tilt_controller/command",
            Float64,
            queue_size=10,
        )
        self.pan_subscriber = rospy.Subscriber(
            "/car/gimbal/pan/command",
            Float64,
            self.pan_callback,
            queue_size=10,
        )
        self.tilt_subscriber = rospy.Subscriber(
            "/car/gimbal/tilt/command",
            Float64,
            self.tilt_callback,
            queue_size=10,
        )
        self.publish_timer = rospy.Timer(
            rospy.Duration(1.0 / publish_rate),
            self.publish_targets,
        )
        rospy.loginfo(
            "[gimbal_controller] pan=[%.3f, %.3f], tilt=[%.3f, %.3f]",
            self.pan_limits[0],
            self.pan_limits[1],
            self.tilt_limits[0],
            self.tilt_limits[1],
        )

    def update_target(
        self,
        axis: str,
        value: float,
        limits: Tuple[float, float],
    ) -> None:
        try:
            target = clamp_command(value, limits[0], limits[1])
        except ValueError as error:
            rospy.logwarn_throttle(
                5.0, "[gimbal_controller] rejected %s command: %s",
                axis, error
            )
            return
        with self.lock:
            if axis == "pan":
                self.pan_target = target
            else:
                self.tilt_target = target

    def pan_callback(self, message: Float64) -> None:
        self.update_target("pan", message.data, self.pan_limits)

    def tilt_callback(self, message: Float64) -> None:
        self.update_target("tilt", message.data, self.tilt_limits)

    def publish_targets(self, _event: rospy.timer.TimerEvent) -> None:
        with self.lock:
            pan_target = self.pan_target
            tilt_target = self.tilt_target
        self.pan_publisher.publish(Float64(pan_target))
        self.tilt_publisher.publish(Float64(tilt_target))


def main() -> None:
    rospy.init_node("gimbal_controller")
    GimbalController()
    rospy.spin()


if __name__ == "__main__":
    main()

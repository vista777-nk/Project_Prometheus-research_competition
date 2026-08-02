#!/usr/bin/env python3
"""Convert chassis Twist commands to mecanum wheel speeds and odometry."""

import math
import threading
from typing import Dict, Iterable, Tuple

import rospy
import tf2_ros
from gazebo_msgs.msg import ModelStates
from geometry_msgs.msg import TransformStamped, Twist
from nav_msgs.msg import Odometry
from sensor_msgs.msg import JointState
from std_msgs.msg import Float64


WHEEL_NAMES = (
    "front_left",
    "front_right",
    "rear_left",
    "rear_right",
)


def _validate_geometry(
    wheel_base: float, track_width: float, wheel_radius: float
) -> None:
    values = (wheel_base, track_width, wheel_radius)
    if not all(math.isfinite(value) and value > 0.0 for value in values):
        raise ValueError("mecanum geometry values must be finite and positive")


def inverse_kinematics(
    linear_x: float,
    linear_y: float,
    angular_z: float,
    wheel_base: float,
    track_width: float,
    wheel_radius: float,
    max_wheel_speed: float,
) -> Tuple[float, float, float, float]:
    """Return FL, FR, RL and RR wheel angular velocities in rad/s."""
    _validate_geometry(wheel_base, track_width, wheel_radius)
    command = (linear_x, linear_y, angular_z, max_wheel_speed)
    if not all(math.isfinite(value) for value in command):
        raise ValueError("mecanum command and speed limit must be finite")
    if max_wheel_speed <= 0.0:
        raise ValueError("max wheel speed must be positive")

    lever_arm = (wheel_base + track_width) / 2.0
    wheel_speeds = (
        (linear_x - linear_y - lever_arm * angular_z) / wheel_radius,
        (linear_x + linear_y + lever_arm * angular_z) / wheel_radius,
        (linear_x + linear_y - lever_arm * angular_z) / wheel_radius,
        (linear_x - linear_y + lever_arm * angular_z) / wheel_radius,
    )

    peak_speed = max(abs(speed) for speed in wheel_speeds)
    if peak_speed <= max_wheel_speed:
        return wheel_speeds

    scale = max_wheel_speed / peak_speed
    return tuple(speed * scale for speed in wheel_speeds)


def forward_kinematics(
    wheel_speeds: Iterable[float],
    wheel_base: float,
    track_width: float,
    wheel_radius: float,
) -> Tuple[float, float, float]:
    """Return body-frame vx, vy and yaw rate from wheel angular velocities."""
    _validate_geometry(wheel_base, track_width, wheel_radius)
    speeds = tuple(float(speed) for speed in wheel_speeds)
    if len(speeds) != 4 or not all(math.isfinite(speed) for speed in speeds):
        raise ValueError("exactly four finite wheel speeds are required")

    front_left, front_right, rear_left, rear_right = speeds
    lever_arm = (wheel_base + track_width) / 2.0
    linear_x = wheel_radius * (
        front_left + front_right + rear_left + rear_right
    ) / 4.0
    linear_y = wheel_radius * (
        -front_left + front_right + rear_left - rear_right
    ) / 4.0
    angular_z = wheel_radius * (
        -front_left + front_right - rear_left + rear_right
    ) / (4.0 * lever_arm)
    return linear_x, linear_y, angular_z


class MecanumController:
    """ROS adapter for mecanum wheel commands and selectable odometry."""

    def __init__(self) -> None:
        self.wheel_base = float(rospy.get_param("~wheel_base", 0.20))
        self.track_width = float(rospy.get_param("~track_width", 0.18))
        self.wheel_radius = float(rospy.get_param("~wheel_radius", 0.040))
        max_rpm = float(rospy.get_param("~max_rpm", 300.0))
        self.command_timeout = rospy.Duration(
            float(rospy.get_param("~command_timeout", 0.5))
        )
        self.odom_frame = str(rospy.get_param("~odom_frame", "odom"))
        self.base_frame = str(rospy.get_param("~base_frame", "base_link"))
        self.gazebo_model_name = str(
            rospy.get_param("~gazebo_model_name", "mecanum_car")
        )
        self.odometry_source = str(
            rospy.get_param("~odometry_source", "encoder")
        ).lower()
        self.publish_tf = bool(rospy.get_param("~publish_tf", True))

        _validate_geometry(
            self.wheel_base, self.track_width, self.wheel_radius
        )
        if not math.isfinite(max_rpm) or max_rpm <= 0.0:
            raise ValueError("max_rpm must be finite and positive")
        if self.command_timeout.to_sec() <= 0.0:
            raise ValueError("command_timeout must be positive")
        if self.odometry_source not in {"encoder", "gazebo", "none"}:
            raise ValueError(
                "odometry_source must be encoder, gazebo or none"
            )
        self.max_wheel_speed = max_rpm * 2.0 * math.pi / 60.0

        self.command_publishers: Dict[str, rospy.Publisher] = {
            name: rospy.Publisher(
                f"/car/{name}_wheel_controller/command",
                Float64,
                queue_size=10,
            )
            for name in WHEEL_NAMES
        }
        self.base_command_publisher = rospy.Publisher(
            "/car/mecanum_cmd_vel", Twist, queue_size=10
        )
        self.odom_publisher = None
        self.tf_broadcaster = None
        if self.odometry_source != "none":
            self.odom_publisher = rospy.Publisher(
                "/car/odom", Odometry, queue_size=20
            )
            self.tf_broadcaster = tf2_ros.TransformBroadcaster()

        self.lock = threading.RLock()
        self.last_command_time = rospy.Time.now()
        self.last_joint_stamp = None
        self.x = 0.0
        self.y = 0.0
        self.yaw = 0.0

        self.command_subscriber = rospy.Subscriber(
            "/car/cmd_vel", Twist, self.command_callback, queue_size=10
        )
        self.joint_subscriber = None
        self.model_states_subscriber = None
        if self.odometry_source == "encoder":
            self.joint_subscriber = rospy.Subscriber(
                "/car/joint_states",
                JointState,
                self.joint_state_callback,
                queue_size=20,
            )
        elif self.odometry_source == "gazebo":
            self.model_states_subscriber = rospy.Subscriber(
                "/gazebo/model_states",
                ModelStates,
                self.model_states_callback,
                queue_size=10,
            )
        self.watchdog_timer = rospy.Timer(
            rospy.Duration(0.1), self.watchdog_callback
        )
        rospy.on_shutdown(self.stop_wheels)
        rospy.loginfo(
            "[mecanum_controller] wheel_base=%.3f, track=%.3f, radius=%.3f",
            self.wheel_base,
            self.track_width,
            self.wheel_radius,
        )

    def publish_wheel_speeds(
        self, wheel_speeds: Iterable[float]
    ) -> None:
        speeds = tuple(wheel_speeds)
        for name, speed in zip(WHEEL_NAMES, speeds):
            self.command_publishers[name].publish(Float64(float(speed)))
        linear_x, linear_y, angular_z = forward_kinematics(
            speeds,
            self.wheel_base,
            self.track_width,
            self.wheel_radius,
        )
        body_command = Twist()
        body_command.linear.x = linear_x
        body_command.linear.y = linear_y
        body_command.angular.z = angular_z
        self.base_command_publisher.publish(body_command)

    def stop_wheels(self) -> None:
        with self.lock:
            self.publish_wheel_speeds((0.0, 0.0, 0.0, 0.0))

    def command_callback(self, message: Twist) -> None:
        try:
            wheel_speeds = inverse_kinematics(
                message.linear.x,
                message.linear.y,
                message.angular.z,
                self.wheel_base,
                self.track_width,
                self.wheel_radius,
                self.max_wheel_speed,
            )
        except ValueError as error:
            rospy.logwarn_throttle(
                5.0, "[mecanum_controller] rejected command: %s", error
            )
            self.stop_wheels()
            return

        with self.lock:
            self.last_command_time = rospy.Time.now()
            self.publish_wheel_speeds(wheel_speeds)

    def watchdog_callback(self, _event: rospy.timer.TimerEvent) -> None:
        with self.lock:
            now = rospy.Time.now()
            elapsed = now - self.last_command_time
            if elapsed.to_sec() < 0.0:
                self.last_command_time = now
            elif elapsed > self.command_timeout:
                self.publish_wheel_speeds((0.0, 0.0, 0.0, 0.0))

    def joint_state_callback(self, message: JointState) -> None:
        velocity_by_joint = {
            name: velocity
            for name, velocity in zip(message.name, message.velocity)
        }
        joint_names = tuple(f"{name}_wheel_joint" for name in WHEEL_NAMES)
        if not all(name in velocity_by_joint for name in joint_names):
            return

        wheel_speeds = tuple(
            velocity_by_joint[name] for name in joint_names
        )
        try:
            linear_x, linear_y, angular_z = forward_kinematics(
                wheel_speeds,
                self.wheel_base,
                self.track_width,
                self.wheel_radius,
            )
        except ValueError as error:
            rospy.logwarn_throttle(
                5.0, "[mecanum_controller] invalid joint state: %s", error
            )
            return

        stamp = (
            message.header.stamp
            if message.header.stamp != rospy.Time()
            else rospy.Time.now()
        )
        with self.lock:
            if self.last_joint_stamp is not None:
                delta_time = (stamp - self.last_joint_stamp).to_sec()
                if 0.0 < delta_time <= 1.0:
                    cos_yaw = math.cos(self.yaw)
                    sin_yaw = math.sin(self.yaw)
                    self.x += (
                        linear_x * cos_yaw - linear_y * sin_yaw
                    ) * delta_time
                    self.y += (
                        linear_x * sin_yaw + linear_y * cos_yaw
                    ) * delta_time
                    self.yaw += angular_z * delta_time
                    self.yaw = math.atan2(
                        math.sin(self.yaw), math.cos(self.yaw)
                    )
            self.last_joint_stamp = stamp
            self.publish_odometry(
                stamp, linear_x, linear_y, angular_z
            )

    def model_states_callback(self, message: ModelStates) -> None:
        try:
            model_index = message.name.index(self.gazebo_model_name)
        except ValueError:
            return

        pose = message.pose[model_index]
        world_twist = message.twist[model_index]
        orientation = pose.orientation
        yaw = math.atan2(
            2.0 * (
                orientation.w * orientation.z
                + orientation.x * orientation.y
            ),
            1.0 - 2.0 * (
                orientation.y * orientation.y
                + orientation.z * orientation.z
            ),
        )
        cosine = math.cos(yaw)
        sine = math.sin(yaw)

        odometry = Odometry()
        odometry.header.stamp = rospy.Time.now()
        odometry.header.frame_id = self.odom_frame
        odometry.child_frame_id = self.base_frame
        odometry.pose.pose = pose
        odometry.twist.twist.linear.x = (
            cosine * world_twist.linear.x
            + sine * world_twist.linear.y
        )
        odometry.twist.twist.linear.y = (
            -sine * world_twist.linear.x
            + cosine * world_twist.linear.y
        )
        odometry.twist.twist.linear.z = world_twist.linear.z
        odometry.twist.twist.angular = world_twist.angular
        self.publish_odometry_message(odometry)

    def publish_odometry(
        self,
        stamp: rospy.Time,
        linear_x: float,
        linear_y: float,
        angular_z: float,
    ) -> None:
        quaternion_z = math.sin(self.yaw / 2.0)
        quaternion_w = math.cos(self.yaw / 2.0)

        odometry = Odometry()
        odometry.header.stamp = stamp
        odometry.header.frame_id = self.odom_frame
        odometry.child_frame_id = self.base_frame
        odometry.pose.pose.position.x = self.x
        odometry.pose.pose.position.y = self.y
        odometry.pose.pose.orientation.z = quaternion_z
        odometry.pose.pose.orientation.w = quaternion_w
        odometry.twist.twist.linear.x = linear_x
        odometry.twist.twist.linear.y = linear_y
        odometry.twist.twist.angular.z = angular_z
        self.publish_odometry_message(odometry)

    def publish_odometry_message(self, odometry: Odometry) -> None:
        odometry.pose.covariance[0] = 0.001
        odometry.pose.covariance[7] = 0.001
        odometry.pose.covariance[35] = 0.03
        odometry.twist.covariance[0] = 0.001
        odometry.twist.covariance[7] = 0.001
        odometry.twist.covariance[35] = 0.03
        self.odom_publisher.publish(odometry)

        if not self.publish_tf:
            return
        transform = TransformStamped()
        transform.header.stamp = odometry.header.stamp
        transform.header.frame_id = self.odom_frame
        transform.child_frame_id = self.base_frame
        transform.transform.translation.x = (
            odometry.pose.pose.position.x
        )
        transform.transform.translation.y = (
            odometry.pose.pose.position.y
        )
        transform.transform.translation.z = (
            odometry.pose.pose.position.z
        )
        transform.transform.rotation = odometry.pose.pose.orientation
        self.tf_broadcaster.sendTransform(transform)


def main() -> None:
    rospy.init_node("mecanum_controller")
    MecanumController()
    rospy.spin()


if __name__ == "__main__":
    main()

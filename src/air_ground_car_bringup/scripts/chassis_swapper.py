#!/usr/bin/env python3
"""Switch Gazebo chassis models while preserving pose and ROS interfaces."""

import os
import signal
import subprocess
import threading
import time
from typing import Dict, Iterable, List

import rosservice
import rospy
import xacro
import yaml
from air_ground_interfaces.srv import SwapChassis, SwapChassisResponse
from controller_manager_msgs.srv import (
    ListControllers,
    LoadController,
    SwitchController,
    UnloadController,
)
from gazebo_msgs.srv import DeleteModel, GetModelState, SpawnModel


CHASSIS_CONTROLLERS: Dict[str, List[str]] = {
    "diff": [
        "joint_state_controller",
        "gimbal_pan_controller",
        "gimbal_tilt_controller",
        "diff_drive_controller",
    ],
    "mecanum": [
        "joint_state_controller",
        "gimbal_pan_controller",
        "gimbal_tilt_controller",
        "front_left_wheel_controller",
        "front_right_wheel_controller",
        "rear_left_wheel_controller",
        "rear_right_wheel_controller",
    ],
}


class ChassisSwapper:
    """Own controller and robot-state-publisher lifecycle across model swaps."""

    def __init__(self) -> None:
        self.package_path = os.path.abspath(
            str(rospy.get_param("~package_path"))
        )
        self.initial_chassis = str(
            rospy.get_param("~initial_chassis", "mecanum")
        )
        self.model_names = {
            "diff": str(
                rospy.get_param("~diff_model_name", "diff_car")
            ),
            "mecanum": str(
                rospy.get_param(
                    "~mecanum_model_name", "mecanum_car"
                )
            ),
        }
        self.timeout = float(rospy.get_param("~timeout", 60.0))
        if self.initial_chassis not in CHASSIS_CONTROLLERS:
            raise ValueError(
                f"unsupported initial chassis: {self.initial_chassis}"
            )
        if self.timeout <= 0.0:
            raise ValueError("timeout must be positive")
        if (
            not all(self.model_names.values())
            or len(set(self.model_names.values())) != 2
        ):
            raise ValueError("chassis model names must be non-empty and unique")

        self.lock = threading.Lock()
        self.current_chassis = self.initial_chassis
        self.robot_state_publisher = None
        self.shutting_down = False

        self._wait_for_service("/gazebo/get_model_state")
        self._wait_for_service("/gazebo/delete_model")
        self._wait_for_service("/gazebo/spawn_urdf_model")
        self.get_model_state = rospy.ServiceProxy(
            "/gazebo/get_model_state", GetModelState
        )
        self.delete_model = rospy.ServiceProxy(
            "/gazebo/delete_model", DeleteModel
        )
        self.spawn_model = rospy.ServiceProxy(
            "/gazebo/spawn_urdf_model", SpawnModel
        )

        initial_model = self.model_names[self.initial_chassis]
        self._wait_until(
            lambda: self._model_exists(initial_model),
            f"initial model {initial_model}",
        )
        self._wait_for_manager(present=True)
        self._load_and_start_controllers(
            CHASSIS_CONTROLLERS[self.initial_chassis]
        )
        self._restart_robot_state_publisher()
        rospy.set_param("/car/current_chassis", self.current_chassis)

        self.service = rospy.Service(
            "/car/swap_chassis", SwapChassis, self.handle_swap
        )
        rospy.on_shutdown(self.shutdown)
        rospy.loginfo(
            "[chassis_swapper] ready with %s chassis",
            self.current_chassis,
        )

    def _wait_for_service(self, service_name: str) -> None:
        rospy.wait_for_service(service_name, timeout=self.timeout)

    def _wait_until(self, predicate, label: str) -> None:
        deadline = time.monotonic() + self.timeout
        while time.monotonic() < deadline and not rospy.is_shutdown():
            if predicate():
                return
            time.sleep(0.05)
        raise RuntimeError(f"timeout waiting for {label}")

    @staticmethod
    def _service_names() -> set:
        try:
            return set(rosservice.get_service_list())
        except rosservice.ROSServiceIOException:
            return set()

    def _manager_available(self) -> bool:
        return (
            "/car/controller_manager/list_controllers"
            in self._service_names()
        )

    def _wait_for_manager(self, present: bool) -> None:
        description = (
            "new /car controller manager"
            if present
            else "old /car controller manager shutdown"
        )
        self._wait_until(
            lambda: self._manager_available() is present, description
        )

    def _model_exists(self, model_name: str) -> bool:
        try:
            return bool(self.get_model_state(model_name, "world").success)
        except rospy.ServiceException:
            return False

    def _get_pose(self, model_name: str):
        response = self.get_model_state(model_name, "world")
        if not response.success:
            raise RuntimeError(
                f"cannot read {model_name} pose: {response.status_message}"
            )
        return response.pose

    def _delete_existing_model(self, model_name: str) -> None:
        if not self._model_exists(model_name):
            return
        response = self.delete_model(model_name)
        if not response.success:
            raise RuntimeError(
                f"cannot delete {model_name}: {response.status_message}"
            )
        self._wait_until(
            lambda: not self._model_exists(model_name),
            f"{model_name} deletion",
        )

    def _config_path(self, chassis_type: str) -> str:
        return os.path.join(
            self.package_path,
            "config",
            f"{chassis_type}_chassis_control.yaml",
        )

    def _urdf_path(self, chassis_type: str) -> str:
        return os.path.join(
            self.package_path,
            "urdf",
            f"{chassis_type}_chassis.urdf.xacro",
        )

    def _load_controller_config(self, chassis_type: str) -> None:
        with open(self._config_path(chassis_type), encoding="utf-8") as stream:
            config = yaml.safe_load(stream)
        if not isinstance(config, dict) or not isinstance(
            config.get("car"), dict
        ):
            raise RuntimeError(
                f"invalid controller config for {chassis_type}"
            )
        rospy.set_param("/car", config["car"])

    def _render_urdf(self, chassis_type: str) -> str:
        document = xacro.process_file(self._urdf_path(chassis_type))
        return document.toxml()

    def _list_controllers(self):
        proxy = rospy.ServiceProxy(
            "/car/controller_manager/list_controllers", ListControllers
        )
        return proxy().controller

    def _stop_and_unload_all_controllers(self) -> None:
        if not self._manager_available():
            return
        controllers = self._list_controllers()
        running = [
            controller.name
            for controller in controllers
            if controller.state == "running"
        ]
        if running:
            switch = rospy.ServiceProxy(
                "/car/controller_manager/switch_controller",
                SwitchController,
            )
            response = switch([], running, 2, False, 5.0)
            if not response.ok:
                raise RuntimeError("failed to stop current controllers")

        unload = rospy.ServiceProxy(
            "/car/controller_manager/unload_controller", UnloadController
        )
        for controller in reversed(controllers):
            if not unload(controller.name).ok:
                raise RuntimeError(
                    f"failed to unload controller {controller.name}"
                )

    def _load_and_start_controllers(
        self, controller_names: Iterable[str]
    ) -> None:
        names = list(controller_names)
        load = rospy.ServiceProxy(
            "/car/controller_manager/load_controller", LoadController
        )
        loaded = []
        for name in names:
            if not load(name).ok:
                raise RuntimeError(f"failed to load controller {name}")
            loaded.append(name)

        switch = rospy.ServiceProxy(
            "/car/controller_manager/switch_controller", SwitchController
        )
        response = switch(loaded, [], 2, False, 5.0)
        if not response.ok:
            raise RuntimeError(
                "failed to start controllers: " + ", ".join(loaded)
            )

        states = {
            controller.name: controller.state
            for controller in self._list_controllers()
        }
        if not all(states.get(name) == "running" for name in loaded):
            raise RuntimeError("controller state verification failed")

    def _stop_robot_state_publisher(self) -> None:
        process = self.robot_state_publisher
        self.robot_state_publisher = None
        if process is None or process.poll() is not None:
            return
        process.send_signal(signal.SIGINT)
        try:
            process.wait(timeout=5.0)
        except subprocess.TimeoutExpired:
            process.terminate()
            try:
                process.wait(timeout=2.0)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=2.0)

    def _restart_robot_state_publisher(self) -> None:
        self._stop_robot_state_publisher()
        self.robot_state_publisher = subprocess.Popen(
            [
                "rosrun",
                "robot_state_publisher",
                "robot_state_publisher",
                "__name:=car_robot_state_publisher",
                "__ns:=/car",
            ],
            close_fds=True,
        )
        time.sleep(0.2)
        if self.robot_state_publisher.poll() is not None:
            raise RuntimeError("robot_state_publisher failed to start")

    def _activate_chassis(self, chassis_type: str, pose) -> None:
        self._load_controller_config(chassis_type)
        robot_description = self._render_urdf(chassis_type)
        rospy.set_param("/robot_description", robot_description)

        model_name = self.model_names[chassis_type]
        response = self.spawn_model(
            model_name, robot_description, "", pose, "world"
        )
        if not response.success:
            raise RuntimeError(
                f"cannot spawn {model_name}: {response.status_message}"
            )
        self._wait_until(
            lambda: self._model_exists(model_name),
            f"{model_name} spawn",
        )
        self._wait_for_manager(present=True)
        self._load_and_start_controllers(
            CHASSIS_CONTROLLERS[chassis_type]
        )
        self._restart_robot_state_publisher()
        self.current_chassis = chassis_type
        rospy.set_param("/car/current_chassis", chassis_type)

    def _remove_chassis(self, chassis_type: str) -> None:
        self._stop_and_unload_all_controllers()
        self._stop_robot_state_publisher()
        self._delete_existing_model(self.model_names[chassis_type])
        self._wait_for_manager(present=False)

    def handle_swap(self, request) -> SwapChassisResponse:
        target = request.target_chassis.strip().lower()
        if target not in CHASSIS_CONTROLLERS:
            return SwapChassisResponse(
                success=False,
                message=f"unknown chassis type: {request.target_chassis}",
            )

        with self.lock:
            if target == self.current_chassis:
                return SwapChassisResponse(
                    success=True,
                    message=f"already using {target} chassis",
                )

            previous = self.current_chassis
            pose = self._get_pose(self.model_names[previous])
            try:
                self._remove_chassis(previous)
                self._activate_chassis(target, pose)
            except Exception as error:
                rospy.logerr(
                    "[chassis_swapper] swap %s -> %s failed: %s",
                    previous,
                    target,
                    error,
                )
                rollback_error = None
                try:
                    if self._manager_available():
                        self._stop_and_unload_all_controllers()
                    self._delete_existing_model(self.model_names[target])
                    if self._manager_available():
                        self._wait_for_manager(present=False)
                    self._activate_chassis(previous, pose)
                except Exception as caught:
                    rollback_error = caught
                    rospy.logerr(
                        "[chassis_swapper] rollback failed: %s", caught
                    )
                message = f"swap failed: {error}"
                if rollback_error is not None:
                    message += f"; rollback failed: {rollback_error}"
                return SwapChassisResponse(
                    success=False, message=message
                )

            rospy.loginfo(
                "[chassis_swapper] switched %s -> %s", previous, target
            )
            return SwapChassisResponse(
                success=True,
                message=f"switched to {target} chassis",
            )

    def shutdown(self) -> None:
        if self.shutting_down:
            return
        self.shutting_down = True
        self._stop_robot_state_publisher()
        try:
            self._stop_and_unload_all_controllers()
        except Exception as error:
            rospy.logwarn(
                "[chassis_swapper] controller shutdown incomplete: %s",
                error,
            )


def main() -> None:
    rospy.init_node("chassis_swapper")
    ChassisSwapper()
    rospy.spin()


if __name__ == "__main__":
    main()

#!/usr/bin/env bash

set -o pipefail

WORKSPACE_ROOT="${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}"
FAILURES=0

pass() {
  printf '[PASS] %s\n' "$1"
}

fail() {
  printf '[FAIL] %s\n' "$1"
  FAILURES=$((FAILURES + 1))
}

check_ros_interface() {
  local command_name="$1"
  local interface_name="$2"

  if "${command_name}" show "${interface_name}" >/dev/null 2>&1; then
    pass "${interface_name}"
  else
    fail "${interface_name}"
  fi
}

printf '=== Task-01 Verification ===\n'

if [ -f /opt/ros/noetic/setup.bash ]; then
  # shellcheck disable=SC1091
  source /opt/ros/noetic/setup.bash
  pass 'ROS Noetic installed'
else
  fail 'ROS Noetic setup not found'
fi

if command -v gzserver >/dev/null 2>&1; then
  pass 'Gazebo server installed'
else
  fail 'Gazebo server not found'
fi

if [ -f "${WORKSPACE_ROOT}/devel/setup.bash" ]; then
  # shellcheck disable=SC1090
  source "${WORKSPACE_ROOT}/devel/setup.bash"
  pass 'Catkin workspace built'
else
  fail "Workspace setup missing: ${WORKSPACE_ROOT}/devel/setup.bash"
fi

for message_name in \
  Capability \
  ChassisState \
  Mission \
  MissionStatus \
  Observation \
  RobotState \
  SemanticLandmark \
  SensorFusion \
  ServerCommand \
  WorldState; do
  check_ros_interface rosmsg "air_ground_interfaces/${message_name}"
done

for service_name in QueryWorldState SwapChassis; do
  check_ros_interface rossrv "air_ground_interfaces/${service_name}"
done

check_ros_interface rosmsg 'air_ground_interfaces/NavigateAction'
check_ros_interface rosmsg 'air_ground_interfaces/NavigateGoal'
check_ros_interface rosmsg 'air_ground_interfaces/NavigateResult'
check_ros_interface rosmsg 'air_ground_interfaces/NavigateFeedback'

if [ "${FAILURES}" -eq 0 ]; then
  printf '=== Task-01 PASS ===\n'
  exit 0
fi

printf '=== Task-01 FAIL: %d check(s) failed ===\n' "${FAILURES}"
exit 1

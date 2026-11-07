#!/usr/bin/env bash
# Install Project Prometheus dependencies on a fresh Ubuntu 20.04 host.

set -euo pipefail

setup_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
workspace_root=${AIR_GROUND_WS:-$setup_root}
px4_root=${PX4_AUTOPILOT_DIR:-${HOME}/PX4-Autopilot}
ubuntu_version=$(source /etc/os-release && printf '%s' "$VERSION_ID")

if [[ "$ubuntu_version" != "20.04" ]]; then
  printf '[ERROR] Ubuntu 20.04 is required; detected %s\n' "$ubuntu_version" >&2
  exit 1
fi

printf '=== Step 1/6: ROS Noetic repository ===\n'
sudo apt-get update
sudo apt-get install -y --no-install-recommends \
  ca-certificates curl gnupg2 lsb-release
if [[ ! -r /usr/share/keyrings/ros-archive-keyring.gpg ]]; then
  curl -fsSL https://raw.githubusercontent.com/ros/rosdistro/master/ros.key |
    sudo gpg --dearmor -o /usr/share/keyrings/ros-archive-keyring.gpg
fi
printf 'deb [signed-by=/usr/share/keyrings/ros-archive-keyring.gpg] http://packages.ros.org/ros/ubuntu %s main\n' \
  "$(lsb_release -sc)" |
  sudo tee /etc/apt/sources.list.d/ros1-latest.list >/dev/null

printf '=== Step 2/6: ROS and system dependencies ===\n'
sudo apt-get update
sudo apt-get install -y \
  build-essential git python3-catkin-tools python3-pip python3-rosdep \
  python3-rosinstall python3-rosinstall-generator python3-wstool xvfb \
  ros-noetic-desktop-full ros-noetic-cv-bridge ros-noetic-gazebo-ros-control \
  ros-noetic-mavros ros-noetic-mavros-extras ros-noetic-mavros-msgs \
  ros-noetic-pcl-ros ros-noetic-robot-localization ros-noetic-ros-control \
  ros-noetic-ros-controllers ros-noetic-teleop-twist-keyboard \
  ros-noetic-tf2-ros

if [[ ! -r /etc/ros/rosdep/sources.list.d/20-default.list ]]; then
  sudo rosdep init
fi
rosdep update

printf '=== Step 3/6: Python dependencies ===\n'
python3 -m pip install --user --upgrade pip
python3 -m pip install --user -r "${workspace_root}/requirements.txt"

printf '=== Step 4/6: PX4 v1.14 SITL ===\n'
if [[ ! -d "${px4_root}/.git" ]]; then
  git clone --recursive --branch v1.14.0 --depth 1 \
    https://github.com/PX4/PX4-Autopilot.git "$px4_root"
fi
if [[ ! -x "${px4_root}/build/px4_sitl_default/bin/px4" ]]; then
  (
    cd "$px4_root"
    git submodule update --init --recursive
    bash Tools/setup/ubuntu.sh --no-nuttx
    make px4_sitl_default gazebo-classic
  )
fi

printf '=== Step 5/6: Workspace dependencies ===\n'
source /opt/ros/noetic/setup.bash
rosdep install --from-paths "${workspace_root}/src" --ignore-src -r -y

printf '=== Step 6/6: Catkin build ===\n'
cd "$workspace_root"
catkin config --cmake-args -DPYTHON_EXECUTABLE=/usr/bin/python3
catkin build --summarize --no-status

printf '\nSetup complete.\n'
printf '  Runtime: source %s/scripts/setup_runtime.sh px4\n' "$workspace_root"
printf '  Help:    make -C %s help\n' "$workspace_root"
printf '  Launch:  make -C %s launch-full\n' "$workspace_root"

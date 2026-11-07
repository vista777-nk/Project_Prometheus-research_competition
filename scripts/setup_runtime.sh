#!/usr/bin/env bash
# Load the ROS workspace and, optionally, the PX4 Gazebo environment.

runtime_mode=${1:-ros}
runtime_script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
runtime_workspace=${AIR_GROUND_WS:-$(cd -- "${runtime_script_dir}/.." && pwd)}
runtime_px4_root=${PX4_AUTOPILOT_DIR:-${HOME}/PX4-Autopilot}

runtime_fail() {
  printf '[ERROR] %s\n' "$1" >&2
  return 1
}

if [[ "$runtime_mode" != "ros" && "$runtime_mode" != "px4" ]]; then
  runtime_fail "runtime mode must be 'ros' or 'px4'"
  return 1 2>/dev/null || exit 1
fi
if [[ ! -r /opt/ros/noetic/setup.bash ]]; then
  runtime_fail "ROS Noetic setup not found"
  return 1 2>/dev/null || exit 1
fi
if [[ ! -r "${runtime_workspace}/devel/setup.bash" ]]; then
  runtime_fail "workspace is not built: ${runtime_workspace}"
  return 1 2>/dev/null || exit 1
fi

# Upstream ROS and PX4 setup scripts are not nounset-clean.
runtime_had_nounset=false
[[ $- == *u* ]] && runtime_had_nounset=true && set +u
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
unset PYTHONHOME PYTHONPATH
unset CONDA_PREFIX CONDA_DEFAULT_ENV CONDA_PROMPT_MODIFIER CONDA_SHLVL
unset CONDA_AUTO_PATH CONDA_EXE CONDA_PYTHON_EXE _CE_CONDA _CE_M
source /opt/ros/noetic/setup.bash
source "${runtime_workspace}/devel/setup.bash"

if [[ "$runtime_mode" == "px4" ]]; then
  runtime_gazebo_setup=/usr/share/gazebo/setup.sh
  runtime_px4_setup="${runtime_px4_root}/Tools/simulation/gazebo-classic/setup_gazebo.bash"
  runtime_px4_build="${runtime_px4_root}/build/px4_sitl_default"
  runtime_sitl_gazebo="${runtime_px4_root}/Tools/simulation/gazebo-classic/sitl_gazebo-classic"

  if [[ ! -r "$runtime_gazebo_setup" ]]; then
    runtime_fail "Gazebo Classic setup not found"
    return 1 2>/dev/null || exit 1
  fi
  if [[ ! -r "$runtime_px4_setup" || ! -x "${runtime_px4_build}/bin/px4" ]]; then
    runtime_fail "PX4 SITL is not built under ${runtime_px4_root}"
    return 1 2>/dev/null || exit 1
  fi

  # Both paths derive from PX4_AUTOPILOT_DIR, so they cannot be resolved
  # statically. Readability is already checked above, which is the part that
  # actually matters; ShellCheck only needs to be told to stop following.
  # shellcheck source=/dev/null
  source "$runtime_gazebo_setup"
  # shellcheck source=/dev/null
  source "$runtime_px4_setup" "$runtime_px4_root" "$runtime_px4_build"
  export ROS_PACKAGE_PATH="${ROS_PACKAGE_PATH}:${runtime_px4_root}:${runtime_sitl_gazebo}"
fi

[[ "$runtime_had_nounset" == true ]] && set -u
unset runtime_mode runtime_script_dir runtime_workspace runtime_px4_root
unset runtime_had_nounset runtime_gazebo_setup runtime_px4_setup
unset runtime_px4_build runtime_sitl_gazebo
unset -f runtime_fail

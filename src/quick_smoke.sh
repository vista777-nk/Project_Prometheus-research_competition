#!/usr/bin/env bash
# Task-09 quick smoke test for daily development.
set -uo pipefail

boot_seconds=${TASK09_SMOKE_BOOT_SECONDS:-20}
workspace_root=${AIR_GROUND_WS:-$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)}
px4_root=${PX4_AUTOPILOT_DIR:-${HOME}/PX4-Autopilot}
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task09-smoke.XXXXXX")
roslaunch_log=${log_dir}/roslaunch.log
roscore_log=${log_dir}/roscore.log
xvfb_log=${log_dir}/xvfb.log

launch_pid=
roscore_pid=
xvfb_pid=
pass_count=0
fail_count=0

pass() {
  printf '[PASS] %s\n' "$1"
  pass_count=$((pass_count + 1))
}

fail() {
  printf '[FAIL] %s\n' "$1"
  fail_count=$((fail_count + 1))
}

process_group_alive() {
  local pid=$1
  [[ -n "$pid" ]] || return 1
  kill -0 -- "-$pid" 2>/dev/null || kill -0 "$pid" 2>/dev/null
}

stop_process_group() {
  local pid=$1
  [[ -n "$pid" ]] && process_group_alive "$pid" || return
  kill -INT -- "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
  for _ in {1..12}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in {1..8}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  stop_process_group "$xvfb_pid"
  [[ -n "$xvfb_pid" ]] && wait "$xvfb_pid" 2>/dev/null
  # Gazebo/PX4 children can detach from roslaunch's process group.
  timeout 15 make -C "$workspace_root" kill >/dev/null 2>&1 || true
  printf 'task09_smoke_log_dir=%s\n' "$log_dir"
}

on_signal() {
  local exit_code=$1
  trap - INT TERM
  exit "$exit_code"
}

trap cleanup EXIT
trap 'on_signal 130' INT
trap 'on_signal 143' TERM

wait_for_master() {
  local deadline=$((SECONDS + 30))
  while ((SECONDS < deadline)); do
    rosparam get /rosversion >/dev/null 2>&1 && return 0
    sleep 1
  done
  return 1
}

start_virtual_display() {
  local display_number=
  [[ -n "${DISPLAY:-}" ]] && return 0
  command -v Xvfb >/dev/null 2>&1 || return 1
  for candidate in {90..199}; do
    if [[ ! -e "/tmp/.X${candidate}-lock" ]] &&
       [[ ! -S "/tmp/.X11-unix/X${candidate}" ]]; then
      display_number=$candidate
      break
    fi
  done
  [[ -n "$display_number" ]] || return 1
  setsid Xvfb ":${display_number}" -screen 0 1280x1024x24 \
    -nolisten tcp >"$xvfb_log" 2>&1 &
  xvfb_pid=$!
  export DISPLAY=":${display_number}"
  export LIBGL_ALWAYS_SOFTWARE=1
  for _ in {1..20}; do
    [[ -S "/tmp/.X11-unix/X${display_number}" ]] && return 0
    process_group_alive "$xvfb_pid" || return 1
    sleep 0.25
  done
  return 1
}

check_topic() {
  local topic=$1
  local pattern=$2
  local label=$3
  local sample
  sample=$(timeout 3 rostopic echo --noarr -n 1 "$topic" 2>/dev/null || true)
  if grep -Eq "$pattern" <<<"$sample"; then
    pass "$label"
  else
    fail "$label (topic=$topic)"
  fi
}

if [[ ! "$boot_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK09_SMOKE_BOOT_SECONDS must be a positive integer\n' >&2
  exit 2
fi
if [[ ! -r /opt/ros/noetic/setup.bash ||
      ! -r "$workspace_root/devel/setup.bash" ||
      ! -r "$px4_root/Tools/simulation/gazebo-classic/setup_gazebo.bash" ||
      ! -x "$px4_root/build/px4_sitl_default/bin/px4" ]]; then
  printf '[ERROR] ROS workspace or PX4 SITL runtime is unavailable\n' >&2
  exit 2
fi

export ROS_LOG_DIR="${log_dir}/ros"
mkdir -p "$ROS_LOG_DIR"
set +u
setup_failed=0
source "$workspace_root/scripts/setup_runtime.sh" px4 || setup_failed=1
set -u
if ((setup_failed != 0)) || ! start_virtual_display; then
  printf '[ERROR] failed to load runtime or start Xvfb\n' >&2
  exit 2
fi

timeout 15 make -C "$workspace_root" kill >/dev/null 2>&1 || true
if ! rosparam get /rosversion >/dev/null 2>&1; then
  setsid roscore >"$roscore_log" 2>&1 &
  roscore_pid=$!
  wait_for_master || {
    printf '[ERROR] ROS master did not become ready\n' >&2
    exit 2
  }
fi

printf '%s\n' '--- Quick Smoke ---'
setsid roslaunch air_ground_bringup air_ground_sim.launch \
  chassis:=diff gui:=false headless:=true interactive:=false \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!
sleep "$boot_seconds"

if ! process_group_alive "$launch_pid"; then
  fail "Full system launch remains alive"
else
  pass "Full system launch remains alive"
fi
check_topic /car/observation 'robot_id:.*car' 'Car Observation'
check_topic /drone/heartbeat 'data:' 'Drone heartbeat'
check_topic /server/world_state 'agents:' 'WorldState'
if timeout 3 rosnode list 2>/dev/null | grep -Fxq /coordinator; then
  pass 'Coordinator node'
else
  fail 'Coordinator node'
fi

printf 'Task-09 smoke results: %d passed, %d failed\n' \
  "$pass_count" "$fail_count"
exit "$((fail_count == 0 ? 0 : 1))"

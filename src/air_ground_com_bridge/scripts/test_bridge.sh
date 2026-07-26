#!/usr/bin/env bash
set -uo pipefail

timeout_seconds=${TASK06_TIMEOUT_SECONDS:-60}
workspace_root=${AIR_GROUND_WS:-${HOME}/air_ground_sim_ws}

if [[ ! "$timeout_seconds" =~ ^[1-9][0-9]*$ ]]; then
  printf '[ERROR] TASK06_TIMEOUT_SECONDS must be a positive integer\n' >&2
  exit 2
fi

pass_count=0
fail_count=0
launch_pid=
roscore_pid=
publisher_pid=
drone_pid=
server_pid=
server_command_pid=
log_dir=$(mktemp -d "${TMPDIR:-/tmp}/task06-bridge.XXXXXX")
roslaunch_log="${log_dir}/roslaunch.log"
roscore_log="${log_dir}/roscore.log"
publisher_log="${log_dir}/publishers.log"
drone_log="${log_dir}/drone.log"
server_log="${log_dir}/server.log"
command_log="${log_dir}/commands.log"
server_command_log="${log_dir}/server_command.log"

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
  kill -0 -- "-$pid" 2>/dev/null || kill -0 "$pid" 2>/dev/null
}

stop_process_group() {
  local pid=$1
  if [[ -z "$pid" ]] || ! process_group_alive "$pid"; then
    return
  fi
  kill -INT -- "-$pid" 2>/dev/null || kill -INT "$pid" 2>/dev/null || true
  for _ in {1..20}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -TERM -- "-$pid" 2>/dev/null || kill -TERM "$pid" 2>/dev/null || true
  for _ in {1..10}; do
    process_group_alive "$pid" || return
    sleep 0.25
  done
  kill -KILL -- "-$pid" 2>/dev/null || kill -KILL "$pid" 2>/dev/null || true
}

cleanup() {
  set +e
  stop_process_group "$launch_pid"
  [[ -n "$launch_pid" ]] && wait "$launch_pid" 2>/dev/null
  stop_process_group "$publisher_pid"
  [[ -n "$publisher_pid" ]] && wait "$publisher_pid" 2>/dev/null
  stop_process_group "$drone_pid"
  [[ -n "$drone_pid" ]] && wait "$drone_pid" 2>/dev/null
  stop_process_group "$server_pid"
  [[ -n "$server_pid" ]] && wait "$server_pid" 2>/dev/null
  stop_process_group "$server_command_pid"
  [[ -n "$server_command_pid" ]] &&
    wait "$server_command_pid" 2>/dev/null
  stop_process_group "$roscore_pid"
  [[ -n "$roscore_pid" ]] && wait "$roscore_pid" 2>/dev/null
  printf 'task06_log_dir=%s\n' "$log_dir"
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

launch_alive() {
  [[ -n "$launch_pid" ]] && kill -0 "$launch_pid" 2>/dev/null
}

check_nodes() {
  local deadline=$((SECONDS + timeout_seconds))
  local nodes
  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "Communication bridge nodes running (roslaunch exited)"
      return
    fi
    nodes=$(timeout 5 rosnode list 2>/dev/null || true)
    if grep -Fxq /drone_car_bridge <<<"$nodes" &&
       grep -Fxq /edge_server_bridge <<<"$nodes"; then
      pass "Communication bridge nodes running"
      return
    fi
    sleep 1
  done
  fail "Communication bridge nodes running (timeout=${timeout_seconds}s)"
}

check_topic_sample() {
  local topic=$1
  local pattern=$2
  local label=$3
  local deadline=$((SECONDS + timeout_seconds))
  local sample
  while ((SECONDS < deadline)); do
    if ! launch_alive; then
      fail "$label (roslaunch exited)"
      return
    fi
    sample=$(timeout 5 rostopic echo -n 1 "$topic" 2>/dev/null || true)
    if grep -Eq "$pattern" <<<"$sample"; then
      pass "$label"
      return
    fi
    sleep 0.5
  done
  fail "$label (topic=${topic}, timeout=${timeout_seconds}s)"
}

wait_for_log() {
  local log_file=$1
  local pattern=$2
  local label=$3
  local process_pid=$4
  local deadline=$((SECONDS + timeout_seconds))
  while ((SECONDS < deadline)); do
    if grep -Fq "$pattern" "$log_file" 2>/dev/null; then
      pass "$label"
      return
    fi
    if [[ -n "$process_pid" ]] &&
       ! process_group_alive "$process_pid"; then
      fail "$label (peer exited; log=${log_file})"
      return
    fi
    sleep 0.5
  done
  fail "$label (timeout=${timeout_seconds}s, log=${log_file})"
}

if [[ ! -r /opt/ros/noetic/setup.bash ]]; then
  printf '[ERROR] ROS Noetic setup not found\n' >&2
  exit 2
fi
if [[ ! -r "$workspace_root/devel/setup.bash" ]]; then
  printf '[ERROR] workspace setup not found: %s/devel/setup.bash\n' \
    "$workspace_root" >&2
  exit 2
fi

export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin
export PYTHONNOUSERSITE=1
unset PYTHONHOME PYTHONPATH
unset CONDA_PREFIX CONDA_DEFAULT_ENV CONDA_PROMPT_MODIFIER CONDA_SHLVL
unset CONDA_AUTO_PATH CONDA_EXE CONDA_PYTHON_EXE _CE_CONDA _CE_M
export ROS_LOG_DIR="${log_dir}/ros"

set +u
setup_failed=0
source /opt/ros/noetic/setup.bash || setup_failed=1
source "$workspace_root/devel/setup.bash" || setup_failed=1
set -u
if ((setup_failed != 0)); then
  printf '[ERROR] failed to load the ROS runtime environment\n' >&2
  exit 2
fi

package_path=$(rospack find air_ground_com_bridge 2>/dev/null) || {
  printf '[ERROR] air_ground_com_bridge is not discoverable\n' >&2
  exit 2
}
peer_script="${package_path}/test/runtime_peer.py"
if [[ ! -r "$peer_script" ]]; then
  printf '[ERROR] runtime peer is missing: %s\n' "$peer_script" >&2
  exit 2
fi

if ! /usr/bin/python3 -c '
import socket

for socket_type, port in (
    (socket.SOCK_DGRAM, 14550),
    (socket.SOCK_DGRAM, 14551),
    (socket.SOCK_DGRAM, 18570),
    (socket.SOCK_STREAM, 9090),
):
    endpoint = socket.socket(socket.AF_INET, socket_type)
    endpoint.bind(("127.0.0.1", port))
    endpoint.close()
' >/dev/null 2>&1; then
  printf '[ERROR] Task-06 UDP/TCP ports are already in use\n' >&2
  exit 2
fi

if ! rosparam get /rosversion >/dev/null 2>&1; then
  setsid roscore >"$roscore_log" 2>&1 &
  roscore_pid=$!
  if ! wait_for_master; then
    printf '[ERROR] ROS master did not become ready\n' >&2
    exit 2
  fi
fi
if timeout 5 rosnode list 2>/dev/null |
   grep -Eq '^/(drone_car_bridge|edge_server_bridge)$'; then
  printf '[ERROR] a Task-06 bridge node is already running\n' >&2
  exit 2
fi

setsid roslaunch air_ground_com_bridge air_ground_com_bridge.launch \
  >"$roslaunch_log" 2>&1 &
launch_pid=$!
check_nodes
check_topic_sample /car/server_connected 'data: *False' \
  "TCP retry state exposed while server is absent"

setsid /usr/bin/python3 "$peer_script" publishers \
  --timeout "$timeout_seconds" >"$publisher_log" 2>&1 &
publisher_pid=$!
setsid /usr/bin/python3 "$peer_script" drone \
  --timeout "$timeout_seconds" >"$drone_log" 2>&1 &
drone_pid=$!

check_topic_sample /drone/heartbeat 'data: *True' \
  "CRC-valid MAVLink heartbeat reaches ROS"
check_topic_sample /drone/pose 'x: *1.25' \
  "Drone ENU pose is forwarded"
check_topic_sample /drone/state 'robot_id: *[\"'\"']?drone' \
  "ICD RobotState is published"

timeout 5 rostopic pub -1 /car/to_drone/cmd std_msgs/String \
  "data: '{\"command\":400,\"params\":[1.0]}'" \
  >"$command_log" 2>&1 || fail "ROS command publication succeeded"
wait_for_log "$drone_log" DRONE_COMMAND_OK \
  "ROS command becomes valid MAVLink COMMAND_LONG" "$drone_pid"

setsid timeout "$timeout_seconds" rostopic echo -n 1 \
  /car/server_command >"$server_command_log" 2>&1 &
server_command_pid=$!
sleep 0.5

setsid /usr/bin/python3 "$peer_script" server \
  --timeout "$timeout_seconds" >"$server_log" 2>&1 &
server_pid=$!
wait_for_log "$server_log" SERVER_READY \
  "Mock TCP server listening" "$server_pid"
check_topic_sample /car/server_connected 'data: *True' \
  "TCP bridge connects after retry"
wait_for_log "$server_log" SERVER_TELEMETRY_OK \
  "Framed JSON carries all sensors and JPEG" "$server_pid"
wait_for_log "$server_command_log" 'query_text: "bridge-test"' \
  "Framed server command reaches ROS" "$server_command_pid"
wait_for_log "$server_log" SERVER_RECONNECT_OK \
  "TCP telemetry recovers after forced disconnect" "$server_pid"

printf 'Task-06 results: %d passed, %d failed\n' "$pass_count" "$fail_count"
((fail_count == 0))

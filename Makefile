SHELL := /bin/bash
.DEFAULT_GOAL := help

WS ?= $(CURDIR)
PX4_ROOT ?= $(HOME)/PX4-Autopilot

export AIR_GROUND_WS := $(WS)
export PX4_AUTOPILOT_DIR := $(PX4_ROOT)

.PHONY: build clean rebuild test-unit test-drone test-car test-diff \
	test-mecanum test-sensors test-bridge test-server test-all \
	test-e2e quick-smoke test-smoke \
	launch-drone launch-car launch-car-mecanum launch-server launch-full \
	launch-full-mecanum kill status help

build:
	@bash -c 'source /opt/ros/noetic/setup.bash && cd "$(WS)" && catkin build --summarize --no-status'
	@echo "Build complete. Runtime environment: source $(WS)/scripts/setup_runtime.sh"

clean:
	@bash -c 'source /opt/ros/noetic/setup.bash && cd "$(WS)" && catkin clean -y'

rebuild: clean build

test-unit: build
	@bash -c 'source "$(WS)/scripts/setup_runtime.sh" ros && cd "$(WS)" && catkin test --no-status && catkin_test_results build'

test-drone: build
	@bash "$(WS)/src/air_ground_drone_bringup/scripts/test_drone.sh"

test-car: test-diff

test-diff: build
	@bash "$(WS)/src/air_ground_car_bringup/scripts/test_diff.sh"

test-mecanum: build
	@bash "$(WS)/src/air_ground_car_bringup/scripts/test_mecanum.sh"

test-sensors: build
	@bash "$(WS)/src/air_ground_car_bringup/scripts/test_sensors.sh"

test-bridge: build
	@bash "$(WS)/src/air_ground_com_bridge/scripts/test_bridge.sh"

test-server: build
	@bash "$(WS)/src/air_ground_lab_server/scripts/test_server.sh"

test-all: test-unit test-drone test-diff test-mecanum test-sensors test-bridge test-server

test-e2e: build
	@bash "$(WS)/src/e2e_test.sh"

quick-smoke: build
	@bash "$(WS)/src/quick_smoke.sh"

test-smoke: quick-smoke

launch-drone: build
	@xvfb-run -a -s '-screen 0 1280x1024x24 -nolisten tcp' bash -c 'export LIBGL_ALWAYS_SOFTWARE=1; source "$(WS)/scripts/setup_runtime.sh" px4 && roslaunch air_ground_bringup drone_only.launch gui:=false headless:=true'

launch-car: build
	@xvfb-run -a -s '-screen 0 1280x1024x24 -nolisten tcp' bash -c 'export LIBGL_ALWAYS_SOFTWARE=1; source "$(WS)/scripts/setup_runtime.sh" ros && roslaunch air_ground_bringup car_only.launch chassis:=diff gui:=false headless:=true'

launch-car-mecanum: build
	@xvfb-run -a -s '-screen 0 1280x1024x24 -nolisten tcp' bash -c 'export LIBGL_ALWAYS_SOFTWARE=1; source "$(WS)/scripts/setup_runtime.sh" ros && roslaunch air_ground_bringup car_only.launch chassis:=mecanum gui:=false headless:=true'

launch-server: build
	@bash -c 'source "$(WS)/scripts/setup_runtime.sh" ros && roslaunch air_ground_bringup server_only.launch'

launch-full: build
	@xvfb-run -a -s '-screen 0 1280x1024x24 -nolisten tcp' bash -c 'export LIBGL_ALWAYS_SOFTWARE=1; source "$(WS)/scripts/setup_runtime.sh" px4 && roslaunch air_ground_bringup air_ground_sim.launch chassis:=diff gui:=false headless:=true'

launch-full-mecanum: build
	@xvfb-run -a -s '-screen 0 1280x1024x24 -nolisten tcp' bash -c 'export LIBGL_ALWAYS_SOFTWARE=1; source "$(WS)/scripts/setup_runtime.sh" px4 && roslaunch air_ground_bringup air_ground_sim.launch chassis:=mecanum gui:=false headless:=true'

kill:
	-@pkill -f '[p]x4.*etc/init.d-posix/rcS' 2>/dev/null || true
	-@pkill -f '[g]zserver' 2>/dev/null || true
	-@pkill -f '[g]zclient' 2>/dev/null || true
	-@pkill -f '[r]oslaunch.*air_ground_' 2>/dev/null || true
	@echo "Air-ground simulation processes stopped."

status:
	@echo "=== ROS Master ==="
	@pgrep -f '[r]osmaster' >/dev/null 2>&1 && echo "  Running" || echo "  Not running"
	@echo "=== Gazebo ==="
	@pgrep -f '[g]zserver' >/dev/null 2>&1 && echo "  gzserver: Running" || echo "  gzserver: Not running"
	@echo "=== PX4 SITL ==="
	@pgrep -f '[p]x4.*etc/init.d-posix/rcS' >/dev/null 2>&1 && echo "  PX4: Running" || echo "  PX4: Not running"
	@echo "=== Active ROS Nodes ==="
	@bash -c 'source /opt/ros/noetic/setup.bash && [[ ! -r "$(WS)/devel/setup.bash" ]] || source "$(WS)/devel/setup.bash"; rosnode list 2>/dev/null' || echo "  (ROS master not running)"

help:
	@echo "Air-Ground Simulation Makefile"
	@echo
	@echo "  make build                 Build all Catkin packages"
	@echo "  make clean                 Clean Catkin build artifacts"
	@echo "  make test-unit             Run all unit tests"
	@echo "  make test-<name>           Run drone/car/bridge/server runtime tests"
	@echo "  make test-all              Run Task 02-07 unit and runtime tests"
	@echo "  make test-e2e              Run the full Task-09 E2E validation"
	@echo "  make quick-smoke           Run the short Task-09 smoke test"
	@echo "  make launch-drone          Launch PX4 drone simulation"
	@echo "  make launch-car            Launch the differential-drive car"
	@echo "  make launch-car-mecanum    Launch the mecanum car"
	@echo "  make launch-server         Launch bridges and laboratory server"
	@echo "  make launch-full           Launch the full system with a diff car"
	@echo "  make launch-full-mecanum   Launch the full system with a mecanum car"
	@echo "  make kill                  Stop air-ground simulation processes"
	@echo "  make status                Show simulation process status"

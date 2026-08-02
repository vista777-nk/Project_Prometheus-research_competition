cmake_minimum_required(VERSION 3.0.2)
project(air_ground_car_bringup)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  controller_manager
  controller_manager_msgs
  cv_bridge
  gazebo_msgs
  gazebo_ros
  gazebo_ros_control
  geometry_msgs
  hardware_interface
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
  tf2_ros
  topic_tools
)
find_package(gazebo REQUIRED)

catkin_package(
  LIBRARIES mecanum_planar_drive_plugin
  CATKIN_DEPENDS
  air_ground_interfaces
  controller_manager
  controller_manager_msgs
  cv_bridge
  gazebo_msgs
  gazebo_ros
  gazebo_ros_control
  geometry_msgs
  hardware_interface
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
  tf2_ros
  topic_tools
)

include_directories(
  ${catkin_INCLUDE_DIRS}
  ${GAZEBO_INCLUDE_DIRS}
)
link_directories(${GAZEBO_LIBRARY_DIRS})

add_library(
  mecanum_planar_drive_plugin
  src/mecanum_planar_drive_plugin.cpp
)
target_link_libraries(
  mecanum_planar_drive_plugin
  ${catkin_LIBRARIES}
  ${GAZEBO_LIBRARIES}
)

catkin_install_python(
  PROGRAMS
    scripts/car_preprocessor.py
    scripts/chassis_swapper.py
    scripts/gimbal_controller.py
    scripts/mecanum_controller.py
    scripts/chassis_protocol.py
    scripts/chassis_bridge.py
    scripts/hardware_interface.py
    scripts/mock_hardware.py
    scripts/real_hardware.py
    scripts/sensor_config.py
    scripts/rplidar_driver.py
    scripts/icm42688_driver.py
    scripts/openmv_bridge.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  PROGRAMS
    scripts/test_diff.sh
    scripts/test_mecanum.sh
    scripts/test_sensors.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  TARGETS mecanum_planar_drive_plugin
  LIBRARY DESTINATION ${CATKIN_PACKAGE_LIB_DESTINATION}
)

install(
  DIRECTORY config launch urdf worlds
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

if(CATKIN_ENABLE_TESTING)
  catkin_add_nosetests(test/test_car_preprocessor.py)
  catkin_add_nosetests(test/test_gimbal_controller.py)
  catkin_add_nosetests(test/test_mecanum_controller.py)
  # test/host/ 下的传感器驱动测试**刻意不在这里注册**:
  # 它们跑在纯 Python 环境 (CI 的 lint-scripts job), 不需要 ROS 也不需要
  # roscore。同一个检查只实现一处 (ADR-0008 §决策-3)。
  # 这里只注册一条 ROS 侧独有的检查: 那套测试用的 ROS 替身是否忠实。
  catkin_add_nosetests(test/test_ros_stub_fidelity.py)
endif()

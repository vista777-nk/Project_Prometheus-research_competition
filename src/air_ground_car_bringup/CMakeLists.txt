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
endif()

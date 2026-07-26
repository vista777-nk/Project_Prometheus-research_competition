cmake_minimum_required(VERSION 3.0.2)
project(air_ground_car_bringup)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  controller_manager
  gazebo_ros
  gazebo_ros_control
  geometry_msgs
  hardware_interface
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
  topic_tools
)

catkin_package(
  CATKIN_DEPENDS
  air_ground_interfaces
  controller_manager
  gazebo_ros
  gazebo_ros_control
  geometry_msgs
  hardware_interface
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
  topic_tools
)

install(
  PROGRAMS
    scripts/test_diff.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  DIRECTORY config launch urdf worlds
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

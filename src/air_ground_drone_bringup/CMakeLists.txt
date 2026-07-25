cmake_minimum_required(VERSION 3.0.2)
project(air_ground_drone_bringup)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  geometry_msgs
  mavros_msgs
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS
  air_ground_interfaces
  geometry_msgs
  mavros_msgs
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
)

install(
  DIRECTORY config launch scripts worlds
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

cmake_minimum_required(VERSION 3.0.2)
project(air_ground_com_bridge)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  cv_bridge
  geometry_msgs
  mavros
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
  cv_bridge
  geometry_msgs
  mavros
  mavros_msgs
  nav_msgs
  roscpp
  rospy
  sensor_msgs
  std_msgs
)

install(
  DIRECTORY config launch scripts
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

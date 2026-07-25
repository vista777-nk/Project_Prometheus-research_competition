cmake_minimum_required(VERSION 3.0.2)
project(air_ground_lab_server)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  geometry_msgs
  nav_msgs
  rospy
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS
  air_ground_interfaces
  geometry_msgs
  nav_msgs
  rospy
  std_msgs
)

install(
  DIRECTORY config launch scripts
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

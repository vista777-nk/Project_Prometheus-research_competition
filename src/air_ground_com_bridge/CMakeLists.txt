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

catkin_install_python(
  PROGRAMS
    scripts/drone_car_bridge.py
    scripts/edge_server_bridge.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  PROGRAMS scripts/test_bridge.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  FILES test/runtime_peer.py
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/test
)

install(
  DIRECTORY config launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

if(CATKIN_ENABLE_TESTING)
  catkin_add_nosetests(test/test_drone_car_bridge.py)
  catkin_add_nosetests(test/test_edge_server_bridge.py)
endif()

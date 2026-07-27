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

catkin_install_python(
  PROGRAMS
    scripts/coordinator.py
    scripts/eqa_engine.py
    scripts/slam_node.py
    scripts/tcp_receiver.py
    scripts/world_model.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  PROGRAMS scripts/test_server.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  FILES test/runtime_sources.py
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}/test
)

install(
  DIRECTORY config launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

if(CATKIN_ENABLE_TESTING)
  catkin_add_nosetests(test/test_server_components.py)
  catkin_add_nosetests(test/test_tcp_receiver.py)
  catkin_add_nosetests(test/test_world_model.py)
endif()

cmake_minimum_required(VERSION 3.0.2)
project(air_ground_drone_bringup)

find_package(catkin REQUIRED COMPONENTS
  air_ground_interfaces
  cv_bridge
  geometry_msgs
  mavros_msgs
  nav_msgs
  nodelet
  pcl_ros
  roscpp
  rospy
  sensor_msgs
  std_msgs
  topic_tools
)

catkin_package(
  CATKIN_DEPENDS
  air_ground_interfaces
  cv_bridge
  geometry_msgs
  mavros_msgs
  nav_msgs
  nodelet
  pcl_ros
  roscpp
  rospy
  sensor_msgs
  std_msgs
  topic_tools
)

catkin_install_python(
  PROGRAMS
    scripts/drone_preprocessor.py
    scripts/gps_converter.py
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  PROGRAMS
    scripts/test_drone.sh
  DESTINATION ${CATKIN_PACKAGE_BIN_DESTINATION}
)

install(
  DIRECTORY config launch
  DESTINATION ${CATKIN_PACKAGE_SHARE_DESTINATION}
)

if(CATKIN_ENABLE_TESTING)
  catkin_add_nosetests(test/test_drone_preprocessor.py)
  catkin_add_nosetests(test/test_gps_converter.py)
endif()

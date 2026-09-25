cmake_minimum_required(VERSION 3.0.2)
project(air_ground_interfaces)

find_package(catkin REQUIRED COMPONENTS
  actionlib_msgs
  geometry_msgs
  message_generation
  nav_msgs
  sensor_msgs
  std_msgs
)

add_message_files(
  FILES
  Capability.msg
  ChassisState.msg
  Mission.msg
  MissionStatus.msg
  Observation.msg
  RobotState.msg
  SemanticLandmark.msg
  SensorFusion.msg
  ServerCommand.msg
  WorldState.msg
)

add_service_files(
  FILES
  QueryWorldState.srv
  SwapChassis.srv
)

add_action_files(
  FILES
  Navigate.action
)

generate_messages(
  DEPENDENCIES
  actionlib_msgs
  geometry_msgs
  nav_msgs
  sensor_msgs
  std_msgs
)

catkin_package(
  CATKIN_DEPENDS
  actionlib_msgs
  geometry_msgs
  message_runtime
  nav_msgs
  sensor_msgs
  std_msgs
)

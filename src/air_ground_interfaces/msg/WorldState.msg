# Server internal: global state maintained by the World Model (ICD 2.3)
Header header

RobotState[] agents
nav_msgs/OccupancyGrid map_2d
geometry_msgs/Pose[] dynamic_obstacles
SemanticLandmark[] landmarks

time last_update_perception
time last_update_planning

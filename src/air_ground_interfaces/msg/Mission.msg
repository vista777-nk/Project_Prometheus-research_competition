# Server -> Edge: high-level task describing what to do (ICD 3.1)
Header header
string mission_id
string robot_id

string type
geometry_msgs/Pose target_pose
string target_landmark_id
float32 search_radius
int32 priority

string query_text
string query_id

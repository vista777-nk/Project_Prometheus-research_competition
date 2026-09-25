# Edge -> Server: robot self-state snapshot (ICD 2.2)
Header header
string robot_id

geometry_msgs/Pose pose
geometry_msgs/Twist velocity

string mode
string chassis_type

float32 battery_voltage
bool is_armed
bool is_connected

# Edge -> Server: robot capability declaration (ICD 3.3)
Header header
string robot_id

string locomotion_type
float32 max_speed
float32 max_endurance

string[] sensor_modalities
float32 sensor_range

string compute_tier
float32 max_payload_kg
bool has_gripper

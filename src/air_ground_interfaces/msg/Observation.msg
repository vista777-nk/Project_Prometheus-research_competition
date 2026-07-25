# Edge -> Server: multimodal observation snapshot (ICD 2.1)
Header header
string robot_id
string[] modalities

# Images are populated only when their modality is listed.
sensor_msgs/CompressedImage rgb
sensor_msgs/Image depth

# Downsampled 2D LiDAR.
float32[] lidar_ranges
float32 lidar_angle_min
float32 lidar_angle_increment

# Ultrasonic ranges ordered as front, rear, left, right.
float32[] ultrasonic_ranges

# IMU sample.
geometry_msgs/Vector3 angular_velocity
geometry_msgs/Vector3 linear_acceleration

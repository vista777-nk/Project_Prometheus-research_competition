#include <cmath>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <boost/bind/bind.hpp>
#include <gazebo/common/Events.hh>
#include <gazebo/common/Plugin.hh>
#include <gazebo/gazebo.hh>
#include <gazebo/physics/physics.hh>
#include <geometry_msgs/Twist.h>
#include <ros/callback_queue.h>
#include <ros/ros.h>
#include <ros/subscribe_options.h>

namespace air_ground_car_bringup
{

class MecanumPlanarDrivePlugin : public gazebo::ModelPlugin
{
public:
  ~MecanumPlanarDrivePlugin() override
  {
    update_connection_.reset();
    command_queue_.disable();
    command_subscriber_.shutdown();
    if (node_handle_)
    {
      node_handle_->shutdown();
    }
  }

  void Load(gazebo::physics::ModelPtr model, sdf::ElementPtr sdf) override
  {
    if (!ros::isInitialized())
    {
      gzerr << "mecanum_planar_drive requires gazebo_ros_api_plugin\n";
      return;
    }

    model_ = std::move(model);
    const std::string robot_namespace =
      ReadString(sdf, "robotNamespace", "/car");
    const std::string command_topic =
      ReadString(sdf, "commandTopic", "mecanum_cmd_vel");
    command_timeout_ = ReadDouble(sdf, "cmdTimeout", 0.5);
    if (!std::isfinite(command_timeout_) || command_timeout_ <= 0.0)
    {
      gzerr << "mecanum_planar_drive cmdTimeout must be positive\n";
      return;
    }

    node_handle_ = std::make_unique<ros::NodeHandle>(robot_namespace);
    ros::SubscribeOptions options =
      ros::SubscribeOptions::create<geometry_msgs::Twist>(
        command_topic,
        1,
        boost::bind(
          &MecanumPlanarDrivePlugin::CommandCallback,
          this,
          boost::placeholders::_1),
        ros::VoidPtr(),
        &command_queue_);
    command_subscriber_ = node_handle_->subscribe(options);
    last_command_time_ = model_->GetWorld()->SimTime();
    update_connection_ = gazebo::event::Events::ConnectWorldUpdateBegin(
      std::bind(
        &MecanumPlanarDrivePlugin::OnUpdate,
        this,
        std::placeholders::_1));
  }

private:
  static std::string ReadString(
    const sdf::ElementPtr & sdf,
    const std::string & name,
    const std::string & default_value)
  {
    return sdf->HasElement(name) ?
      sdf->Get<std::string>(name) : default_value;
  }

  static double ReadDouble(
    const sdf::ElementPtr & sdf,
    const std::string & name,
    double default_value)
  {
    return sdf->HasElement(name) ?
      sdf->Get<double>(name) : default_value;
  }

  void CommandCallback(const geometry_msgs::TwistConstPtr & message)
  {
    command_ = *message;
    command_received_ = true;
  }

  void OnUpdate(const gazebo::common::UpdateInfo & information)
  {
    command_queue_.callAvailable(ros::WallDuration(0.0));
    if (command_received_)
    {
      last_command_time_ = information.simTime;
      command_received_ = false;
    }

    geometry_msgs::Twist command = command_;
    const double age = (information.simTime - last_command_time_).Double();
    if (age < 0.0 || age > command_timeout_)
    {
      command = geometry_msgs::Twist();
    }

    const double yaw = model_->WorldPose().Rot().Yaw();
    const double cosine = std::cos(yaw);
    const double sine = std::sin(yaw);
    const ignition::math::Vector3d current_linear =
      model_->WorldLinearVel();
    const ignition::math::Vector3d current_angular =
      model_->WorldAngularVel();
    model_->SetLinearVel(
      ignition::math::Vector3d(
        cosine * command.linear.x - sine * command.linear.y,
        sine * command.linear.x + cosine * command.linear.y,
        current_linear.Z()));
    model_->SetAngularVel(
      ignition::math::Vector3d(
        current_angular.X(),
        current_angular.Y(),
        command.angular.z));
  }

  gazebo::physics::ModelPtr model_;
  gazebo::event::ConnectionPtr update_connection_;
  std::unique_ptr<ros::NodeHandle> node_handle_;
  ros::Subscriber command_subscriber_;
  ros::CallbackQueue command_queue_;
  geometry_msgs::Twist command_;
  gazebo::common::Time last_command_time_;
  double command_timeout_{0.5};
  bool command_received_{false};
};

GZ_REGISTER_MODEL_PLUGIN(MecanumPlanarDrivePlugin)

}  // namespace air_ground_car_bringup

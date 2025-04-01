#include "force_plugin/force_plugin.hpp"
#include <gz/plugin/Register.hh>
#include <gz/sim/components/Link.hh>
#include <gz/sim/components/LinearVelocityCmd.hh>
#include <gz/sim/components/AngularVelocityCmd.hh>
#include <gz/sim/Model.hh>
#include <gz/transport/Node.hh>
#include <rclcpp/rclcpp.hpp>

GZ_ADD_PLUGIN(force_plugin::ForcePlugin, gz::sim::System, gz::sim::ISystemConfigure, gz::sim::ISystemPreUpdate)

namespace force_plugin
{
  ForcePlugin::ForcePlugin() : force_(0, 0, 0), torque_(0, 0, 0) {}

  ForcePlugin::~ForcePlugin()
  {
    if(rclcpp_thread_.joinable())
      rclcpp_thread_.join();
  }

  void ForcePlugin::Configure(const gz::sim::Entity &entity, const std::shared_ptr<const sdf::Element> &sdf,
                              gz::sim::EntityComponentManager &ecm, gz::sim::EventManager & /*eventMgr*/)
  {
    auto model = gz::sim::Model(entity);

    // if ros2 not initialised then init it
    if(!rclcpp::ok())
      {
        rclcpp::init(0, nullptr);
      }

    // Read body name from SDF
    std::string bodyName = "base_link";
    if(sdf->HasElement("bodyName"))
      bodyName = sdf->Get<std::string>("bodyName");

    RCLCPP_INFO(rclcpp::get_logger("force_plugin"), "ForcePlugin: bodyName = %s", bodyName.c_str());
    baseLinkEntity_ = model.LinkByName(ecm, bodyName);
    if(!baseLinkEntity_)
      {
        RCLCPP_ERROR(rclcpp::get_logger("force_plugin"), "Link [%s] not found in model [%s]", bodyName.c_str(),
                     model.Name(ecm).c_str());
        return;
      }

    // Read topic name from SDF
    std::string tetherForceTopic = "cmd_vel";
    if(sdf->HasElement("tetherForceTopic"))
      tetherForceTopic = sdf->Get<std::string>("tetherForceTopic");

    node_ = std::make_shared<rclcpp::Node>("force_plugin");
    sub_ = node_->create_subscription<geometry_msgs::msg::Wrench>(
      tetherForceTopic, 10, std::bind(&ForcePlugin::OnWrenchMsg, this, std::placeholders::_1));

    // Create a separate thread to spin the ROS node
    rclcpp_thread_ = std::thread([this]() { rclcpp::spin(node_); });
  }

  // frequency is determined by world sdf variable: max_step_size
  void ForcePlugin::PreUpdate(const gz::sim::UpdateInfo & /*info*/, gz::sim::EntityComponentManager &ecm)
  {
    auto linVelComp = ecm.Component<gz::sim::components::LinearVelocityCmd>(baseLinkEntity_);
    auto angVelComp = ecm.Component<gz::sim::components::AngularVelocityCmd>(baseLinkEntity_);

    if(linVelComp && angVelComp)
      {
        linVelComp->Data().Set(force_.X(), force_.Y(), force_.Z());
        angVelComp->Data().Set(torque_.X(), torque_.Y(), torque_.Z());
      }
    else
      {
        ecm.CreateComponent(baseLinkEntity_, gz::sim::components::LinearVelocityCmd(force_));
        ecm.CreateComponent(baseLinkEntity_, gz::sim::components::AngularVelocityCmd(torque_));
      }
  }

  void ForcePlugin::OnWrenchMsg(const geometry_msgs::msg::Wrench::SharedPtr msg)
  {
    // Store force and torque directly in Vector3d
    force_.Set(msg->force.x, msg->force.y, msg->force.z);
    torque_.Set(msg->torque.x, msg->torque.y, msg->torque.z);

    RCLCPP_INFO(node_->get_logger(), "Received Wrench - Force: [%f, %f, %f], Torque: [%f, %f, %f]", force_.X(),
                force_.Y(), force_.Z(), torque_.X(), torque_.Y(), torque_.Z());
  }
}

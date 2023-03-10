#include "rclcpp/rclcpp.hpp"
#include "geometry_msgs/msg/twist.hpp"
#include "nav_msgs/msg/odometry.hpp"

#include <tf2/utils.h>
#include <tf2_geometry_msgs/tf2_geometry_msgs.h>
#include <tf2_ros/buffer.h>
#include <tf2_ros/transform_broadcaster.h>
#include <tf2_ros/transform_listener.h>


#include <ypspur.h>

class Icart_mini_driver : public rclcpp::Node
{
    public:
        Icart_mini_driver(): Node("icart_mini_driver")
        {
            odom_pub_ = this->create_publisher<nav_msgs::msg::Odometry>("odom",1);
            tf_broadcaster_ = std::make_unique<tf2_ros::TransformBroadcaster>(*this);

            // odom_pub_timer_ = this->create_wall_timer(500ms,std::bind(&Icart_mini_driver::odometry,this));
            cmd_vel_sub_ = this->create_subscription<geometry_msgs::msg::Twist>(
                "cmd_vel", 1, std::bind(&Icart_mini_driver::cmd_vel_cb, this, std::placeholders::_1));
        }
        // setParam();
        // getParam();
        // geometry_msgs::msg::Twist::ConstPtr cmd_vel_;
        // void cmd_vel_cb(const geometry_msgs::msg::Twist::SharedPtr msg);
        void read_param();
        void reset_param();
        // void bringup_ypspur(std::vector<std::string> args);
        void bringup_ypspur();
        void odometry();
        // void jointstate();
        bool loop();
        // int Hz = 100;
        std::string odom_frame_id;
        std::string base_frame_id;
        int loop_hz;
    
    private:
    
        geometry_msgs::msg::Twist::SharedPtr cmd_vel_ = std::make_shared<geometry_msgs::msg::Twist>();
        rclcpp::Subscription<geometry_msgs::msg::Twist>::SharedPtr cmd_vel_sub_;
        rclcpp::Publisher<nav_msgs::msg::Odometry>::SharedPtr odom_pub_;
        nav_msgs::msg::Odometry odom;
        //devel odom
        float dt = 1.0 / 100;
        double tf_time_offset_ =0.0;
        tf2::Vector3 z_axis_;
        
        std::unique_ptr<tf2_ros::TransformBroadcaster> tf_broadcaster_;
        geometry_msgs::msg::TransformStamped odom_trans;

        //this function is cmd_vel callback and send command ypspur
        void cmd_vel_cb(const geometry_msgs::msg::Twist::SharedPtr msg)
        {
          cmd_vel_ =  msg;
          RCLCPP_INFO(this->get_logger(),"sub cmd_vel");
        // https://github.com/openspur/yp-spur/blob/master/doc/Manpage.control.md#velocity-control
          Spur_vel(msg->linear.x,msg->angular.z);
        }
        
};
    // this function is read ypspur_param file from yaml
    void Icart_mini_driver::read_param()
    {  
      declare_parameter("odom_frame_id","odom");
      declare_parameter("base_frame_id","base_footprint");
      declare_parameter("Hz","100");
      
      get_parameter("odom_frame_id",odom_frame_id);
      get_parameter("base_frame_id",base_frame_id);
      get_parameter("Hz",loop_hz);
      
    }
    //this function is set ypspur_param and bringup ypspur_coordinator
    void Icart_mini_driver::bringup_ypspur()
    {
    //     std::vector<std::string> args =
    //         {
    //           ypspur_bin_,
    //           "-d", port_,
    //           "--admask", ad_mask,
    //           "--msq-key", std::to_string(key_)
    //         };
    //     system("ypspur-coordinator -p  -d /dev/sensors/icart-mini");
        if(YPSpur_init()>0)
        {
          RCLCPP_INFO(this->get_logger(),"Bringup ypspur!!");
          YPSpur_stop();
          YPSpur_free();
        }
        else 
        {
           RCLCPP_WARN(this->get_logger(),"Disconnected ypspur");
        }
    }

    //this function is compute odometry and pub odometry topic , odom tf
    void Icart_mini_driver::odometry()
    {
        //odom
        double x,y,yaw,v,w;
        bool odom_from_cmd_vel = false;
        bool odom_from_ypspur_function = true;
        z_axis_.setX(0);
        z_axis_.setY(0);
        z_axis_.setZ(1);
        rclcpp::Time t = this->now();
        const rclcpp::Time current_stamp(t);
        
        //compute odom from cmd_vel
        if (odom_from_cmd_vel && !odom_from_ypspur_function)
        {
          v = cmd_vel_->linear.x;
          w = cmd_vel_->angular.z;
          yaw = tf2::getYaw(odom.pose.pose.orientation) + dt * w;
          x = odom.pose.pose.position.x + dt * v * cosf(yaw);
          y = odom.pose.pose.position.y + dt * v * sinf(yaw);
        }
        //compute odom from ypspur's function
        if (odom_from_ypspur_function && !odom_from_cmd_vel)
        {
          YPSpur_get_pos(CS_BS,&x,&y,&yaw);
          YPSpur_get_vel(&v,&w);
        }
        
        //publish odom
        odom.header.stamp = current_stamp;
        // odom.header.frame_id = "odom";
        // odom.child_frame_id = "base_footprint";
        odom.header.frame_id = odom_frame_id;
        odom.child_frame_id = base_frame_id;
        odom.pose.pose.position.x = x;
        odom.pose.pose.position.y = y;
        odom.pose.pose.position.z = 0;
        odom.pose.pose.orientation = tf2::toMsg(tf2::Quaternion(z_axis_, yaw));
        odom.twist.twist.linear.x = v;
        odom.twist.twist.linear.y = 0;
        odom.twist.twist.angular.z = w;
        odom_pub_->publish(odom);

        //odom_tf
        odom_trans.header.stamp = current_stamp + rclcpp::Duration::from_seconds(tf_time_offset_);
        odom_trans.transform.translation.x = x;
        odom_trans.transform.translation.y = y;
        odom_trans.transform.translation.z = 0;
        odom_trans.transform.rotation = odom.pose.pose.orientation;
        tf_broadcaster_->sendTransform(odom_trans);
    }
    void Icart_mini_driver::reset_param()
    {
        z_axis_.setX(0);
        z_axis_.setY(0);
        z_axis_.setZ(1);
        cmd_vel_->linear.x = 0.0;
        cmd_vel_->angular.z  = 0.0;
        odom.pose.pose.position.x = 0;
        odom.pose.pose.position.y = 0;
        odom.pose.pose.position.z = 0;
        odom.pose.pose.orientation = tf2::toMsg(tf2::Quaternion(z_axis_, 0));
        odom.twist.twist.linear.x = 0;
        odom.twist.twist.linear.y = 0;
        odom.twist.twist.angular.z = 0;
    }
    //main loop function
    bool Icart_mini_driver::loop()
    {
      if (!YP_get_error_state())
      {
          odometry();
            // YP::YPSpur_vel(cmd_vel_->linear.x,cmd_vel_->angular.z);
           RCLCPP_INFO(this->get_logger(),"Connect ypspur!!");
      }
      else
      {
          RCLCPP_WARN(this->get_logger(),"Disconnected ypspur");
          bringup_ypspur();
          return false;
      }

     return true;
    }

int main(int argc, char * argv[])
{
  rclcpp::init(argc, argv);
//   Icart_mini_driver icart;
  auto icart = std::make_shared<Icart_mini_driver>();
// //   rclcpp::shutdown();
  rclcpp::WallRate looprate(100);
  icart->bringup_ypspur();
  icart->reset_param();
  // icart->loop();
  // rclcpp::spin(icart);
  while (rclcpp::ok())
  {
    icart->loop();
    rclcpp::spin_some(icart);
    looprate.sleep();
  }
  
  return 0;
}

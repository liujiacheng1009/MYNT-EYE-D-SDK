#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

#include <cv_bridge/cv_bridge.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <rclcpp/rclcpp.hpp>
#include <sensor_msgs/image_encodings.hpp>
#include <sensor_msgs/msg/camera_info.hpp>
#include <sensor_msgs/msg/image.hpp>
#include <sensor_msgs/msg/imu.hpp>
#include <std_msgs/msg/header.hpp>

#include "mynteyed/camera.h"
#include "mynteyed/utils.h"

MYNTEYE_USE_NAMESPACE

namespace {

template <typename EnumT>
EnumT enum_from_int(int value) {
  return static_cast<EnumT>(value);
}

int mat_type_for_format(ImageFormat format) {
  switch (format) {
    case ImageFormat::IMAGE_BGR_24:
    case ImageFormat::IMAGE_RGB_24:
    case ImageFormat::IMAGE_GRAY_24:
      return CV_8UC3;
    case ImageFormat::IMAGE_GRAY_8:
      return CV_8UC1;
    case ImageFormat::IMAGE_GRAY_16:
      return CV_16UC1;
    case ImageFormat::IMAGE_YUYV:
      return CV_8UC2;
    default:
      throw std::runtime_error("Image format can not be wrapped as cv::Mat");
  }
}

cv::Mat image_to_mat(const Image::pointer& image) {
  if (!image) {
    throw std::runtime_error("Image is null");
  }
  return cv::Mat(
      image->height(), image->width(), mat_type_for_format(image->format()),
      image->data());
}

sensor_msgs::msg::CameraInfo make_camera_info(
    const CameraIntrinsics& in, const std::string& frame_id) {
  sensor_msgs::msg::CameraInfo info;
  info.header.frame_id = frame_id;
  info.width = in.width;
  info.height = in.height;
  info.distortion_model = "plumb_bob";
  info.d.resize(5);
  for (std::size_t i = 0; i < 5; ++i) {
    info.d[i] = in.coeffs[i];
  }

  info.k[0] = in.fx;
  info.k[2] = in.cx;
  info.k[4] = in.fy;
  info.k[5] = in.cy;
  info.k[8] = 1.0;

  for (std::size_t i = 0; i < 9; ++i) {
    info.r[i] = in.r[i];
  }

  info.p[0] = in.p[0];
  info.p[2] = in.p[2];
  info.p[3] = in.p[3];
  info.p[5] = in.p[5];
  info.p[6] = in.p[6];
  info.p[10] = in.p[10];
  return info;
}

}  // namespace

class MynteyeRos2Node : public rclcpp::Node {
 public:
  MynteyeRos2Node() : Node("mynteye_ros2_node") {
    declare_params();
    load_params();
    open_camera();
    create_publishers();

    const auto period_ms = std::max(1, 1000 / std::max(1, framerate_));
    timer_ = create_wall_timer(
        std::chrono::milliseconds(period_ms),
        std::bind(&MynteyeRos2Node::poll_camera, this));
  }

  ~MynteyeRos2Node() override {
    if (camera_) {
      camera_->Close();
    }
  }

 private:
  void declare_params() {
    declare_parameter<int>("dev_index", 0);
    declare_parameter<int>("framerate", 30);
    declare_parameter<int>("dev_mode", 2);
    declare_parameter<int>("color_mode", 0);
    declare_parameter<int>("stream_mode", 3);
    declare_parameter<int>("color_stream_format", 1);
    declare_parameter<int>("depth_stream_format", 1);
    declare_parameter<bool>("state_ae", true);
    declare_parameter<bool>("state_awb", true);
    declare_parameter<int>("ir_intensity", 4);
    declare_parameter<bool>("ir_depth_only", false);
    declare_parameter<bool>("publish_left", true);
	    declare_parameter<bool>("publish_right", true);
	    declare_parameter<bool>("publish_depth", true);
	    declare_parameter<bool>("publish_depth_color", true);
	    declare_parameter<bool>("publish_imu", true);
	    declare_parameter<double>("depth_visual_max_m", 10.0);
	    declare_parameter<std::string>("left_frame_id", "mynteye_left_color_frame");
	    declare_parameter<std::string>("right_frame_id", "mynteye_right_color_frame");
	    declare_parameter<std::string>("depth_frame_id", "mynteye_depth_frame");
	    declare_parameter<std::string>("imu_frame_id", "mynteye_imu_frame");
	    declare_parameter<std::string>("left_topic", "mynteye/left/image_color");
	    declare_parameter<std::string>("right_topic", "mynteye/right/image_color");
	    declare_parameter<std::string>("depth_topic", "mynteye/depth/image_raw");
	    declare_parameter<std::string>("depth_color_topic", "mynteye/depth/image_color");
	    declare_parameter<std::string>("imu_topic", "mynteye/imu/data_raw");
	  }

  void load_params() {
    dev_index_ = get_parameter("dev_index").as_int();
    framerate_ = get_parameter("framerate").as_int();
    dev_mode_ = get_parameter("dev_mode").as_int();
    color_mode_ = get_parameter("color_mode").as_int();
    stream_mode_ = get_parameter("stream_mode").as_int();
    color_stream_format_ = get_parameter("color_stream_format").as_int();
    depth_stream_format_ = get_parameter("depth_stream_format").as_int();
    state_ae_ = get_parameter("state_ae").as_bool();
    state_awb_ = get_parameter("state_awb").as_bool();
    ir_intensity_ = get_parameter("ir_intensity").as_int();
    ir_depth_only_ = get_parameter("ir_depth_only").as_bool();
	    publish_left_ = get_parameter("publish_left").as_bool();
	    publish_right_ = get_parameter("publish_right").as_bool();
	    publish_depth_ = get_parameter("publish_depth").as_bool();
	    publish_depth_color_ = get_parameter("publish_depth_color").as_bool();
	    publish_imu_ = get_parameter("publish_imu").as_bool();
	    depth_visual_max_m_ = get_parameter("depth_visual_max_m").as_double();
	    left_frame_id_ = get_parameter("left_frame_id").as_string();
	    right_frame_id_ = get_parameter("right_frame_id").as_string();
	    depth_frame_id_ = get_parameter("depth_frame_id").as_string();
	    imu_frame_id_ = get_parameter("imu_frame_id").as_string();
	    left_topic_ = get_parameter("left_topic").as_string();
	    right_topic_ = get_parameter("right_topic").as_string();
	    depth_topic_ = get_parameter("depth_topic").as_string();
	    depth_color_topic_ = get_parameter("depth_color_topic").as_string();
	    imu_topic_ = get_parameter("imu_topic").as_string();
	  }

  void open_camera() {
    camera_ = std::make_unique<Camera>();
    const auto devices = camera_->GetDeviceInfos();
    if (devices.empty()) {
      throw std::runtime_error("No MYNT EYE device found");
    }
    if (dev_index_ < 0 || static_cast<std::size_t>(dev_index_) >= devices.size()) {
      throw std::runtime_error("MYNT EYE device index out of range");
    }

    OpenParams params(dev_index_);
    params.framerate = framerate_;
    params.dev_mode = enum_from_int<DeviceMode>(dev_mode_);
    params.color_mode = enum_from_int<ColorMode>(color_mode_);
    params.stream_mode = enum_from_int<StreamMode>(stream_mode_);
    params.color_stream_format = enum_from_int<StreamFormat>(color_stream_format_);
    params.depth_stream_format = enum_from_int<StreamFormat>(depth_stream_format_);
    params.state_ae = state_ae_;
    params.state_awb = state_awb_;
    params.ir_intensity = static_cast<std::uint8_t>(ir_intensity_);
    params.ir_depth_only = ir_depth_only_;

    camera_->EnableImageInfo(true);
    if (publish_imu_) {
      camera_->EnableMotionDatas();
    }

    RCLCPP_INFO(get_logger(), "Opening MYNT EYE device %d: %s",
                dev_index_, devices[dev_index_].name.c_str());
    camera_->Open(params);
    if (!camera_->IsOpened()) {
      throw std::runtime_error("Open MYNT EYE camera failed");
    }

    const auto intrinsics = camera_->GetStreamIntrinsics(params.stream_mode);
    left_info_ = make_camera_info(intrinsics.left, left_frame_id_);
    right_info_ = make_camera_info(intrinsics.right, right_frame_id_);
    depth_info_ = make_camera_info(intrinsics.left, depth_frame_id_);

    left_enabled_ = camera_->IsStreamDataEnabled(ImageType::IMAGE_LEFT_COLOR);
    right_enabled_ = camera_->IsStreamDataEnabled(ImageType::IMAGE_RIGHT_COLOR);
    depth_enabled_ = camera_->IsStreamDataEnabled(ImageType::IMAGE_DEPTH);

    RCLCPP_INFO(get_logger(), "MYNT EYE opened: left=%d right=%d depth=%d imu=%d",
                left_enabled_, right_enabled_, depth_enabled_, publish_imu_);
  }

  void create_publishers() {
    auto qos = rclcpp::SensorDataQoS();
    if (publish_left_ && left_enabled_) {
      left_pub_ = create_publisher<sensor_msgs::msg::Image>(left_topic_, qos);
      left_info_pub_ = create_publisher<sensor_msgs::msg::CameraInfo>(left_topic_ + "/camera_info", qos);
    }
    if (publish_right_ && right_enabled_) {
      right_pub_ = create_publisher<sensor_msgs::msg::Image>(right_topic_, qos);
      right_info_pub_ = create_publisher<sensor_msgs::msg::CameraInfo>(right_topic_ + "/camera_info", qos);
    }
    if (publish_depth_ && depth_enabled_) {
      depth_pub_ = create_publisher<sensor_msgs::msg::Image>(depth_topic_, qos);
      depth_info_pub_ = create_publisher<sensor_msgs::msg::CameraInfo>(depth_topic_ + "/camera_info", qos);
      if (publish_depth_color_) {
        depth_color_pub_ = create_publisher<sensor_msgs::msg::Image>(depth_color_topic_, qos);
      }
    }
    if (publish_imu_) {
      imu_pub_ = create_publisher<sensor_msgs::msg::Imu>(imu_topic_, qos);
    }

    RCLCPP_INFO(
        get_logger(), "Publishing topics: left=%d right=%d depth=%d depth_color=%d imu=%d",
        static_cast<bool>(left_pub_), static_cast<bool>(right_pub_),
        static_cast<bool>(depth_pub_), static_cast<bool>(depth_color_pub_),
        static_cast<bool>(imu_pub_));
  }

  void poll_camera() {
    if (!camera_ || !camera_->IsOpened()) {
      return;
    }

    camera_->WaitForStream();
    const auto stamp = now();

    if (publish_left_ && left_enabled_) {
      publish_color(ImageType::IMAGE_LEFT_COLOR, left_frame_id_, stamp, left_pub_, left_info_pub_, left_info_);
    }
    if (publish_right_ && right_enabled_) {
      publish_color(ImageType::IMAGE_RIGHT_COLOR, right_frame_id_, stamp, right_pub_, right_info_pub_, right_info_);
    }
    if (publish_depth_ && depth_enabled_) {
      publish_depth(stamp);
    }
    if (publish_imu_) {
      publish_motion_data(stamp);
    }
  }

  void publish_color(
      ImageType type,
      const std::string& frame_id,
      const rclcpp::Time& stamp,
      rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr& pub,
      rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr& info_pub,
      sensor_msgs::msg::CameraInfo& info) {
    if (!pub || !info_pub) {
      return;
    }
    auto data = camera_->GetStreamData(type);
    if (!data.img) {
      return;
    }
    auto bgr_image = data.img->To(ImageFormat::COLOR_BGR);
    cv::Mat bgr = image_to_mat(bgr_image);
    std_msgs::msg::Header header;
    header.stamp = stamp;
    header.frame_id = frame_id;
    info.header = header;
    auto msg = cv_bridge::CvImage(header, sensor_msgs::image_encodings::BGR8, bgr).toImageMsg();
    pub->publish(*msg);
    info_pub->publish(info);
  }

  void publish_depth(const rclcpp::Time& stamp) {
    if (!depth_pub_ || !depth_info_pub_) {
      return;
    }
    auto data = camera_->GetStreamData(ImageType::IMAGE_DEPTH);
    if (!data.img) {
      return;
    }
    auto depth_image = data.img->To(ImageFormat::DEPTH_RAW);
    cv::Mat depth = image_to_mat(depth_image);
    if (depth.empty() || depth.cols == 0 || depth.rows == 0) {
      RCLCPP_WARN_THROTTLE(
          get_logger(), *get_clock(), 5000,
          "Depth image is empty. Try another stream_mode/depth_stream_format.");
      return;
    }
    std_msgs::msg::Header header;
    header.stamp = stamp;
    header.frame_id = depth_frame_id_;
    depth_info_.header = header;
    auto msg = cv_bridge::CvImage(header, sensor_msgs::image_encodings::TYPE_16UC1, depth).toImageMsg();
    depth_pub_->publish(*msg);
    if (publish_depth_color_ && depth_color_pub_) {
      cv::Mat depth_visual;
      const double max_depth_mm = std::max(1.0, depth_visual_max_m_ * 1000.0);
      depth.convertTo(depth_visual, CV_8UC1, 255.0 / max_depth_mm);
      auto color_msg = cv_bridge::CvImage(
          header, sensor_msgs::image_encodings::MONO8, depth_visual).toImageMsg();
      depth_color_pub_->publish(*color_msg);
    }
    depth_info_pub_->publish(depth_info_);
  }

  void publish_motion_data(const rclcpp::Time& stamp) {
    if (!imu_pub_) {
      return;
    }
    const auto motions = camera_->GetMotionDatas();
    for (const auto& motion : motions) {
      if (!motion.imu) {
        continue;
      }
      const auto& imu = *motion.imu;
      if (imu.flag == MYNTEYE_IMU_ACCEL || imu.flag == MYNTEYE_IMU_ACCEL_GYRO_CALIB) {
        last_accel_ = imu;
        have_accel_ = true;
      }
      if (imu.flag == MYNTEYE_IMU_GYRO || imu.flag == MYNTEYE_IMU_ACCEL_GYRO_CALIB) {
        last_gyro_ = imu;
        have_gyro_ = true;
      }
      if (have_accel_ && have_gyro_) {
        sensor_msgs::msg::Imu msg;
        msg.header.stamp = stamp;
        msg.header.frame_id = imu_frame_id_;
        msg.linear_acceleration.x = last_accel_.accel[0];
        msg.linear_acceleration.y = last_accel_.accel[1];
        msg.linear_acceleration.z = last_accel_.accel[2];
        msg.angular_velocity.x = last_gyro_.gyro[0];
        msg.angular_velocity.y = last_gyro_.gyro[1];
        msg.angular_velocity.z = last_gyro_.gyro[2];
        msg.orientation_covariance[0] = -1.0;
        imu_pub_->publish(msg);
        have_accel_ = false;
        have_gyro_ = false;
      }
    }
  }

  std::unique_ptr<Camera> camera_;
  rclcpp::TimerBase::SharedPtr timer_;

  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr left_pub_;
  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr left_info_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr right_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr right_info_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr depth_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::Image>::SharedPtr depth_color_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::CameraInfo>::SharedPtr depth_info_pub_;
	  rclcpp::Publisher<sensor_msgs::msg::Imu>::SharedPtr imu_pub_;

  sensor_msgs::msg::CameraInfo left_info_;
  sensor_msgs::msg::CameraInfo right_info_;
  sensor_msgs::msg::CameraInfo depth_info_;

  ImuData last_accel_;
  ImuData last_gyro_;
  bool have_accel_{false};
  bool have_gyro_{false};

  int dev_index_{0};
  int framerate_{30};
  int dev_mode_{2};
  int color_mode_{0};
  int stream_mode_{3};
  int color_stream_format_{1};
	  int depth_stream_format_{1};
	  int ir_intensity_{4};
	  double depth_visual_max_m_{10.0};
	  bool state_ae_{true};
	  bool state_awb_{true};
	  bool ir_depth_only_{false};
	  bool publish_left_{true};
	  bool publish_right_{true};
	  bool publish_depth_{true};
	  bool publish_depth_color_{true};
	  bool publish_imu_{true};
  bool left_enabled_{false};
  bool right_enabled_{false};
  bool depth_enabled_{false};

  std::string left_frame_id_;
  std::string right_frame_id_;
  std::string depth_frame_id_;
  std::string imu_frame_id_;
  std::string left_topic_;
	  std::string right_topic_;
	  std::string depth_topic_;
	  std::string depth_color_topic_;
	  std::string imu_topic_;
	};

int main(int argc, char** argv) {
  rclcpp::init(argc, argv);
  try {
    rclcpp::spin(std::make_shared<MynteyeRos2Node>());
  } catch (const std::exception& e) {
    RCLCPP_FATAL(rclcpp::get_logger("mynteye_ros2_node"), "%s", e.what());
  }
  rclcpp::shutdown();
  return 0;
}

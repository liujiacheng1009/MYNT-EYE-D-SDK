// Copyright 2018 Slightech Co., Ltd. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <map>
#include <iomanip>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>

#include <opencv2/highgui/highgui.hpp>

#include "mynteyed/camera.h"
#include "mynteyed/utils.h"
#include "mynteyed/util/times.h"

#include "dataset.h"

MYNTEYE_USE_NAMESPACE

namespace {

struct RecordOptions {
  std::string outdir{"./dataset"};
  int duration_sec{0};
  int dev_index{-1};
  int framerate{30};
  int save_rate{0};
  int ir_intensity{4};
  bool sync_save{false};
  bool display{true};
  bool left{true};
  bool right{false};
  bool depth{true};
  bool imu{true};
  DeviceMode dev_mode{DeviceMode::DEVICE_ALL};
  StreamMode stream_mode{StreamMode::STREAM_1280x720};
  StreamFormat color_format{StreamFormat::STREAM_YUYV};
  StreamFormat depth_format{StreamFormat::STREAM_YUYV};
};

void print_usage(const char* prog) {
  std::cout
      << "Usage: " << prog << " [outdir] [options]\n"
      << "\n"
      << "Options:\n"
      << "  --out DIR              Output directory. Default: ./dataset\n"
      << "  --duration SEC         Stop after SEC seconds. Default: 0, run until q/ESC or Ctrl-C\n"
      << "  --dev-index N          Device index. Default: interactive/auto select\n"
      << "  --framerate FPS        Camera framerate. Default: 30\n"
      << "  --save-rate FPS        Limit saved image streams to FPS. Default: 0, save all frames\n"
      << "  --sync-save            Save only frame_ids present in all enabled image streams\n"
      << "  --stream-mode N        0=640x480, 1=1280x480, 2=1280x720, 3=2560x720. Default: 2\n"
      << "  --dev-mode N           0=color, 1=depth, 2=all. Default: 2\n"
      << "  --color-format N       0=MJPG, 1=YUYV. Default: 1\n"
      << "  --depth-format N       0=MJPG, 1=YUYV. Default: 1\n"
      << "  --ir-intensity N       IR intensity. Default: 4\n"
      << "  --left / --no-left     Enable/disable left color recording. Default: on\n"
      << "  --right / --no-right   Enable/disable right color recording. Default: off\n"
      << "  --depth / --no-depth   Enable/disable raw depth recording. Default: on\n"
      << "  --imu / --no-imu       Enable/disable IMU recording. Default: on\n"
      << "  --display              Show preview windows. Default: on\n"
      << "  --no-display           Disable preview windows for headless recording\n"
      << "  -h, --help             Show this message\n";
}

int parse_int_arg(const std::string& name, const char* value) {
  if (value == nullptr) {
    throw std::runtime_error("Missing value for " + name);
  }
  return std::atoi(value);
}

const char* next_arg(int argc, char const* argv[], int* index, const std::string& name) {
  if (*index + 1 >= argc) {
    throw std::runtime_error("Missing value for " + name);
  }
  ++(*index);
  return argv[*index];
}

RecordOptions parse_args(int argc, char const* argv[]) {
  RecordOptions options;
  bool positional_outdir_used = false;

  for (int i = 1; i < argc; ++i) {
    std::string arg(argv[i]);
    if (arg == "-h" || arg == "--help") {
      print_usage(argv[0]);
      std::exit(0);
    } else if (arg == "--out") {
      options.outdir = next_arg(argc, argv, &i, arg);
    } else if (arg == "--duration") {
      options.duration_sec = parse_int_arg(arg, next_arg(argc, argv, &i, arg));
    } else if (arg == "--dev-index") {
      options.dev_index = parse_int_arg(arg, next_arg(argc, argv, &i, arg));
    } else if (arg == "--framerate") {
      options.framerate = parse_int_arg(arg, next_arg(argc, argv, &i, arg));
    } else if (arg == "--save-rate") {
      options.save_rate = parse_int_arg(arg, next_arg(argc, argv, &i, arg));
    } else if (arg == "--sync-save") {
      options.sync_save = true;
    } else if (arg == "--stream-mode") {
      options.stream_mode =
          static_cast<StreamMode>(parse_int_arg(arg, next_arg(argc, argv, &i, arg)));
    } else if (arg == "--dev-mode") {
      options.dev_mode =
          static_cast<DeviceMode>(parse_int_arg(arg, next_arg(argc, argv, &i, arg)));
    } else if (arg == "--color-format") {
      options.color_format =
          static_cast<StreamFormat>(parse_int_arg(arg, next_arg(argc, argv, &i, arg)));
    } else if (arg == "--depth-format") {
      options.depth_format =
          static_cast<StreamFormat>(parse_int_arg(arg, next_arg(argc, argv, &i, arg)));
    } else if (arg == "--ir-intensity") {
      options.ir_intensity = parse_int_arg(arg, next_arg(argc, argv, &i, arg));
    } else if (arg == "--left") {
      options.left = true;
    } else if (arg == "--no-left") {
      options.left = false;
    } else if (arg == "--right") {
      options.right = true;
    } else if (arg == "--no-right") {
      options.right = false;
    } else if (arg == "--depth") {
      options.depth = true;
    } else if (arg == "--no-depth") {
      options.depth = false;
    } else if (arg == "--imu") {
      options.imu = true;
    } else if (arg == "--no-imu") {
      options.imu = false;
    } else if (arg == "--display") {
      options.display = true;
    } else if (arg == "--no-display") {
      options.display = false;
    } else if (!arg.empty() && arg[0] != '-' && !positional_outdir_used) {
      options.outdir = arg;
      positional_outdir_used = true;
    } else {
      throw std::runtime_error("Unknown argument: " + arg);
    }
  }

  return options;
}

class SaveLimiter {
 public:
  explicit SaveLimiter(int save_rate) {
    if (save_rate > 0) {
      sdk_interval_ = 100000.0 / static_cast<double>(save_rate);
      host_interval_us_ = 1000000.0 / static_cast<double>(save_rate);
    }
  }

  bool accept(ImageType type, const StreamData& data) {
    if (sdk_interval_ <= 0) {
      return true;
    }

    const bool use_sdk_time = data.img_info && data.img_info->timestamp > 0;
    const double timestamp = use_sdk_time
        ? static_cast<double>(data.img_info->timestamp)
        : current_time_us();
    const double interval = use_sdk_time ? sdk_interval_ : host_interval_us_;
    auto it = last_saved_us_.find(type);
    if (it == last_saved_us_.end() ||
        timestamp - it->second >= interval * 0.95 ||
        timestamp < it->second) {
      last_saved_us_[type] = timestamp;
      return true;
    }
    return false;
  }

 private:
  static double current_time_us() {
    return static_cast<double>(times::since_epoch<times::microseconds>(times::now()));
  }

  double sdk_interval_{0.0};
  double host_interval_us_{0.0};
  std::map<ImageType, double> last_saved_us_;
};

template <typename T>
void write_array(std::ostream& os, const T* data, std::size_t size) {
  os << "[";
  for (std::size_t i = 0; i < size; ++i) {
    if (i > 0) {
      os << ", ";
    }
    os << data[i];
  }
  os << "]";
}

void write_matrix3(std::ostream& os, const double data[3][3]) {
  os << "[";
  for (std::size_t i = 0; i < 3; ++i) {
    if (i > 0) {
      os << ", ";
    }
    write_array(os, data[i], 3);
  }
  os << "]";
}

void write_camera_intrinsics(
    std::ostream& os, const std::string& name, const CameraIntrinsics& in) {
  os << name << ":\n"
     << "  width: " << in.width << "\n"
     << "  height: " << in.height << "\n"
     << "  fx: " << in.fx << "\n"
     << "  fy: " << in.fy << "\n"
     << "  cx: " << in.cx << "\n"
     << "  cy: " << in.cy << "\n"
     << "  coeffs: ";
  write_array(os, in.coeffs, 5);
  os << "\n"
     << "  camera_matrix: [[" << in.fx << ", 0, " << in.cx << "], "
     << "[0, " << in.fy << ", " << in.cy << "], [0, 0, 1]]\n"
     << "  projection_matrix: ";
  write_array(os, in.p, 12);
  os << "\n"
     << "  rectification_matrix: ";
  write_array(os, in.r, 9);
  os << "\n";
}

void write_extrinsics(
    std::ostream& os, const std::string& name, const Extrinsics& ex) {
  os << name << ":\n"
     << "  rotation: ";
  write_matrix3(os, ex.rotation);
  os << "\n"
     << "  translation: ";
  write_array(os, ex.translation, 3);
  os << "\n";
}

void save_calibration(
    Camera* cam, const std::string& outdir, StreamMode stream_mode,
    bool include_motion) {
  const std::string path = outdir + "/calibration.yaml";
  std::ofstream out(path);
  if (!out) {
    std::cerr << "Warning: cannot write calibration file: " << path << std::endl;
    return;
  }

  out << std::setprecision(std::numeric_limits<double>::max_digits10);
  out << "stream_mode: " << static_cast<int>(stream_mode) << "\n";

  bool stream_in_ok = false;
  auto stream_in = cam->GetStreamIntrinsics(stream_mode, &stream_in_ok);
  out << "stream_intrinsics_ok: " << (stream_in_ok ? "true" : "false") << "\n";
  if (stream_in_ok) {
    write_camera_intrinsics(out, "left", stream_in.left);
    write_camera_intrinsics(out, "right", stream_in.right);
  }

  bool stream_ex_ok = false;
  auto stream_ex = cam->GetStreamExtrinsics(stream_mode, &stream_ex_ok);
  out << "stream_extrinsics_ok: " << (stream_ex_ok ? "true" : "false") << "\n";
  if (stream_ex_ok) {
    write_extrinsics(out, "left_to_right", stream_ex);
  }

  if (include_motion) {
    bool motion_ex_ok = false;
    auto motion_ex = cam->GetMotionExtrinsics(&motion_ex_ok);
    out << "motion_extrinsics_ok: " << (motion_ex_ok ? "true" : "false")
        << "\n";
    if (motion_ex_ok) {
      write_extrinsics(out, "left_to_imu", motion_ex);
    }
  }

  std::cout << "Saved calibration to: " << path << std::endl;
}

std::uint64_t frame_id_of(const StreamData& data) {
  if (data.img_info) {
    return data.img_info->frame_id;
  }
  if (data.img) {
    return data.img->frame_id();
  }
  return 0;
}

void save_streams(
    Camera* cam, tools::Dataset* dataset, ImageType type, bool enabled,
    bool display, const std::string& window, SaveLimiter* limiter,
    std::size_t* saved_count, std::size_t* received_count) {
  if (!enabled) {
    return;
  }

  auto stream_data = cam->GetStreamDatas(type);
  *received_count += stream_data.size();

  if (display && !stream_data.empty() && stream_data.back().img) {
    if (type == ImageType::IMAGE_DEPTH) {
      cv::imshow(window, stream_data.back().img->To(ImageFormat::DEPTH_GRAY)->ToMat());
    } else {
      cv::imshow(window, stream_data.back().img->To(ImageFormat::COLOR_BGR)->ToMat());
    }
  }

  for (auto&& data : stream_data) {
    if (limiter && !limiter->accept(type, data)) {
      continue;
    }
    dataset->SaveStreamData(type, data);
    ++(*saved_count);
  }
}

using stream_map_t = std::map<std::uint64_t, StreamData>;

struct SyncSaveState {
  stream_map_t left_frames;
  stream_map_t right_frames;
  stream_map_t depth_frames;
};

void collect_stream_map(
    Camera* cam, ImageType type, bool enabled, bool display,
    const std::string& window, stream_map_t* frames,
    std::size_t* received_count) {
  if (!enabled) {
    return;
  }

  auto stream_data = cam->GetStreamDatas(type);
  *received_count += stream_data.size();

  if (display && !stream_data.empty() && stream_data.back().img) {
    if (type == ImageType::IMAGE_DEPTH) {
      cv::imshow(window, stream_data.back().img->To(ImageFormat::DEPTH_GRAY)->ToMat());
    } else {
      cv::imshow(window, stream_data.back().img->To(ImageFormat::COLOR_BGR)->ToMat());
    }
  }

  for (auto&& data : stream_data) {
    const auto frame_id = frame_id_of(data);
    if (frame_id == 0) {
      continue;
    }
    (*frames)[frame_id] = data;
  }
}

void prune_pending_frames(stream_map_t* frames) {
  constexpr std::size_t kMaxPendingFrames = 120;
  while (frames->size() > kMaxPendingFrames) {
    frames->erase(frames->begin());
  }
}

void intersect_frame_ids(
    std::set<std::uint64_t>* ids, bool* initialized,
    const stream_map_t& frames, bool enabled) {
  if (!enabled) {
    return;
  }

  std::set<std::uint64_t> next;
  for (const auto& item : frames) {
    if (!*initialized || ids->count(item.first) > 0) {
      next.insert(item.first);
    }
  }
  *ids = next;
  *initialized = true;
}

std::size_t save_synced_streams(
    Camera* cam, tools::Dataset* dataset, SyncSaveState* state,
    bool left_enabled, bool right_enabled,
    bool depth_enabled, bool display, SaveLimiter* limiter,
    std::size_t* left_count, std::size_t* right_count, std::size_t* depth_count,
    std::size_t* left_received_count, std::size_t* right_received_count,
    std::size_t* depth_received_count) {
  collect_stream_map(
      cam, ImageType::IMAGE_LEFT_COLOR, left_enabled, display, "left",
      &state->left_frames, left_received_count);
  collect_stream_map(
      cam, ImageType::IMAGE_RIGHT_COLOR, right_enabled, display, "right",
      &state->right_frames, right_received_count);
  collect_stream_map(
      cam, ImageType::IMAGE_DEPTH, depth_enabled, display, "depth",
      &state->depth_frames, depth_received_count);

  std::set<std::uint64_t> frame_ids;
  bool frame_ids_initialized = false;
  intersect_frame_ids(&frame_ids, &frame_ids_initialized,
                      state->left_frames, left_enabled);
  intersect_frame_ids(&frame_ids, &frame_ids_initialized,
                      state->right_frames, right_enabled);
  intersect_frame_ids(&frame_ids, &frame_ids_initialized,
                      state->depth_frames, depth_enabled);

  std::size_t groups_saved = 0;
  for (const auto frame_id : frame_ids) {
    const StreamData* rate_data = nullptr;
    if (left_enabled) {
      rate_data = &state->left_frames.at(frame_id);
    } else if (right_enabled) {
      rate_data = &state->right_frames.at(frame_id);
    } else if (depth_enabled) {
      rate_data = &state->depth_frames.at(frame_id);
    }

    const bool accepted = !limiter || !rate_data ||
        limiter->accept(ImageType::IMAGE_ALL, *rate_data);

    if (accepted && left_enabled) {
      dataset->SaveStreamData(
          ImageType::IMAGE_LEFT_COLOR, state->left_frames.at(frame_id));
      ++(*left_count);
    }
    if (accepted && right_enabled) {
      dataset->SaveStreamData(
          ImageType::IMAGE_RIGHT_COLOR, state->right_frames.at(frame_id));
      ++(*right_count);
    }
    if (accepted && depth_enabled) {
      dataset->SaveStreamData(
          ImageType::IMAGE_DEPTH, state->depth_frames.at(frame_id));
      ++(*depth_count);
    }
    if (accepted) {
      ++groups_saved;
    }

    state->left_frames.erase(frame_id);
    state->right_frames.erase(frame_id);
    state->depth_frames.erase(frame_id);
  }

  prune_pending_frames(&state->left_frames);
  prune_pending_frames(&state->right_frames);
  prune_pending_frames(&state->depth_frames);

  return groups_saved;
}

}  // namespace

int main(int argc, char const *argv[]) {
  RecordOptions options;
  try {
    options = parse_args(argc, argv);
  } catch (const std::exception& e) {
    std::cerr << "Error: " << e.what() << std::endl;
    print_usage(argv[0]);
    return 2;
  }

  Camera cam;
  DeviceInfo dev_info;
  if (options.dev_index >= 0) {
    auto devices = cam.GetDeviceInfos();
    if (devices.empty() ||
        static_cast<std::size_t>(options.dev_index) >= devices.size()) {
      std::cerr << "Error: device index out of range: "
                << options.dev_index << std::endl;
      return 1;
    }
    dev_info = devices[options.dev_index];
  } else if (!util::select(cam, &dev_info)) {
    return 1;
  }

  util::print_stream_infos(cam, dev_info.index);
  std::cout << "Open device: " << dev_info.index << ", "
      << dev_info.name << std::endl << std::endl;

  tools::Dataset dataset(options.outdir);

  OpenParams params(dev_info.index);
  params.dev_mode = options.dev_mode;
  params.stream_mode = options.stream_mode;
  params.color_stream_format = options.color_format;
  params.depth_stream_format = options.depth_format;
  params.ir_intensity = static_cast<std::uint8_t>(options.ir_intensity);
  params.framerate = options.framerate;

  cam.EnableImageInfo(true);

  const bool imu_supported = cam.IsMotionDatasSupported();
  const bool record_imu = options.imu && imu_supported;
  if (record_imu) {
    cam.EnableMotionDatas();
  }

  cam.Open(params);

  std::cout << std::endl;
  if (!cam.IsOpened()) {
    std::cerr << "Error: Open camera failed" << std::endl;
    return 1;
  }
  std::cout << "Open device success" << std::endl << std::endl;

  const bool left_enabled =
      options.left && cam.IsStreamDataEnabled(ImageType::IMAGE_LEFT_COLOR);
  const bool right_enabled =
      options.right && cam.IsStreamDataEnabled(ImageType::IMAGE_RIGHT_COLOR);
  const bool depth_enabled =
      options.depth && cam.IsStreamDataEnabled(ImageType::IMAGE_DEPTH);

  save_calibration(&cam, options.outdir, options.stream_mode, record_imu);

  if (options.display) {
    if (left_enabled) cv::namedWindow("left");
    if (right_enabled) cv::namedWindow("right");
    if (depth_enabled) cv::namedWindow("depth");
    std::cout << "Press ESC/Q to terminate" << std::endl;
  } else {
    std::cout << "Press Ctrl-C to terminate";
    if (options.duration_sec > 0) {
      std::cout << ", or wait " << options.duration_sec << " seconds";
    }
    std::cout << std::endl;
  }

  if (options.imu && !imu_supported) {
    std::cout << "IMU is not supported on this device; recording streams only."
              << std::endl;
  }
  if (options.sync_save) {
    std::cout << "Sync save is enabled; only common frame_ids across enabled "
                 "image streams will be written." << std::endl;
  }

  std::size_t left_count = 0;
  std::size_t right_count = 0;
  std::size_t depth_count = 0;
  std::size_t left_received_count = 0;
  std::size_t right_received_count = 0;
  std::size_t depth_received_count = 0;
  std::size_t accel_count = 0;
  std::size_t gyro_count = 0;
  SaveLimiter save_limiter(options.save_rate);
  SyncSaveState sync_save_state;
  auto time_beg = times::now();

  for (;;) {
    cam.WaitForStream();

    if (options.sync_save) {
      save_synced_streams(&cam, &dataset, &sync_save_state,
                          left_enabled, right_enabled,
                          depth_enabled, options.display, &save_limiter,
                          &left_count, &right_count, &depth_count,
                          &left_received_count, &right_received_count,
                          &depth_received_count);
    } else {
      save_streams(&cam, &dataset, ImageType::IMAGE_LEFT_COLOR, left_enabled,
                   options.display, "left", &save_limiter, &left_count,
                   &left_received_count);
      save_streams(&cam, &dataset, ImageType::IMAGE_RIGHT_COLOR, right_enabled,
                   options.display, "right", &save_limiter, &right_count,
                   &right_received_count);
      save_streams(&cam, &dataset, ImageType::IMAGE_DEPTH, depth_enabled,
                   options.display, "depth", &save_limiter, &depth_count,
                   &depth_received_count);
    }

    if (record_imu) {
      auto motion_data = cam.GetMotionDatas();
      for (auto&& motion : motion_data) {
        if (!motion.imu) continue;
        if (motion.imu->flag == MYNTEYE_IMU_ACCEL) {
          ++accel_count;
        } else if (motion.imu->flag == MYNTEYE_IMU_GYRO) {
          ++gyro_count;
        } else if (motion.imu->flag == MYNTEYE_IMU_ACCEL_GYRO_CALIB) {
          ++accel_count;
          ++gyro_count;
        } else {
          continue;
        }
        dataset.SaveMotionData(motion);
      }
    }

    std::cout << "\rSaved "
              << left_count << " left, "
              << right_count << " right, "
              << depth_count << " depth";
    if (record_imu) {
      std::cout << ", " << accel_count << " accels"
          << ", " << gyro_count << " gyros";
    }
    std::cout << std::flush;

    auto time_now = times::now();
    if (options.duration_sec > 0) {
      const float elapsed_ms =
          times::count<times::microseconds>(time_now - time_beg) * 0.001f;
      if (elapsed_ms >= options.duration_sec * 1000.f) {
        break;
      }
    }

    if (options.display) {
      char key = static_cast<char>(cv::waitKey(1));
      if (key == 27 || key == 'q' || key == 'Q') {
        break;
      }
    }
  }
  std::cout << " to " << options.outdir << std::endl;
  auto time_end = times::now();

  cam.Close();

  float elapsed_ms =
      times::count<times::microseconds>(time_end - time_beg) * 0.001f;
  std::cout << "Time beg: " << times::to_local_string(time_beg)
    << ", end: " << times::to_local_string(time_end)
    << ", cost: " << elapsed_ms << "ms" << std::endl;
  std::cout << "Left count: " << left_count
    << ", fps: " << (1000.f * left_count / elapsed_ms)
    << ", received: " << left_received_count << std::endl;
  std::cout << "Right count: " << right_count
    << ", fps: " << (1000.f * right_count / elapsed_ms)
    << ", received: " << right_received_count << std::endl;
  std::cout << "Depth count: " << depth_count
    << ", fps: " << (1000.f * depth_count / elapsed_ms)
    << ", received: " << depth_received_count << std::endl;
  if (record_imu) {
    std::cout << "Accel count: " << accel_count
      << ", hz: " << (1000.f * accel_count / elapsed_ms) << std::endl;
    std::cout << "Gyro count: " << gyro_count
      << ", hz: " << (1000.f * gyro_count / elapsed_ms) << std::endl;
  }

  if (options.display) {
    cv::destroyAllWindows();
  }
  return 0;
}

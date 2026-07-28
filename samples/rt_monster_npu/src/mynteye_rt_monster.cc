#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/viz.hpp>
#include <openvino/openvino.hpp>

#include "mynteyed/camera.h"

MYNTEYE_USE_NAMESPACE

namespace {

using Clock = std::chrono::steady_clock;

struct Args {
  std::string blob;
  int camera_index = 0;
  int fps = 30;
  bool no_sync_check = false;
  bool compare_native = false;
  bool pointcloud = false;
};

void PrintUsage(const char* program) {
  std::cout << "Usage: " << program
            << " --blob MODEL.blob [--camera-index N] [--fps N]"
               " [--no-sync-check] [--compare-native] [--pointcloud]\n"
            << "Keys: q/ESC quit, s save the current left/disparity images\n";
}

Args ParseArgs(int argc, char** argv) {
  Args args;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (arg == "--blob" && i + 1 < argc) {
      args.blob = argv[++i];
    } else if (arg == "--camera-index" && i + 1 < argc) {
      args.camera_index = std::stoi(argv[++i]);
    } else if (arg == "--fps" && i + 1 < argc) {
      args.fps = std::stoi(argv[++i]);
    } else if (arg == "--no-sync-check") {
      args.no_sync_check = true;
    } else if (arg == "--compare-native") {
      args.compare_native = true;
    } else if (arg == "--pointcloud") {
      args.pointcloud = true;
    } else if (arg == "-h" || arg == "--help") {
      PrintUsage(argv[0]);
      std::exit(0);
    } else {
      throw std::runtime_error("Unknown or incomplete argument: " + arg);
    }
  }
  if (args.blob.empty()) throw std::runtime_error("--blob is required");
  return args;
}

void BgrToNchwRgb(const cv::Mat& bgr, ov::Tensor& tensor) {
  const auto shape = tensor.get_shape();
  const int height = static_cast<int>(shape[2]);
  const int width = static_cast<int>(shape[3]);
  cv::Mat resized;
  cv::resize(bgr, resized, cv::Size(width, height), 0, 0, cv::INTER_AREA);

  float* dst = tensor.data<float>();
  const size_t plane = static_cast<size_t>(height) * width;
  for (int y = 0; y < height; ++y) {
    const auto* row = resized.ptr<cv::Vec3b>(y);
    for (int x = 0; x < width; ++x) {
      const size_t p = static_cast<size_t>(y) * width + x;
      dst[p] = static_cast<float>(row[x][2]);
      dst[plane + p] = static_cast<float>(row[x][1]);
      dst[2 * plane + p] = static_cast<float>(row[x][0]);
    }
  }
}

cv::Mat RestoreDisparity(const ov::Tensor& output, const cv::Size& original,
                         int model_width) {
  const auto shape = output.get_shape();
  if (shape.size() < 2) throw std::runtime_error("Unexpected output rank");
  const int out_height = static_cast<int>(shape[shape.size() - 2]);
  const int out_width = static_cast<int>(shape[shape.size() - 1]);
  cv::Mat raw(out_height, out_width, CV_32F,
              const_cast<float*>(output.data<const float>()));
  cv::Mat disparity;
  cv::resize(raw, disparity, original, 0, 0, cv::INTER_LINEAR);
  disparity *= static_cast<float>(original.width) / model_width;
  return disparity;
}

cv::Mat DisparityToDepth(const cv::Mat& disparity, double focal_px,
                         double baseline_mm) {
  cv::Mat depth(disparity.size(), CV_32F, cv::Scalar(0));
  const double fb = focal_px * baseline_mm;
  for (int y = 0; y < disparity.rows; ++y) {
    const auto* dp = disparity.ptr<float>(y);
    auto* zp = depth.ptr<float>(y);
    for (int x = 0; x < disparity.cols; ++x) {
      if (std::isfinite(dp[x]) && dp[x] > 0.0f && dp[x] <= disparity.cols) {
        zp[x] = static_cast<float>(fb / dp[x]);
      }
    }
  }
  return depth;
}

cv::viz::WCloud MakePointCloud(const cv::Mat& depth_mm, const cv::Mat& bgr,
                               double fx, double fy, double cx, double cy,
                               int stride = 4) {
  std::vector<cv::Vec3f> points;
  std::vector<cv::Vec3b> colors;
  points.reserve(depth_mm.total() / (stride * stride));
  colors.reserve(points.capacity());
  for (int y = 0; y < depth_mm.rows; y += stride) {
    const auto* zp = depth_mm.ptr<float>(y);
    const auto* cp = bgr.ptr<cv::Vec3b>(y);
    for (int x = 0; x < depth_mm.cols; x += stride) {
      const float z_mm = zp[x];
      if (!std::isfinite(z_mm) || z_mm < 200.0f || z_mm > 10000.0f) continue;
      const float z = z_mm / 1000.0f;
      points.emplace_back(static_cast<float>((x - cx) * z / fx),
                          static_cast<float>((y - cy) * z / fy), z);
      const auto bgr_pixel = cp[x];
      colors.emplace_back(bgr_pixel[2], bgr_pixel[1], bgr_pixel[0]);
    }
  }
  if (points.empty()) {
    points.emplace_back(0.0f, 0.0f, 0.0f);
    colors.emplace_back(0, 0, 0);
  }
  cv::Mat point_mat(points, true);
  cv::Mat color_mat(colors, true);
  return cv::viz::WCloud(point_mat, color_mat);
}

cv::Mat Colorize(const cv::Mat& disparity) {
  std::vector<float> valid;
  valid.reserve(disparity.total());
  for (const float value : cv::Mat_<float>(disparity)) {
    if (std::isfinite(value)) valid.push_back(value);
  }
  cv::Mat normalized(disparity.size(), CV_8U, cv::Scalar(0));
  if (!valid.empty()) {
    const size_t lo_i = valid.size() * 2 / 100;
    const size_t hi_i = valid.size() * 98 / 100;
    std::nth_element(valid.begin(), valid.begin() + lo_i, valid.end());
    const float low = valid[lo_i];
    std::nth_element(valid.begin(), valid.begin() + hi_i, valid.end());
    const float high = valid[hi_i];
    disparity.convertTo(normalized, CV_8U, 255.0 / std::max(high - low, 1e-6f),
                        -low * 255.0 / std::max(high - low, 1e-6f));
  }
  cv::Mat color;
  cv::applyColorMap(normalized, color, cv::COLORMAP_INFERNO);
  return color;
}

cv::Mat ColorizeRange(const cv::Mat& values, float low, float high,
                      const cv::Mat& valid_mask = cv::Mat()) {
  cv::Mat normalized;
  values.convertTo(normalized, CV_8U, 255.0 / std::max(high - low, 1e-6f),
                   -low * 255.0 / std::max(high - low, 1e-6f));
  if (!valid_mask.empty()) normalized.setTo(0, ~valid_mask);
  cv::Mat color;
  cv::applyColorMap(normalized, color, cv::COLORMAP_INFERNO);
  if (!valid_mask.empty()) color.setTo(cv::Scalar(0, 0, 0), ~valid_mask);
  return color;
}

std::pair<float, float> SharedRange(const cv::Mat& a, const cv::Mat& b,
                                    const cv::Mat& valid) {
  std::vector<float> values;
  values.reserve(a.total() * 2);
  for (int y = 0; y < a.rows; ++y) {
    const auto* ap = a.ptr<float>(y);
    const auto* bp = b.ptr<float>(y);
    const auto* vp = valid.ptr<uint8_t>(y);
    for (int x = 0; x < a.cols; ++x) {
      if (vp[x]) {
        values.push_back(ap[x]);
        values.push_back(bp[x]);
      }
    }
  }
  if (values.empty()) return {0.0f, 1.0f};
  const size_t lo = values.size() * 2 / 100;
  const size_t hi = values.size() * 98 / 100;
  std::nth_element(values.begin(), values.begin() + lo, values.end());
  const float low = values[lo];
  std::nth_element(values.begin(), values.begin() + hi, values.end());
  return {low, std::max(values[hi], low + 1e-6f)};
}

struct ComparisonStats {
  double mae = 0.0;
  double rmse = 0.0;
  double bias = 0.0;
  double median = 0.0;
  double p95 = 0.0;
  double abs_rel = 0.0;
  double bad1 = 0.0;
  double bad3 = 0.0;
  double coverage = 0.0;
  size_t count = 0;
};

ComparisonStats CompareDisparity(const cv::Mat& prediction,
                                 const cv::Mat& reference,
                                 const cv::Mat& valid, cv::Mat* abs_error) {
  cv::absdiff(prediction, reference, *abs_error);
  abs_error->setTo(0, ~valid);
  double sum = 0.0, sum_sq = 0.0, relative_sum = 0.0;
  double signed_sum = 0.0;
  std::vector<float> errors;
  errors.reserve(prediction.total());
  size_t bad1 = 0, bad3 = 0, count = 0;
  for (int y = 0; y < abs_error->rows; ++y) {
    const auto* ep = abs_error->ptr<float>(y);
    const auto* vp = valid.ptr<uint8_t>(y);
    for (int x = 0; x < abs_error->cols; ++x) {
      if (!vp[x]) continue;
      const double e = ep[x];
      signed_sum += prediction.at<float>(y, x) - reference.at<float>(y, x);
      sum += e;
      sum_sq += e * e;
      relative_sum += e / std::max<double>(reference.at<float>(y, x), 1e-6);
      errors.push_back(static_cast<float>(e));
      bad1 += e > 1.0;
      bad3 += e > 3.0;
      ++count;
    }
  }
  ComparisonStats stats;
  stats.count = count;
  stats.coverage = 100.0 * count / prediction.total();
  if (count) {
    stats.mae = sum / count;
    stats.rmse = std::sqrt(sum_sq / count);
    stats.bias = signed_sum / count;
    const size_t mid = errors.size() / 2;
    const size_t p95 = errors.size() * 95 / 100;
    std::nth_element(errors.begin(), errors.begin() + mid, errors.end());
    stats.median = errors[mid];
    std::nth_element(errors.begin(), errors.begin() + p95, errors.end());
    stats.p95 = errors[p95];
    stats.abs_rel = 100.0 * relative_sum / count;
    stats.bad1 = 100.0 * bad1 / count;
    stats.bad3 = 100.0 * bad3 / count;
  }
  return stats;
}

void PutLabel(cv::Mat& image, const std::string& label, int line = 0) {
  const cv::Point origin(12, image.rows - 14 - line * 25);
  cv::putText(image, label, origin, cv::FONT_HERSHEY_SIMPLEX, 0.58,
              cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
  cv::putText(image, label, origin, cv::FONT_HERSHEY_SIMPLEX, 0.58,
              cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
}

void DrawValueGrid(cv::Mat& image, const cv::Mat& values_mm,
                   const cv::Mat& valid, int columns = 12, int rows = 8) {
  for (int gy = 0; gy < rows; ++gy) {
    const int y0 = gy * image.rows / rows;
    const int y1 = (gy + 1) * image.rows / rows;
    for (int gx = 0; gx < columns; ++gx) {
      const int x0 = gx * image.cols / columns;
      const int x1 = (gx + 1) * image.cols / columns;
      double sum = 0.0;
      size_t count = 0;
      for (int y = y0; y < y1; ++y) {
        const auto* ep = values_mm.ptr<float>(y);
        const auto* vp = valid.ptr<uint8_t>(y);
        for (int x = x0; x < x1; ++x) {
          if (vp[x]) {
            sum += ep[x];
            ++count;
          }
        }
      }
      std::string text = "N/A";
      if (count) {
        const double mean_mm = sum / count;
        std::ostringstream os;
        if (mean_mm < 1000.0) {
          os << std::fixed << std::setprecision(0) << mean_mm << "mm";
        } else {
          os << std::fixed << std::setprecision(2) << mean_mm / 1000.0 << "m";
        }
        text = os.str();
      }
      int baseline = 0;
      const auto size = cv::getTextSize(text, cv::FONT_HERSHEY_SIMPLEX, 0.34,
                                        1, &baseline);
      const cv::Point origin((x0 + x1 - size.width) / 2,
                             (y0 + y1 + size.height) / 2);
      cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.34,
                  cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
      cv::putText(image, text, origin, cv::FONT_HERSHEY_SIMPLEX, 0.34,
                  cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
    }
  }
  for (int x = 1; x < columns; ++x) {
    const int px = x * image.cols / columns;
    cv::line(image, cv::Point(px, 0), cv::Point(px, image.rows),
             cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
  }
  for (int y = 1; y < rows; ++y) {
    const int py = y * image.rows / rows;
    cv::line(image, cv::Point(0, py), cv::Point(image.cols, py),
             cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
  }
}

void PutStatus(cv::Mat& image, double infer_ms, double fps, uint16_t frame_id) {
  std::ostringstream text;
  text << std::fixed << std::setprecision(1) << "NPU " << infer_ms
       << " ms | " << fps << " FPS | frame " << frame_id;
  cv::putText(image, text.str(), cv::Point(12, 28), cv::FONT_HERSHEY_SIMPLEX,
              0.65, cv::Scalar(0, 0, 0), 3, cv::LINE_AA);
  cv::putText(image, text.str(), cv::Point(12, 28), cv::FONT_HERSHEY_SIMPLEX,
              0.65, cv::Scalar(255, 255, 255), 1, cv::LINE_AA);
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const Args args = ParseArgs(argc, argv);

    ov::Core core;
    const auto devices = core.get_available_devices();
    if (std::find(devices.begin(), devices.end(), "NPU") == devices.end()) {
      throw std::runtime_error("OpenVINO did not find an NPU");
    }
    std::ifstream blob(args.blob, std::ios::binary);
    if (!blob) throw std::runtime_error("Cannot open blob: " + args.blob);
    ov::CompiledModel model = core.import_model(blob, "NPU");
    if (model.inputs().size() != 2) {
      throw std::runtime_error("The model must have exactly two inputs");
    }
    const auto input_shape = model.input(0).get_shape();
    if (input_shape.size() != 4 || input_shape[1] != 3) {
      throw std::runtime_error("Expected model input layout NCHW with 3 channels");
    }
    const int model_height = static_cast<int>(input_shape[2]);
    const int model_width = static_cast<int>(input_shape[3]);
    ov::InferRequest request = model.create_infer_request();
    ov::Tensor left_tensor(ov::element::f32, input_shape);
    ov::Tensor right_tensor(ov::element::f32, model.input(1).get_shape());

    Camera camera;
    const auto devices_info = camera.GetDeviceInfos();
    auto device = std::find_if(devices_info.begin(), devices_info.end(),
        [&](const DeviceInfo& info) { return info.index == args.camera_index; });
    if (device == devices_info.end()) {
      throw std::runtime_error("MYNT EYE camera index not found: " +
                               std::to_string(args.camera_index));
    }

    OpenParams params(device->index);
    params.framerate = args.fps;
    params.dev_mode = (args.compare_native || args.pointcloud)
                          ? DeviceMode::DEVICE_ALL
                          : DeviceMode::DEVICE_COLOR;
    params.color_mode = ColorMode::COLOR_RECTIFIED;
    params.stream_mode = StreamMode::STREAM_1280x480;
    camera.EnableImageInfo(true);
    camera.Open(params);
    if (!camera.IsOpened()) throw std::runtime_error("Failed to open MYNT EYE");
    if (!camera.IsStreamDataEnabled(ImageType::IMAGE_LEFT_COLOR) ||
        !camera.IsStreamDataEnabled(ImageType::IMAGE_RIGHT_COLOR)) {
      throw std::runtime_error("The selected stream mode did not enable both eyes");
    }

    double native_fx = 0.0, native_fy = 0.0;
    double native_cx = 0.0, native_cy = 0.0;
    double native_baseline_mm = 0.0;
    if (args.compare_native || args.pointcloud) {
      if (!camera.IsStreamDataEnabled(ImageType::IMAGE_DEPTH)) {
        if (args.compare_native) {
          throw std::runtime_error("MYNT native depth stream is not available");
        }
      }
      bool intrinsics_ok = false, extrinsics_ok = false;
      const auto intrinsics = camera.GetStreamIntrinsics(params.stream_mode,
                                                         &intrinsics_ok);
      const auto extrinsics = camera.GetStreamExtrinsics(params.stream_mode,
                                                         &extrinsics_ok);
      if (!intrinsics_ok || std::abs(intrinsics.right.p[0]) < 1e-9) {
        throw std::runtime_error("Could not read MYNT stereo calibration");
      }
      native_fx = intrinsics.left.p[0];
      native_fy = intrinsics.left.p[5];
      native_cx = intrinsics.left.p[2];
      native_cy = intrinsics.left.p[6];
      native_baseline_mm =
          std::abs(intrinsics.right.p[3] / intrinsics.right.p[0]);
      std::cout << "Native calibration: fx=" << native_fx
                << " px, fy=" << intrinsics.left.p[5]
                << " px, cx=" << intrinsics.left.p[2]
                << ", cy=" << intrinsics.left.p[6]
                << ", rectified baseline=" << native_baseline_mm << " mm\n";
      if (extrinsics_ok) {
        std::cout << "Left-to-right translation: ["
                  << extrinsics.translation[0] << ", "
                  << extrinsics.translation[1] << ", "
                  << extrinsics.translation[2] << "] mm\n";
      }
    }

    std::cout << "Camera: " << device->name << " (index " << device->index << ")\n"
              << "Model input: " << model_width << "x" << model_height << " on NPU\n"
              << "Press q/ESC to quit; s to save a snapshot.\n";

    const std::string window_name = "MYNT EYE | RT-MonSter++ NPU";
    cv::namedWindow(window_name, cv::WINDOW_NORMAL);
    cv::resizeWindow(window_name, args.compare_native ? 1600 : 1400,
                     args.compare_native ? 1100 : 700);
    std::unique_ptr<cv::viz::Viz3d> cloud_window;
    if (args.pointcloud) {
      cloud_window = std::make_unique<cv::viz::Viz3d>(
          "RT-MonSter++ RGB point cloud (metres)");
      cloud_window->setBackgroundColor(cv::viz::Color::black());
      cloud_window->showWidget("axes", cv::viz::WCoordinateSystem(0.5));
    }

    double display_fps = 0.0;
    auto fps_start = Clock::now();
    int fps_frames = 0;
    cv::Mat latest_left, latest_disparity, latest_color;
    cv::Mat latest_native_color, latest_error_color, latest_native_depth;
    uint16_t latest_frame_id = 0;

    for (;;) {
      camera.WaitForStream();
      auto left_data = camera.GetStreamData(ImageType::IMAGE_LEFT_COLOR);
      auto right_data = camera.GetStreamData(ImageType::IMAGE_RIGHT_COLOR);
      StreamData depth_data;
      if (args.compare_native) {
        depth_data = camera.GetStreamData(ImageType::IMAGE_DEPTH);
      }
      if (!left_data.img || !right_data.img) continue;
      if (args.compare_native && !depth_data.img) continue;
      if (!args.no_sync_check && left_data.img_info && right_data.img_info &&
          left_data.img_info->frame_id != right_data.img_info->frame_id) {
        continue;
      }
      if (!args.no_sync_check && args.compare_native && left_data.img_info &&
          depth_data.img_info &&
          left_data.img_info->frame_id != depth_data.img_info->frame_id) {
        continue;
      }

      cv::Mat left = left_data.img->To(ImageFormat::COLOR_BGR)->ToMat().clone();
      cv::Mat right = right_data.img->To(ImageFormat::COLOR_BGR)->ToMat().clone();
      if (left.empty() || right.empty() || left.size() != right.size()) continue;
      BgrToNchwRgb(left, left_tensor);
      BgrToNchwRgb(right, right_tensor);
      request.set_input_tensor(0, left_tensor);
      request.set_input_tensor(1, right_tensor);

      const auto infer_start = Clock::now();
      request.infer();
      const double infer_ms = std::chrono::duration<double, std::milli>(
          Clock::now() - infer_start).count();
      cv::Mat disparity = RestoreDisparity(request.get_output_tensor(), left.size(),
                                           model_width);
      cv::Mat model_depth;
      if (args.compare_native || args.pointcloud) {
        model_depth = DisparityToDepth(disparity, native_fx,
                                       native_baseline_mm);
      }
      if (cloud_window && !cloud_window->wasStopped()) {
        cloud_window->showWidget(
            "monster_cloud",
            MakePointCloud(model_depth, left, native_fx, native_fy,
                           native_cx, native_cy));
        cloud_window->spinOnce(1, true);
      }
      cv::Mat color;

      ++fps_frames;
      const double fps_period = std::chrono::duration<double>(Clock::now() - fps_start).count();
      if (fps_period >= 0.5) {
        display_fps = fps_frames / fps_period;
        fps_frames = 0;
        fps_start = Clock::now();
      }
      const uint16_t frame_id = left_data.img_info ? left_data.img_info->frame_id : 0;
      PutStatus(left, infer_ms, display_fps, frame_id);
      cv::Mat view;
      if (!args.compare_native) {
        color = Colorize(disparity);
        PutLabel(left, "Rectified left");
        PutLabel(color, "RT-MonSter++ disparity");
        cv::hconcat(left, color, view);
      } else {
        cv::Mat depth_mm =
            depth_data.img->To(ImageFormat::DEPTH_RAW)->ToMat().clone();
        if (depth_mm.size() != left.size()) {
          cv::resize(depth_mm, depth_mm, left.size(), 0, 0, cv::INTER_NEAREST);
        }
        cv::Mat native_depth;
        depth_mm.convertTo(native_depth, CV_32F);
        // Exclude the camera's invalid/near-saturation codes and pathological
        // network outliers. Both depth maps are evaluated in 0.2-10 metres.
        cv::Mat valid = (depth_mm >= 200) & (depth_mm <= 10000);
        for (int y = 0; y < depth_mm.rows; ++y) {
          auto* vp = valid.ptr<uint8_t>(y);
          const auto* model_z = model_depth.ptr<float>(y);
          for (int x = 0; x < depth_mm.cols; ++x) {
            if (!vp[x] || model_z[x] < 200.0f || model_z[x] > 10000.0f) {
              vp[x] = 0;
            }
          }
        }
        cv::Mat abs_error;
        const auto all_stats = CompareDisparity(model_depth, native_depth, valid,
                                                &abs_error);

        // Native stereo depth is least reliable at occlusion/discontinuity
        // boundaries. Evaluate a second, conservative mask for stable surfaces:
        // reject >2 px local disparity jumps, then erode by two pixels.
        cv::Mat grad_x, grad_y, edge, stable;
        cv::Sobel(native_depth, grad_x, CV_32F, 1, 0, 3);
        cv::Sobel(native_depth, grad_y, CV_32F, 0, 1, 3);
        cv::magnitude(grad_x, grad_y, edge);
        stable = valid & (edge < 400.0f);  // About 100 mm/pixel Sobel gradient.
        cv::erode(stable, stable,
                  cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5)));
        cv::Mat stable_error;
        const auto stats = CompareDisparity(model_depth, native_depth, stable,
                                            &stable_error);
        const auto range = SharedRange(model_depth, native_depth, valid);
        color = ColorizeRange(model_depth, range.first, range.second, valid);
        cv::Mat native_color =
            ColorizeRange(native_depth, range.first, range.second, valid);
        cv::Mat error_color = ColorizeRange(abs_error, 0.0f, 2000.0f, valid);
        DrawValueGrid(color, model_depth, valid);
        DrawValueGrid(native_color, native_depth, valid);
        DrawValueGrid(error_color, abs_error, valid);
        std::ostringstream metrics;
        metrics << std::fixed << std::setprecision(2)
                << "Stable: MAE " << stats.mae / 1000.0 << "m  med "
                << stats.median / 1000.0 << "m  P95 "
                << stats.p95 / 1000.0 << "m";
        std::ostringstream bad;
        bad << std::fixed << std::setprecision(1)
            << "bias " << stats.bias / 1000.0 << "m  AbsRel "
            << stats.abs_rel << "%  coverage " << stats.coverage << "%";
        std::ostringstream all;
        all << std::fixed << std::setprecision(2)
            << "All-valid MAE " << all_stats.mae / 1000.0
            << "m  AbsRel " << all_stats.abs_rel << "%";
        PutLabel(left, "Rectified left");
        PutLabel(color, "RT-MonSter++ depth (shared scale, 0.2-10m)");
        PutLabel(native_color, "MYNT native depth (shared scale, 0.2-10m)");
        PutLabel(error_color, "Grid mean absolute depth error (0-2m scale)");
        cv::Mat top, bottom;
        cv::hconcat(left, color, top);
        cv::hconcat(native_color, error_color, bottom);
        cv::vconcat(top, bottom, view);
        latest_native_color = native_color;
        latest_error_color = error_color;
        latest_native_depth = depth_mm;
        latest_disparity = model_depth;
        if (fps_frames == 0) {
          std::cout << metrics.str() << " | " << bad.str()
                    << " | " << all.str() << '\n';
        }
      }
      cv::imshow(window_name, view);

      latest_left = left;
      if (!args.compare_native) latest_disparity = disparity;
      latest_color = color;
      latest_frame_id = frame_id;
      const int key = cv::waitKey(1) & 0xff;
      if (key == 27 || key == 'q' || key == 'Q') break;
      if (key == 's' || key == 'S') {
        const std::string prefix = "mynteye_" + std::to_string(latest_frame_id);
        cv::imwrite(prefix + "_left.png", latest_left);
        cv::imwrite(prefix + "_disp_color.png", latest_color);
        cv::Mat disp16;
        if (args.compare_native) {
          latest_disparity.convertTo(disp16, CV_16U);
          cv::imwrite(prefix + "_model_depth_mm.png", disp16);
        } else {
          latest_disparity.convertTo(disp16, CV_16U, 256.0);
          cv::imwrite(prefix + "_disp16.png", disp16);
        }
        if (!latest_native_depth.empty()) {
          cv::imwrite(prefix + "_native_depth_mm.png", latest_native_depth);
          cv::imwrite(prefix + "_native_depth_color.png", latest_native_color);
          cv::imwrite(prefix + "_error_color.png", latest_error_color);
        }
        std::cout << "Saved comparison snapshot with prefix " << prefix << '\n';
      }
    }
    camera.Close();
    cv::destroyAllWindows();
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "Error: " << error.what() << '\n';
    return 1;
  } catch (std::runtime_error* error) {
    // MYNT EYE D SDK 1.9.0 throws a pointer for some unsupported USB modes.
    std::cerr << "MYNT SDK error: " << (error ? error->what() : "unknown")
              << "\nThe live L+R+Depth comparison requires a USB 3.0 link. "
                 "Check that lsusb -t reports 5000M or faster.\n";
    delete error;
    return 1;
  }
}

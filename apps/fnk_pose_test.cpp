#include "fnk/camera/media_foundation_camera.hpp"
#include "fnk/nui/depth_synthesizer.hpp"
#include "fnk/nui/frame_store.hpp"
#include "fnk/nui/skeleton_mapper.hpp"
#include "fnk/nui/types.hpp"
#include "fnk/pose/movenet_onnx.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Arguments {
  std::uint32_t device{};
  std::uint64_t frames{60};
  float threshold{0.25F};
  std::filesystem::path model{"tools/pose/models/movenet-singlepose-lightning.onnx"};
  std::filesystem::path output{"artifacts/pose/latest.ppm"};
};

bool parse_arguments(int argc, char** argv, Arguments& arguments) {
  for (int index = 1; index < argc; ++index) {
    if (index + 1 >= argc) {
      return false;
    }
    const std::string_view option(argv[index++]);
    const std::string value(argv[index]);
    try {
      if (option == "--device") {
        arguments.device = static_cast<std::uint32_t>(std::stoul(value));
      } else if (option == "--frames") {
        arguments.frames = std::stoull(value);
      } else if (option == "--threshold") {
        arguments.threshold = std::stof(value);
      } else if (option == "--model") {
        arguments.model = value;
      } else if (option == "--output") {
        arguments.output = value;
      } else {
        return false;
      }
    } catch (...) {
      return false;
    }
  }
  return arguments.frames > 0 && arguments.threshold >= 0.0F && arguments.threshold <= 1.0F;
}

void set_pixel(fnk::camera::CameraFrame& frame, int x, int y, std::uint8_t red,
               std::uint8_t green, std::uint8_t blue) {
  if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) ||
      y >= static_cast<int>(frame.height)) {
    return;
  }
  const std::size_t offset =
      (static_cast<std::size_t>(y) * frame.width + static_cast<std::uint32_t>(x)) * 4U;
  frame.bgra[offset] = blue;
  frame.bgra[offset + 1U] = green;
  frame.bgra[offset + 2U] = red;
}

void draw_line(fnk::camera::CameraFrame& frame, int x0, int y0, int x1, int y1) {
  const int delta_x = std::abs(x1 - x0);
  const int step_x = x0 < x1 ? 1 : -1;
  const int delta_y = -std::abs(y1 - y0);
  const int step_y = y0 < y1 ? 1 : -1;
  int error = delta_x + delta_y;
  for (;;) {
    for (int offset = -1; offset <= 1; ++offset) {
      set_pixel(frame, x0 + offset, y0, 30, 255, 80);
      set_pixel(frame, x0, y0 + offset, 30, 255, 80);
    }
    if (x0 == x1 && y0 == y1) {
      break;
    }
    const int doubled = error * 2;
    if (doubled >= delta_y) {
      error += delta_y;
      x0 += step_x;
    }
    if (doubled <= delta_x) {
      error += delta_x;
      y0 += step_y;
    }
  }
}

void draw_pose(fnk::camera::CameraFrame& frame, const fnk::nui::PoseFrame& pose,
               float threshold) {
  using fnk::nui::Joint;
  constexpr std::array<std::array<Joint, 2>, 19> bones{{
      {Joint::Head, Joint::ShoulderCenter}, {Joint::ShoulderCenter, Joint::Spine},
      {Joint::Spine, Joint::HipCenter}, {Joint::ShoulderCenter, Joint::ShoulderLeft},
      {Joint::ShoulderLeft, Joint::ElbowLeft}, {Joint::ElbowLeft, Joint::WristLeft},
      {Joint::WristLeft, Joint::HandLeft}, {Joint::ShoulderCenter, Joint::ShoulderRight},
      {Joint::ShoulderRight, Joint::ElbowRight}, {Joint::ElbowRight, Joint::WristRight},
      {Joint::WristRight, Joint::HandRight}, {Joint::HipCenter, Joint::HipLeft},
      {Joint::HipLeft, Joint::KneeLeft}, {Joint::KneeLeft, Joint::AnkleLeft},
      {Joint::AnkleLeft, Joint::FootLeft}, {Joint::HipCenter, Joint::HipRight},
      {Joint::HipRight, Joint::KneeRight}, {Joint::KneeRight, Joint::AnkleRight},
      {Joint::AnkleRight, Joint::FootRight},
  }};

  for (const auto& bone : bones) {
    const auto& first = pose.joints[fnk::nui::joint_index(bone[0])];
    const auto& second = pose.joints[fnk::nui::joint_index(bone[1])];
    if (first.confidence < threshold || second.confidence < threshold) {
      continue;
    }
    draw_line(frame, static_cast<int>(first.image.x * (frame.width - 1U)),
              static_cast<int>(first.image.y * (frame.height - 1U)),
              static_cast<int>(second.image.x * (frame.width - 1U)),
              static_cast<int>(second.image.y * (frame.height - 1U)));
  }
}

bool write_ppm(const std::filesystem::path& path, const fnk::camera::CameraFrame& frame) {
  std::error_code error;
  std::filesystem::create_directories(path.parent_path(), error);
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  for (std::size_t index = 0; index < frame.bgra.size(); index += 4U) {
    const char rgb[] = {static_cast<char>(frame.bgra[index + 2U]),
                        static_cast<char>(frame.bgra[index + 1U]),
                        static_cast<char>(frame.bgra[index])};
    output.write(rgb, sizeof(rgb));
  }
  return output.good();
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!parse_arguments(argc, argv, arguments)) {
    std::cerr << "Usage: fnk_pose_test [--device N] [--frames N] [--threshold 0..1] "
                 "[--model file.onnx] [--output frame.ppm]\n";
    return EXIT_FAILURE;
  }

  fnk::pose::MoveNetOnnx estimator(arguments.model);
  if (!estimator.ready()) {
    std::cerr << "Could not load pose model: " << estimator.last_error() << '\n';
    return EXIT_FAILURE;
  }

  fnk::camera::MediaFoundationCamera camera;
  if (!camera.start({arguments.device, 640, 480, 30})) {
    std::cerr << "Could not start camera: " << camera.last_error() << '\n';
    return EXIT_FAILURE;
  }

  fnk::camera::CameraFrame frame;
  fnk::camera::CameraFrame annotated;
  fnk::nui::PoseFrame pose;
  fnk::nui::SkeletonMapper skeleton_mapper;
  fnk::nui::DepthSynthesizer depth_synthesizer;
  fnk::nui::NuiFrameStore nui_frames;
  std::uint64_t previous_sequence = 0;
  std::uint64_t inferred_frames = 0;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
  while (inferred_frames < arguments.frames && std::chrono::steady_clock::now() < deadline) {
    if (!camera.latest_frame(frame) || frame.sequence == previous_sequence) {
      if (!camera.running()) {
        break;
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    previous_sequence = frame.sequence;
    if (!estimator.infer(frame, pose)) {
      std::cerr << "Pose inference failed: " << estimator.last_error() << '\n';
      camera.stop();
      return EXIT_FAILURE;
    }
    const auto skeleton = skeleton_mapper.map(pose);
    const auto depth = depth_synthesizer.synthesize(pose, skeleton);
    nui_frames.publish(skeleton, depth);
    annotated = frame;
    draw_pose(annotated, pose, arguments.threshold);
    ++inferred_frames;
  }
  camera.stop();

  if (inferred_frames != arguments.frames) {
    std::cerr << "Timed out after " << inferred_frames << " inferred frames.\n";
    return EXIT_FAILURE;
  }
  if (!write_ppm(arguments.output, annotated)) {
    std::cerr << "Could not write " << arguments.output.string() << '\n';
    return EXIT_FAILURE;
  }

  const auto stats = estimator.stats();
  const auto latest_nui = nui_frames.latest();
  std::cout << "MoveNet input: " << stats.input_width << 'x' << stats.input_height << '\n'
            << "Inferred frames: " << stats.frames_processed << '\n'
            << "Last inference: " << stats.last_inference_ms << " ms\n"
            << "Average inference: " << stats.average_inference_ms << " ms ("
            << (stats.average_inference_ms > 0.0 ? 1000.0 / stats.average_inference_ms : 0.0)
            << " FPS)\n";
  if (latest_nui) {
    std::cout << "NUI frame: " << latest_nui->skeleton.frame_number.value() << " ("
              << latest_nui->depth_width << 'x' << latest_nui->depth_height << ", "
              << latest_nui->depth_pixels.size() << " guest bytes)\n";
  }
  std::cout
            << "Saved annotated frame to " << arguments.output.string() << '\n';
  return EXIT_SUCCESS;
}

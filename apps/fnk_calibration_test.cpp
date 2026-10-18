#include "fnk/camera/media_foundation_camera.hpp"
#include "fnk/nui/game_space_calibration.hpp"
#include "fnk/nui/skeleton_mapper.hpp"
#include "fnk/pose/movenet_onnx.hpp"

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

namespace {

struct Arguments {
  std::uint32_t device{};
  std::size_t samples{30};
  float confidence{0.30F};
  float reference_depth_m{2.0F};
  std::uint32_t timeout_seconds{45};
  std::filesystem::path model{"tools/pose/models/movenet-singlepose-lightning.onnx"};
  std::filesystem::path output{"artifacts/calibration/droidcam.toml"};
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
      } else if (option == "--samples") {
        arguments.samples = static_cast<std::size_t>(std::stoull(value));
      } else if (option == "--confidence") {
        arguments.confidence = std::stof(value);
      } else if (option == "--depth") {
        arguments.reference_depth_m = std::stof(value);
      } else if (option == "--timeout") {
        arguments.timeout_seconds = static_cast<std::uint32_t>(std::stoul(value));
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
  return arguments.samples > 0 && arguments.timeout_seconds > 0 &&
         arguments.confidence >= 0.0F && arguments.confidence <= 1.0F &&
         arguments.reference_depth_m >= 0.5F && arguments.reference_depth_m <= 4.0F;
}

const char* quality_name(fnk::nui::CalibrationQuality quality) {
  return quality == fnk::nui::CalibrationQuality::FullBody ? "full-body" : "torso-only";
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!parse_arguments(argc, argv, arguments)) {
    std::cerr << "Usage: fnk_calibration_test [--device N] [--samples N] "
                 "[--confidence 0..1] [--depth metres] [--timeout seconds] "
                 "[--model file.onnx] "
                 "[--output profile.toml]\n";
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

  fnk::nui::GameSpaceCalibrationOptions calibration_options;
  calibration_options.required_samples = arguments.samples;
  calibration_options.minimum_confidence = arguments.confidence;
  calibration_options.reference_depth_m = arguments.reference_depth_m;
  fnk::nui::GameSpaceCalibrator calibrator(calibration_options);
  fnk::camera::CameraFrame frame;
  fnk::nui::PoseFrame pose;
  std::uint64_t previous_sequence = 0;
  std::size_t last_reported = 0;
  const auto deadline = std::chrono::steady_clock::now() +
                        std::chrono::seconds(arguments.timeout_seconds);

  std::cout << "Stand facing the camera and hold still. Keep shoulders and hips visible; "
               "show both ankles for full-body floor calibration.\n";
  while (!calibrator.complete() && std::chrono::steady_clock::now() < deadline) {
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
    calibrator.add_sample(pose, frame.width, frame.height);
    if (calibrator.accepted_samples() != last_reported &&
        (calibrator.accepted_samples() % 5U == 0U || calibrator.complete())) {
      last_reported = calibrator.accepted_samples();
      std::cout << "Calibration samples: " << last_reported << '/' << arguments.samples << '\n';
    }
  }
  camera.stop();

  if (!calibrator.complete()) {
    std::cerr << "Calibration incomplete: " << calibrator.accepted_samples() << '/'
              << arguments.samples << " valid samples, " << calibrator.rejected_samples()
              << " rejected. Reframe the DroidCam so shoulders and hips are visible.\n";
    return EXIT_FAILURE;
  }

  std::string error;
  const auto& profile = calibrator.profile();
  if (!fnk::nui::save_game_space_profile(profile, arguments.output, &error)) {
    std::cerr << error << '\n';
    return EXIT_FAILURE;
  }

  fnk::nui::GameSpaceProfile loaded_profile;
  if (!fnk::nui::load_game_space_profile(arguments.output, loaded_profile, &error)) {
    std::cerr << "Saved profile could not be loaded: " << error << '\n';
    return EXIT_FAILURE;
  }

  fnk::nui::SkeletonMapper mapper;
  mapper.set_game_space_profile(loaded_profile);
  const auto skeleton = mapper.map(pose);
  std::cout << "Calibration complete (" << quality_name(profile.quality) << ")\n"
            << "  player center x: " << profile.center_x << '\n'
            << "  floor y: " << profile.floor_y << '\n'
            << "  normalized torso: " << profile.reference_torso_normalized << '\n'
            << "  reference depth: " << profile.reference_depth_m << " m\n"
            << "  mapped hip: (" << skeleton.position_m.x << ", " << skeleton.position_m.y
            << ", " << skeleton.position_m.z << ") m\n"
            << "Saved profile to " << arguments.output.string() << '\n';
  return EXIT_SUCCESS;
}

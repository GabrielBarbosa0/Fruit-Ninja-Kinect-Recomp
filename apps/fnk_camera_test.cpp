#include "fnk/camera/media_foundation_camera.hpp"

#include <chrono>
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
  bool list_only{};
  std::uint32_t device{};
  std::uint32_t width{640};
  std::uint32_t height{480};
  std::uint32_t fps{30};
  std::uint64_t frames{60};
  std::filesystem::path output{};
};

bool parse_unsigned(const char* value, std::uint64_t& destination) {
  try {
    std::size_t consumed = 0;
    const std::string text(value);
    destination = std::stoull(text, &consumed);
    return consumed == text.size();
  } catch (...) {
    return false;
  }
}

bool parse_arguments(int argc, char** argv, Arguments& arguments) {
  for (int index = 1; index < argc; ++index) {
    const std::string_view argument(argv[index]);
    if (argument == "--list") {
      arguments.list_only = true;
      continue;
    }
    if (index + 1 >= argc) {
      return false;
    }
    if (argument == "--output") {
      arguments.output = argv[++index];
      continue;
    }

    std::uint64_t value = 0;
    if (!parse_unsigned(argv[++index], value)) {
      return false;
    }
    if (argument == "--device") {
      arguments.device = static_cast<std::uint32_t>(value);
    } else if (argument == "--width") {
      arguments.width = static_cast<std::uint32_t>(value);
    } else if (argument == "--height") {
      arguments.height = static_cast<std::uint32_t>(value);
    } else if (argument == "--fps") {
      arguments.fps = static_cast<std::uint32_t>(value);
    } else if (argument == "--frames") {
      arguments.frames = value;
    } else {
      return false;
    }
  }
  return true;
}

bool write_ppm(const std::filesystem::path& path,
               const fnk::camera::CameraFrame& frame) {
  std::ofstream output(path, std::ios::binary);
  if (!output) {
    return false;
  }
  output << "P6\n" << frame.width << ' ' << frame.height << "\n255\n";
  for (std::size_t index = 0; index < frame.bgra.size(); index += 4) {
    const char rgb[] = {static_cast<char>(frame.bgra[index + 2]),
                        static_cast<char>(frame.bgra[index + 1]),
                        static_cast<char>(frame.bgra[index])};
    output.write(rgb, sizeof(rgb));
  }
  return output.good();
}

void print_usage() {
  std::cerr << "Usage: fnk_camera_test [--list] [--device N] [--width N] "
               "[--height N] [--fps N] [--frames N] [--output frame.ppm]\n";
}

}  // namespace

int main(int argc, char** argv) {
  Arguments arguments;
  if (!parse_arguments(argc, argv, arguments)) {
    print_usage();
    return EXIT_FAILURE;
  }

  std::string error;
  const auto devices = fnk::camera::MediaFoundationCamera::enumerate(&error);
  if (!error.empty()) {
    std::cerr << error << '\n';
    return EXIT_FAILURE;
  }
  std::cout << "Cameras: " << devices.size() << '\n';
  for (const auto& device : devices) {
    std::cout << "  [" << device.index << "] " << device.name << '\n';
  }
  if (arguments.list_only) {
    return EXIT_SUCCESS;
  }
  if (devices.empty()) {
    std::cerr << "No camera found.\n";
    return EXIT_FAILURE;
  }

  fnk::camera::MediaFoundationCamera camera;
  if (!camera.start({arguments.device, arguments.width, arguments.height, arguments.fps})) {
    std::cerr << camera.last_error() << '\n';
    return EXIT_FAILURE;
  }

  fnk::camera::CameraFrame frame;
  const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
  while (std::chrono::steady_clock::now() < deadline) {
    if (camera.latest_frame(frame) && frame.sequence >= arguments.frames) {
      break;
    }
    if (!camera.running()) {
      break;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
  }
  camera.stop();

  if (frame.sequence < arguments.frames) {
    std::cerr << "Capture stopped at frame " << frame.sequence << ": "
              << camera.last_error() << '\n';
    return EXIT_FAILURE;
  }

  const auto stats = camera.stats();
  std::cout << "Captured " << stats.frames_captured << " frames at " << stats.width << 'x'
            << stats.height << " (" << stats.capture_fps << " measured FPS)\n";
  if (!arguments.output.empty()) {
    if (!write_ppm(arguments.output, frame)) {
      std::cerr << "Could not write " << arguments.output.string() << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "Saved diagnostic frame to " << arguments.output.string() << '\n';
  }
  return EXIT_SUCCESS;
}

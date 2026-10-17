#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace fnk::camera {

struct CameraDeviceInfo {
  std::uint32_t index{};
  std::string name{};
  std::string id{};
};

struct CameraOptions {
  std::uint32_t device_index{};
  std::uint32_t requested_width{640};
  std::uint32_t requested_height{480};
  std::uint32_t requested_fps{30};
};

struct CameraFrame {
  std::uint64_t sequence{};
  std::uint64_t timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  // Tightly packed top-to-bottom BGRA8. Alpha is normalized to 255.
  std::vector<std::uint8_t> bgra{};
};

struct CameraStats {
  std::uint64_t frames_captured{};
  double capture_fps{};
  std::uint32_t width{};
  std::uint32_t height{};
};

class MediaFoundationCamera {
 public:
  MediaFoundationCamera();
  ~MediaFoundationCamera();

  MediaFoundationCamera(const MediaFoundationCamera&) = delete;
  MediaFoundationCamera& operator=(const MediaFoundationCamera&) = delete;

  static std::vector<CameraDeviceInfo> enumerate(std::string* error = nullptr);

  bool start(const CameraOptions& options = {});
  void stop() noexcept;

  bool running() const noexcept;
  bool latest_frame(CameraFrame& destination) const;
  CameraStats stats() const noexcept;
  std::string last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fnk::camera

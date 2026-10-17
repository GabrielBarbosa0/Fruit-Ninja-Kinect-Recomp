#pragma once

#include "fnk/camera/media_foundation_camera.hpp"
#include "fnk/nui/types.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

namespace fnk::pose {

struct PoseInferenceStats {
  std::uint64_t frames_processed{};
  double last_inference_ms{};
  double average_inference_ms{};
  std::uint32_t input_width{};
  std::uint32_t input_height{};
};

class MoveNetOnnx {
 public:
  explicit MoveNetOnnx(const std::filesystem::path& model_path);
  ~MoveNetOnnx();

  MoveNetOnnx(const MoveNetOnnx&) = delete;
  MoveNetOnnx& operator=(const MoveNetOnnx&) = delete;

  bool ready() const noexcept;
  bool infer(const camera::CameraFrame& frame, nui::PoseFrame& pose);
  PoseInferenceStats stats() const noexcept;
  std::string last_error() const;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

}  // namespace fnk::pose

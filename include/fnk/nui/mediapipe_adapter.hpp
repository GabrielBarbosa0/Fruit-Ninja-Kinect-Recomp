#pragma once

#include "fnk/nui/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fnk::nui {

// MediaPipe Pose Landmarker's stable 33-landmark order.
enum class MediaPipeLandmark : std::uint8_t {
  Nose = 0,
  LeftEyeInner,
  LeftEye,
  LeftEyeOuter,
  RightEyeInner,
  RightEye,
  RightEyeOuter,
  LeftEar,
  RightEar,
  MouthLeft,
  MouthRight,
  LeftShoulder,
  RightShoulder,
  LeftElbow,
  RightElbow,
  LeftWrist,
  RightWrist,
  LeftPinky,
  RightPinky,
  LeftIndex,
  RightIndex,
  LeftThumb,
  RightThumb,
  LeftHip,
  RightHip,
  LeftKnee,
  RightKnee,
  LeftAnkle,
  RightAnkle,
  LeftHeel,
  RightHeel,
  LeftFootIndex,
  RightFootIndex,
  Count,
};

inline constexpr std::size_t kMediaPipeLandmarkCount =
    static_cast<std::size_t>(MediaPipeLandmark::Count);

constexpr std::size_t landmark_index(MediaPipeLandmark landmark) noexcept {
  return static_cast<std::size_t>(landmark);
}

struct MediaPipePoseLandmark {
  float x{};
  float y{};
  float z{};
  float visibility{1.0F};
  float presence{1.0F};
};

struct MediaPipePoseFrame {
  std::uint64_t timestamp_us{};
  std::uint32_t tracking_id{1};
  std::array<MediaPipePoseLandmark, kMediaPipeLandmarkCount> landmarks{};
};

struct MediaPipeAdapterOptions {
  // Set when the detector receives a mirrored preview rather than the original
  // camera frame. This unmirrors x while preserving anatomical left/right.
  bool mirror_x{};
};

class MediaPipeAdapter {
 public:
  explicit MediaPipeAdapter(MediaPipeAdapterOptions options = {}) : options_(options) {}

  PoseFrame convert(const MediaPipePoseFrame& source) const noexcept;

 private:
  MediaPipeAdapterOptions options_{};
};

}  // namespace fnk::nui

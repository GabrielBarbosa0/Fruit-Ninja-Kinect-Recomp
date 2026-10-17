#pragma once

#include "fnk/nui/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace fnk::nui {

// COCO's 17-keypoint order, used by MoveNet SinglePose.
enum class MoveNetKeypoint : std::uint8_t {
  Nose = 0,
  LeftEye,
  RightEye,
  LeftEar,
  RightEar,
  LeftShoulder,
  RightShoulder,
  LeftElbow,
  RightElbow,
  LeftWrist,
  RightWrist,
  LeftHip,
  RightHip,
  LeftKnee,
  RightKnee,
  LeftAnkle,
  RightAnkle,
  Count,
};

inline constexpr std::size_t kMoveNetKeypointCount =
    static_cast<std::size_t>(MoveNetKeypoint::Count);

constexpr std::size_t keypoint_index(MoveNetKeypoint keypoint) noexcept {
  return static_cast<std::size_t>(keypoint);
}

struct MoveNetPoint {
  float x{};
  float y{};
  float confidence{};
};

struct MoveNetPoseFrame {
  std::uint64_t timestamp_us{};
  std::uint32_t tracking_id{1};
  std::array<MoveNetPoint, kMoveNetKeypointCount> keypoints{};
};

class MoveNetAdapter {
 public:
  PoseFrame convert(const MoveNetPoseFrame& source) const noexcept;
};

}  // namespace fnk::nui

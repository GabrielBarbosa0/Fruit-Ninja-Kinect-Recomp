#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace fnk::nui {

// The numeric order matches Kinect v1's NUI_SKELETON_POSITION_INDEX. Keeping
// this stable will make guest-structure marshalling a direct indexed copy.
enum class Joint : std::uint8_t {
  HipCenter = 0,
  Spine,
  ShoulderCenter,
  Head,
  ShoulderLeft,
  ElbowLeft,
  WristLeft,
  HandLeft,
  ShoulderRight,
  ElbowRight,
  WristRight,
  HandRight,
  HipLeft,
  KneeLeft,
  AnkleLeft,
  FootLeft,
  HipRight,
  KneeRight,
  AnkleRight,
  FootRight,
  Count,
};

inline constexpr std::size_t kJointCount = static_cast<std::size_t>(Joint::Count);

constexpr std::size_t joint_index(Joint joint) noexcept {
  return static_cast<std::size_t>(joint);
}

enum class JointTrackingState : std::uint32_t {
  NotTracked = 0,
  Inferred = 1,
  Tracked = 2,
};

enum class SkeletonTrackingState : std::uint32_t {
  NotTracked = 0,
  PositionOnly = 1,
  Tracked = 2,
};

struct Vec2 {
  float x{};
  float y{};
};

struct Vec3 {
  float x{};
  float y{};
  float z{};
};

// Detector-neutral input. x/y are normalized image coordinates in [0, 1].
// relative_depth is an optional model-relative offset: positive is farther
// from the camera. confidence is also normalized to [0, 1].
struct PoseJoint {
  Vec2 image{};
  float relative_depth{};
  float confidence{};
};

struct PoseFrame {
  std::uint64_t timestamp_us{};
  std::uint32_t tracking_id{1};
  std::array<PoseJoint, kJointCount> joints{};
};

struct SkeletonJoint {
  Vec3 position_m{};
  JointTrackingState state{JointTrackingState::NotTracked};
};

struct SkeletonFrame {
  std::uint64_t timestamp_us{};
  std::uint32_t tracking_id{};
  SkeletonTrackingState state{SkeletonTrackingState::NotTracked};
  Vec3 position_m{};
  std::array<SkeletonJoint, kJointCount> joints{};
};

class PoseProvider {
 public:
  virtual ~PoseProvider() = default;
  virtual bool poll(PoseFrame& frame) = 0;
};

}  // namespace fnk::nui

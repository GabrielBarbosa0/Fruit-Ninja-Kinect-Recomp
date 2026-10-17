#pragma once

#include "fnk/nui/game_space_calibration.hpp"
#include "fnk/nui/types.hpp"

#include <array>
#include <optional>

namespace fnk::nui {

struct CameraCalibration {
  float horizontal_fov_degrees{57.0F};
  float vertical_fov_degrees{43.0F};
  float reference_torso_length_m{0.55F};
  float minimum_depth_m{0.8F};
  float maximum_depth_m{4.0F};
  float relative_depth_scale_m{0.35F};
};

struct TrackingThresholds {
  float inferred_confidence{0.25F};
  float tracked_confidence{0.60F};
  std::size_t minimum_tracked_joints{6};
};

// Parameters mirror the Kinect v1 transform-smoothing model. Radii use metres.
struct SmoothingParameters {
  float smoothing{0.50F};
  float correction{0.50F};
  float prediction{0.50F};
  float jitter_radius_m{0.05F};
  float maximum_deviation_radius_m{0.04F};
};

class SkeletonMapper {
 public:
  SkeletonMapper() = default;
  SkeletonMapper(CameraCalibration calibration, TrackingThresholds thresholds,
                 SmoothingParameters smoothing);

  SkeletonFrame map(const PoseFrame& pose);
  void reset() noexcept;
  void set_game_space_profile(const GameSpaceProfile& profile);
  void clear_game_space_profile() noexcept;

  const CameraCalibration& calibration() const noexcept { return calibration_; }
  const std::optional<GameSpaceProfile>& game_space_profile() const noexcept {
    return game_space_profile_;
  }

 private:
  struct JointHistory {
    Vec3 filtered{};
    Vec3 trend{};
    bool initialized{};
  };

  float estimate_depth(const PoseFrame& pose) const noexcept;
  Vec3 project(const PoseJoint& joint, float body_depth_m) const noexcept;
  Vec3 smooth(std::size_t index, Vec3 raw);

  CameraCalibration calibration_{};
  TrackingThresholds thresholds_{};
  SmoothingParameters smoothing_{};
  std::optional<GameSpaceProfile> game_space_profile_{};
  std::array<JointHistory, kJointCount> history_{};
  std::uint32_t active_tracking_id_{};
};

}  // namespace fnk::nui

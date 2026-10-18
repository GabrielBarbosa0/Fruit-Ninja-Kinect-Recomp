#include "fnk/nui/skeleton_mapper.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace fnk::nui {
namespace {

constexpr float kEpsilon = 1.0e-5F;

float clamp01(float value) noexcept {
  return std::clamp(value, 0.0F, 1.0F);
}

Vec3 add(Vec3 left, Vec3 right) noexcept {
  return {left.x + right.x, left.y + right.y, left.z + right.z};
}

Vec3 subtract(Vec3 left, Vec3 right) noexcept {
  return {left.x - right.x, left.y - right.y, left.z - right.z};
}

Vec3 scale(Vec3 value, float factor) noexcept {
  return {value.x * factor, value.y * factor, value.z * factor};
}

Vec3 lerp(Vec3 from, Vec3 to, float amount) noexcept {
  return add(scale(from, 1.0F - amount), scale(to, amount));
}

float length(Vec3 value) noexcept {
  return std::sqrt(value.x * value.x + value.y * value.y + value.z * value.z);
}

Vec3 clamp_distance(Vec3 value, Vec3 origin, float maximum_distance) noexcept {
  const Vec3 offset = subtract(value, origin);
  const float distance = length(offset);
  if (distance <= maximum_distance || distance <= kEpsilon) {
    return value;
  }
  return add(origin, scale(offset, maximum_distance / distance));
}

float radians(float degrees) noexcept {
  return degrees * std::numbers::pi_v<float> / 180.0F;
}

float image_distance(const PoseJoint& first, const PoseJoint& second) noexcept {
  const float dx = first.image.x - second.image.x;
  const float dy = first.image.y - second.image.y;
  return std::sqrt(dx * dx + dy * dy);
}

}  // namespace

SkeletonMapper::SkeletonMapper(CameraCalibration calibration,
                               TrackingThresholds thresholds,
                               SmoothingParameters smoothing)
    : calibration_(calibration), thresholds_(thresholds), smoothing_(smoothing) {}

void SkeletonMapper::reset() noexcept {
  history_ = {};
  active_tracking_id_ = 0;
}

void SkeletonMapper::set_game_space_profile(const GameSpaceProfile& profile) {
  if (profile.valid()) {
    game_space_profile_ = profile;
    reset();
  }
}

void SkeletonMapper::clear_game_space_profile() noexcept {
  game_space_profile_.reset();
  reset();
}

float SkeletonMapper::estimate_depth(const PoseFrame& pose) const noexcept {
  const PoseJoint& shoulder_center = pose.joints[joint_index(Joint::ShoulderCenter)];
  const PoseJoint& hip_center = pose.joints[joint_index(Joint::HipCenter)];

  float normalized_torso = 0.0F;
  if (shoulder_center.confidence >= thresholds_.inferred_confidence &&
      hip_center.confidence >= thresholds_.inferred_confidence) {
    normalized_torso = image_distance(shoulder_center, hip_center);
  } else {
    const PoseJoint& shoulder_left = pose.joints[joint_index(Joint::ShoulderLeft)];
    const PoseJoint& shoulder_right = pose.joints[joint_index(Joint::ShoulderRight)];
    const PoseJoint& hip_left = pose.joints[joint_index(Joint::HipLeft)];
    const PoseJoint& hip_right = pose.joints[joint_index(Joint::HipRight)];
    const float confidence = std::min(
        {shoulder_left.confidence, shoulder_right.confidence, hip_left.confidence,
         hip_right.confidence});
    if (confidence >= thresholds_.inferred_confidence) {
      const PoseJoint shoulders{{(shoulder_left.image.x + shoulder_right.image.x) * 0.5F,
                                 (shoulder_left.image.y + shoulder_right.image.y) * 0.5F},
                                0.0F, confidence};
      const PoseJoint hips{{(hip_left.image.x + hip_right.image.x) * 0.5F,
                            (hip_left.image.y + hip_right.image.y) * 0.5F},
                           0.0F, confidence};
      normalized_torso = image_distance(shoulders, hips);
    }
  }

  if (normalized_torso <= kEpsilon) {
    return (calibration_.minimum_depth_m + calibration_.maximum_depth_m) * 0.5F;
  }

  if (game_space_profile_ && game_space_profile_->valid()) {
    return std::clamp(game_space_profile_->reference_depth_m *
                          game_space_profile_->reference_torso_normalized / normalized_torso,
                      calibration_.minimum_depth_m, calibration_.maximum_depth_m);
  }

  const float half_height_at_unit_depth =
      std::tan(radians(calibration_.vertical_fov_degrees) * 0.5F);
  const float depth = calibration_.reference_torso_length_m /
                      (2.0F * normalized_torso * half_height_at_unit_depth);
  return std::clamp(depth, calibration_.minimum_depth_m, calibration_.maximum_depth_m);
}

Vec3 SkeletonMapper::project(const PoseJoint& joint, float body_depth_m) const noexcept {
  const float depth = std::clamp(
      body_depth_m + joint.relative_depth * calibration_.relative_depth_scale_m,
      calibration_.minimum_depth_m, calibration_.maximum_depth_m);

  if (game_space_profile_ && game_space_profile_->valid()) {
    const GameSpaceProfile& profile = *game_space_profile_;
    const float reference_scale =
        profile.reference_torso_length_m / profile.reference_torso_normalized;
    const float distance_scale = depth / profile.reference_depth_m;
    const float vertical_scale = reference_scale * distance_scale;
    const float game_depth =
        depth * profile.game_reference_depth_m / profile.reference_depth_m;
    return {(joint.image.x - profile.center_x) * vertical_scale *
                profile.image_aspect_ratio,
            (profile.floor_y - joint.image.y) * vertical_scale, game_depth};
  }

  const float half_width = depth * std::tan(radians(calibration_.horizontal_fov_degrees) * 0.5F);
  const float half_height = depth * std::tan(radians(calibration_.vertical_fov_degrees) * 0.5F);

  return {(joint.image.x - 0.5F) * 2.0F * half_width,
          (0.5F - joint.image.y) * 2.0F * half_height, depth};
}

Vec3 SkeletonMapper::smooth(std::size_t index, Vec3 raw) {
  JointHistory& history = history_[index];
  if (!history.initialized) {
    history.filtered = raw;
    history.trend = {};
    history.initialized = true;
    return raw;
  }

  const bool is_hand = index == joint_index(Joint::HandLeft) ||
                       index == joint_index(Joint::HandRight);
  const float jitter_radius =
      is_hand ? smoothing_.hand_jitter_radius_m : smoothing_.jitter_radius_m;
  const float smoothing = is_hand ? smoothing_.hand_smoothing : smoothing_.smoothing;
  const float correction = is_hand ? smoothing_.hand_correction : smoothing_.correction;
  const float prediction = is_hand ? smoothing_.hand_prediction : smoothing_.prediction;
  const float maximum_deviation = is_hand ? smoothing_.hand_maximum_deviation_radius_m
                                          : smoothing_.maximum_deviation_radius_m;

  Vec3 jitter_filtered = raw;
  const float raw_delta = length(subtract(raw, history.filtered));
  if (jitter_radius > kEpsilon && raw_delta <= jitter_radius) {
    jitter_filtered = lerp(history.filtered, raw, raw_delta / jitter_radius);
  }

  const Vec3 predicted_from_history = add(history.filtered, history.trend);
  const Vec3 filtered =
      lerp(jitter_filtered, predicted_from_history, clamp01(smoothing));
  const Vec3 difference = subtract(filtered, history.filtered);
  const Vec3 trend = lerp(history.trend, difference, clamp01(correction));
  Vec3 predicted = add(filtered, scale(trend, std::max(0.0F, prediction)));

  predicted = clamp_distance(predicted, raw, std::max(0.0F, maximum_deviation));
  history.filtered = filtered;
  history.trend = trend;
  return predicted;
}

SkeletonFrame SkeletonMapper::map(const PoseFrame& pose) {
  if (active_tracking_id_ != 0 && active_tracking_id_ != pose.tracking_id) {
    reset();
  }
  active_tracking_id_ = pose.tracking_id;

  SkeletonFrame result{};
  result.timestamp_us = pose.timestamp_us;
  result.tracking_id = pose.tracking_id;

  const float body_depth = estimate_depth(pose);
  std::size_t visible_joint_count = 0;
  std::size_t tracked_joint_count = 0;

  for (std::size_t index = 0; index < kJointCount; ++index) {
    const PoseJoint& source = pose.joints[index];
    SkeletonJoint& destination = result.joints[index];
    const float confidence = clamp01(source.confidence);

    const bool is_hand = index == joint_index(Joint::HandLeft) ||
                         index == joint_index(Joint::HandRight);
    const float tracked_confidence =
        is_hand ? thresholds_.hand_tracked_confidence : thresholds_.tracked_confidence;
    if (confidence >= tracked_confidence) {
      destination.state = JointTrackingState::Tracked;
      ++tracked_joint_count;
      ++visible_joint_count;
    } else if (confidence >= thresholds_.inferred_confidence) {
      destination.state = JointTrackingState::Inferred;
      ++visible_joint_count;
    } else {
      destination.state = JointTrackingState::NotTracked;
      history_[index] = {};
      continue;
    }

    destination.position_m = smooth(index, project(source, body_depth));
  }

  const SkeletonJoint& hip_center = result.joints[joint_index(Joint::HipCenter)];
  if (hip_center.state != JointTrackingState::NotTracked) {
    result.position_m = hip_center.position_m;
  }

  if (tracked_joint_count >= thresholds_.minimum_tracked_joints) {
    result.state = SkeletonTrackingState::Tracked;
  } else if (visible_joint_count > 0) {
    result.state = SkeletonTrackingState::PositionOnly;
  }

  return result;
}

}  // namespace fnk::nui

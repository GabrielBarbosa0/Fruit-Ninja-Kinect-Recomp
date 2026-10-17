#include "fnk/nui/depth_synthesizer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <utility>

namespace fnk::nui {
namespace {

struct Bone {
  Joint first;
  Joint second;
  bool torso{};
};

constexpr std::array<Bone, 19> kBones{{
    {Joint::HipCenter, Joint::Spine, true},
    {Joint::Spine, Joint::ShoulderCenter, true},
    {Joint::ShoulderCenter, Joint::Head, true},
    {Joint::ShoulderCenter, Joint::ShoulderLeft, true},
    {Joint::ShoulderLeft, Joint::ElbowLeft, false},
    {Joint::ElbowLeft, Joint::WristLeft, false},
    {Joint::WristLeft, Joint::HandLeft, false},
    {Joint::ShoulderCenter, Joint::ShoulderRight, true},
    {Joint::ShoulderRight, Joint::ElbowRight, false},
    {Joint::ElbowRight, Joint::WristRight, false},
    {Joint::WristRight, Joint::HandRight, false},
    {Joint::HipCenter, Joint::HipLeft, true},
    {Joint::HipLeft, Joint::KneeLeft, false},
    {Joint::KneeLeft, Joint::AnkleLeft, false},
    {Joint::AnkleLeft, Joint::FootLeft, false},
    {Joint::HipCenter, Joint::HipRight, true},
    {Joint::HipRight, Joint::KneeRight, false},
    {Joint::KneeRight, Joint::AnkleRight, false},
    {Joint::AnkleRight, Joint::FootRight, false},
}};

struct PixelPoint {
  float x{};
  float y{};
};

PixelPoint pixel_position(const PoseJoint& joint, std::uint32_t width,
                          std::uint32_t height) noexcept {
  return {joint.image.x * static_cast<float>(width - 1),
          joint.image.y * static_cast<float>(height - 1)};
}

bool visible(const SkeletonJoint& joint) noexcept {
  return joint.state != JointTrackingState::NotTracked;
}

std::uint16_t metres_to_millimetres(float metres) noexcept {
  return static_cast<std::uint16_t>(
      std::clamp(std::lround(metres * 1000.0F), 1L, 8191L));
}

void draw_capsule(DepthPlayerFrame& frame, PixelPoint first, PixelPoint second,
                  float radius, std::uint16_t packed_depth) {
  const float min_x = std::min(first.x, second.x) - radius;
  const float max_x = std::max(first.x, second.x) + radius;
  const float min_y = std::min(first.y, second.y) - radius;
  const float max_y = std::max(first.y, second.y) + radius;

  const int x_begin = std::max(0, static_cast<int>(std::floor(min_x)));
  const int x_end = std::min(static_cast<int>(frame.width) - 1,
                             static_cast<int>(std::ceil(max_x)));
  const int y_begin = std::max(0, static_cast<int>(std::floor(min_y)));
  const int y_end = std::min(static_cast<int>(frame.height) - 1,
                             static_cast<int>(std::ceil(max_y)));

  const float dx = second.x - first.x;
  const float dy = second.y - first.y;
  const float length_squared = dx * dx + dy * dy;
  const float radius_squared = radius * radius;

  for (int y = y_begin; y <= y_end; ++y) {
    for (int x = x_begin; x <= x_end; ++x) {
      const float px = static_cast<float>(x) + 0.5F;
      const float py = static_cast<float>(y) + 0.5F;
      float amount = 0.0F;
      if (length_squared > 1.0e-5F) {
        amount = std::clamp(((px - first.x) * dx + (py - first.y) * dy) /
                                length_squared,
                            0.0F, 1.0F);
      }
      const float nearest_x = first.x + amount * dx;
      const float nearest_y = first.y + amount * dy;
      const float offset_x = px - nearest_x;
      const float offset_y = py - nearest_y;
      if (offset_x * offset_x + offset_y * offset_y > radius_squared) {
        continue;
      }

      std::uint16_t& target = frame.packed_depth[
          static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)];
      if (target == 0 || unpack_depth_mm(packed_depth) < unpack_depth_mm(target)) {
        target = packed_depth;
      }
    }
  }
}

}  // namespace

DepthPlayerFrame DepthSynthesizer::synthesize(const PoseFrame& pose,
                                               const SkeletonFrame& skeleton,
                                               std::uint8_t player_index) const {
  DepthPlayerFrame result{};
  result.timestamp_us = pose.timestamp_us;
  result.width = std::max(1U, options_.width);
  result.height = std::max(1U, options_.height);
  result.packed_depth.resize(static_cast<std::size_t>(result.width) * result.height);

  if (skeleton.state == SkeletonTrackingState::NotTracked) {
    return result;
  }

  const float torso_radius = options_.torso_radius_ratio * static_cast<float>(result.height);
  const float limb_radius = options_.limb_radius_ratio * static_cast<float>(result.height);

  for (const Bone& bone : kBones) {
    const std::size_t first_index = joint_index(bone.first);
    const std::size_t second_index = joint_index(bone.second);
    const SkeletonJoint& first_joint = skeleton.joints[first_index];
    const SkeletonJoint& second_joint = skeleton.joints[second_index];
    if (!visible(first_joint) || !visible(second_joint)) {
      continue;
    }

    const float depth_m = (first_joint.position_m.z + second_joint.position_m.z) * 0.5F;
    draw_capsule(result, pixel_position(pose.joints[first_index], result.width, result.height),
                 pixel_position(pose.joints[second_index], result.width, result.height),
                 bone.torso ? torso_radius : limb_radius,
                 pack_depth_player(metres_to_millimetres(depth_m), player_index));
  }

  for (const auto [joint, radius_ratio] :
       std::array<std::pair<Joint, float>, 5>{{
           {Joint::Head, options_.head_radius_ratio},
           {Joint::HandLeft, options_.extremity_radius_ratio},
           {Joint::HandRight, options_.extremity_radius_ratio},
           {Joint::FootLeft, options_.extremity_radius_ratio},
           {Joint::FootRight, options_.extremity_radius_ratio},
       }}) {
    const std::size_t index = joint_index(joint);
    if (!visible(skeleton.joints[index])) {
      continue;
    }
    const PixelPoint center = pixel_position(pose.joints[index], result.width, result.height);
    draw_capsule(result, center, center, radius_ratio * static_cast<float>(result.height),
                 pack_depth_player(
                     metres_to_millimetres(skeleton.joints[index].position_m.z), player_index));
  }

  return result;
}

}  // namespace fnk::nui

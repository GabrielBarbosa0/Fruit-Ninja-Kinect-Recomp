#include "fnk/nui/movenet_adapter.hpp"

#include <algorithm>
#include <initializer_list>

namespace fnk::nui {
namespace {

PoseJoint copy_point(const MoveNetPoint& point) noexcept {
  return {{point.x, point.y}, 0.0F, std::clamp(point.confidence, 0.0F, 1.0F)};
}

PoseJoint average(std::initializer_list<const MoveNetPoint*> points) noexcept {
  PoseJoint result{};
  float weight_sum = 0.0F;
  for (const MoveNetPoint* point : points) {
    const float weight = std::max(std::clamp(point->confidence, 0.0F, 1.0F), 0.01F);
    result.image.x += point->x * weight;
    result.image.y += point->y * weight;
    result.confidence = std::max(result.confidence, point->confidence);
    weight_sum += weight;
  }
  if (weight_sum > 0.0F) {
    result.image.x /= weight_sum;
    result.image.y /= weight_sum;
  }
  result.confidence = std::clamp(result.confidence, 0.0F, 1.0F);
  return result;
}

PoseJoint midpoint(const PoseJoint& first, const PoseJoint& second) noexcept {
  return {{(first.image.x + second.image.x) * 0.5F,
           (first.image.y + second.image.y) * 0.5F},
          0.0F, std::min(first.confidence, second.confidence)};
}

}  // namespace

PoseFrame MoveNetAdapter::convert(const MoveNetPoseFrame& source) const noexcept {
  PoseFrame destination{};
  destination.timestamp_us = source.timestamp_us;
  destination.tracking_id = source.tracking_id;

  const auto point = [&source](MoveNetKeypoint keypoint) -> const MoveNetPoint& {
    return source.keypoints[keypoint_index(keypoint)];
  };
  const auto assign = [&destination, &point](Joint joint, MoveNetKeypoint keypoint) {
    destination.joints[joint_index(joint)] = copy_point(point(keypoint));
  };

  destination.joints[joint_index(Joint::Head)] =
      average({&point(MoveNetKeypoint::Nose), &point(MoveNetKeypoint::LeftEye),
               &point(MoveNetKeypoint::RightEye), &point(MoveNetKeypoint::LeftEar),
               &point(MoveNetKeypoint::RightEar)});

  assign(Joint::ShoulderLeft, MoveNetKeypoint::LeftShoulder);
  assign(Joint::ShoulderRight, MoveNetKeypoint::RightShoulder);
  assign(Joint::ElbowLeft, MoveNetKeypoint::LeftElbow);
  assign(Joint::ElbowRight, MoveNetKeypoint::RightElbow);
  assign(Joint::WristLeft, MoveNetKeypoint::LeftWrist);
  assign(Joint::WristRight, MoveNetKeypoint::RightWrist);
  assign(Joint::HipLeft, MoveNetKeypoint::LeftHip);
  assign(Joint::HipRight, MoveNetKeypoint::RightHip);
  assign(Joint::KneeLeft, MoveNetKeypoint::LeftKnee);
  assign(Joint::KneeRight, MoveNetKeypoint::RightKnee);
  assign(Joint::AnkleLeft, MoveNetKeypoint::LeftAnkle);
  assign(Joint::AnkleRight, MoveNetKeypoint::RightAnkle);

  destination.joints[joint_index(Joint::HandLeft)] =
      destination.joints[joint_index(Joint::WristLeft)];
  destination.joints[joint_index(Joint::HandRight)] =
      destination.joints[joint_index(Joint::WristRight)];
  destination.joints[joint_index(Joint::FootLeft)] =
      destination.joints[joint_index(Joint::AnkleLeft)];
  destination.joints[joint_index(Joint::FootRight)] =
      destination.joints[joint_index(Joint::AnkleRight)];

  const PoseJoint shoulder_center =
      midpoint(destination.joints[joint_index(Joint::ShoulderLeft)],
               destination.joints[joint_index(Joint::ShoulderRight)]);
  const PoseJoint hip_center = midpoint(destination.joints[joint_index(Joint::HipLeft)],
                                        destination.joints[joint_index(Joint::HipRight)]);
  destination.joints[joint_index(Joint::ShoulderCenter)] = shoulder_center;
  destination.joints[joint_index(Joint::HipCenter)] = hip_center;
  destination.joints[joint_index(Joint::Spine)] = midpoint(shoulder_center, hip_center);
  return destination;
}

}  // namespace fnk::nui

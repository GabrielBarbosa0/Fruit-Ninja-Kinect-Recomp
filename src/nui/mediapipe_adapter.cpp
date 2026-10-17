#include "fnk/nui/mediapipe_adapter.hpp"

#include <algorithm>
#include <initializer_list>

namespace fnk::nui {
namespace {

float confidence(const MediaPipePoseLandmark& landmark) noexcept {
  return std::clamp(std::min(landmark.visibility, landmark.presence), 0.0F, 1.0F);
}

PoseJoint copy_joint(const MediaPipePoseLandmark& landmark, bool mirror_x) noexcept {
  return {{mirror_x ? 1.0F - landmark.x : landmark.x, landmark.y}, landmark.z,
          confidence(landmark)};
}

PoseJoint average(std::initializer_list<const MediaPipePoseLandmark*> landmarks,
                  bool mirror_x) noexcept {
  PoseJoint result{};
  if (landmarks.size() == 0) {
    return result;
  }

  result.confidence = 1.0F;
  for (const MediaPipePoseLandmark* landmark : landmarks) {
    result.image.x += mirror_x ? 1.0F - landmark->x : landmark->x;
    result.image.y += landmark->y;
    result.relative_depth += landmark->z;
    result.confidence = std::min(result.confidence, confidence(*landmark));
  }

  const float divisor = static_cast<float>(landmarks.size());
  result.image.x /= divisor;
  result.image.y /= divisor;
  result.relative_depth /= divisor;
  return result;
}

}  // namespace

PoseFrame MediaPipeAdapter::convert(const MediaPipePoseFrame& source) const noexcept {
  PoseFrame destination{};
  destination.timestamp_us = source.timestamp_us;
  destination.tracking_id = source.tracking_id;

  const auto landmark = [&source](MediaPipeLandmark index) -> const MediaPipePoseLandmark& {
    return source.landmarks[landmark_index(index)];
  };
  const auto assign = [&destination, &landmark, this](Joint joint,
                                                      MediaPipeLandmark source_joint) {
    destination.joints[joint_index(joint)] =
        copy_joint(landmark(source_joint), options_.mirror_x);
  };

  const PoseJoint shoulder_center =
      average({&landmark(MediaPipeLandmark::LeftShoulder),
               &landmark(MediaPipeLandmark::RightShoulder)},
              options_.mirror_x);
  const PoseJoint hip_center = average(
      {&landmark(MediaPipeLandmark::LeftHip), &landmark(MediaPipeLandmark::RightHip)},
      options_.mirror_x);

  destination.joints[joint_index(Joint::Head)] =
      average({&landmark(MediaPipeLandmark::Nose), &landmark(MediaPipeLandmark::LeftEar),
               &landmark(MediaPipeLandmark::RightEar)},
              options_.mirror_x);
  destination.joints[joint_index(Joint::ShoulderCenter)] = shoulder_center;
  destination.joints[joint_index(Joint::HipCenter)] = hip_center;

  PoseJoint spine{};
  spine.image = {(shoulder_center.image.x + hip_center.image.x) * 0.5F,
                 (shoulder_center.image.y + hip_center.image.y) * 0.5F};
  spine.relative_depth =
      (shoulder_center.relative_depth + hip_center.relative_depth) * 0.5F;
  spine.confidence = std::min(shoulder_center.confidence, hip_center.confidence);
  destination.joints[joint_index(Joint::Spine)] = spine;

  assign(Joint::ShoulderLeft, MediaPipeLandmark::LeftShoulder);
  assign(Joint::ElbowLeft, MediaPipeLandmark::LeftElbow);
  assign(Joint::WristLeft, MediaPipeLandmark::LeftWrist);
  destination.joints[joint_index(Joint::HandLeft)] =
      average({&landmark(MediaPipeLandmark::LeftPinky),
               &landmark(MediaPipeLandmark::LeftIndex),
               &landmark(MediaPipeLandmark::LeftThumb)},
              options_.mirror_x);

  assign(Joint::ShoulderRight, MediaPipeLandmark::RightShoulder);
  assign(Joint::ElbowRight, MediaPipeLandmark::RightElbow);
  assign(Joint::WristRight, MediaPipeLandmark::RightWrist);
  destination.joints[joint_index(Joint::HandRight)] =
      average({&landmark(MediaPipeLandmark::RightPinky),
               &landmark(MediaPipeLandmark::RightIndex),
               &landmark(MediaPipeLandmark::RightThumb)},
              options_.mirror_x);

  assign(Joint::HipLeft, MediaPipeLandmark::LeftHip);
  assign(Joint::KneeLeft, MediaPipeLandmark::LeftKnee);
  assign(Joint::AnkleLeft, MediaPipeLandmark::LeftAnkle);
  destination.joints[joint_index(Joint::FootLeft)] =
      average({&landmark(MediaPipeLandmark::LeftHeel),
               &landmark(MediaPipeLandmark::LeftFootIndex)},
              options_.mirror_x);

  assign(Joint::HipRight, MediaPipeLandmark::RightHip);
  assign(Joint::KneeRight, MediaPipeLandmark::RightKnee);
  assign(Joint::AnkleRight, MediaPipeLandmark::RightAnkle);
  destination.joints[joint_index(Joint::FootRight)] =
      average({&landmark(MediaPipeLandmark::RightHeel),
               &landmark(MediaPipeLandmark::RightFootIndex)},
              options_.mirror_x);

  return destination;
}

}  // namespace fnk::nui

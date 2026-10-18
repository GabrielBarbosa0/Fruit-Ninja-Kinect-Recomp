#include "fnk/nui/depth_synthesizer.hpp"
#include "fnk/nui/dispatcher.hpp"
#include "fnk/nui/game_space_calibration.hpp"
#include "fnk/nui/frame_store.hpp"
#include "fnk/nui/guest_abi.hpp"
#include "fnk/nui/guest_marshaller.hpp"
#include "fnk/nui/mediapipe_adapter.hpp"
#include "fnk/nui/movenet_adapter.hpp"
#include "fnk/nui/skeleton_mapper.hpp"

#include <cmath>
#include <cstring>
#include <cstdlib>
#include <iostream>
#include <string_view>

namespace {

using fnk::nui::Joint;
using fnk::nui::JointTrackingState;
using fnk::nui::DepthSynthesizer;
using fnk::nui::MediaPipeAdapter;
using fnk::nui::MediaPipeAdapterOptions;
using fnk::nui::MediaPipeLandmark;
using fnk::nui::MediaPipePoseFrame;
using fnk::nui::MoveNetAdapter;
using fnk::nui::MoveNetKeypoint;
using fnk::nui::MoveNetPoseFrame;
using fnk::nui::PoseFrame;
using fnk::nui::PoseJoint;
using fnk::nui::SkeletonMapper;
using fnk::nui::SkeletonTrackingState;
using fnk::nui::CalibrationQuality;
using fnk::nui::GameSpaceCalibrationOptions;
using fnk::nui::GameSpaceCalibrator;
using fnk::nui::joint_index;

int failures = 0;

class TestGuestMemory final : public fnk::nui::GuestMemory {
 public:
  TestGuestMemory() : bytes_(2U * 1024U * 1024U) {}

  bool write(fnk::nui::guest::Address address,
             std::span<const std::uint8_t> bytes) override {
    if (address > bytes_.size() || bytes.size() > bytes_.size() - address) {
      return false;
    }
    std::memcpy(bytes_.data() + address, bytes.data(), bytes.size());
    return true;
  }

  fnk::nui::guest::Address allocate(std::size_t size, std::size_t alignment) override {
    const std::size_t aligned = (next_ + alignment - 1U) & ~(alignment - 1U);
    if (aligned > bytes_.size() || size > bytes_.size() - aligned) {
      return 0;
    }
    next_ = aligned + size;
    return static_cast<fnk::nui::guest::Address>(aligned);
  }

  void release(fnk::nui::guest::Address) override {}

  std::uint32_t read_u32(fnk::nui::guest::Address address) const {
    return (static_cast<std::uint32_t>(bytes_[address]) << 24U) |
           (static_cast<std::uint32_t>(bytes_[address + 1U]) << 16U) |
           (static_cast<std::uint32_t>(bytes_[address + 2U]) << 8U) |
           bytes_[address + 3U];
  }

  const std::uint8_t* at(fnk::nui::guest::Address address) const {
    return bytes_.data() + address;
  }

 private:
  std::vector<std::uint8_t> bytes_;
  std::size_t next_{0x10000U};
};

void expect(bool condition, std::string_view message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

bool near(float actual, float expected, float tolerance = 1.0e-3F) {
  return std::abs(actual - expected) <= tolerance;
}

void set_joint(PoseFrame& frame, Joint joint, float x, float y, float confidence = 1.0F,
               float relative_depth = 0.0F) {
  frame.joints[joint_index(joint)] = PoseJoint{{x, y}, relative_depth, confidence};
}

PoseFrame make_pose() {
  PoseFrame frame{};
  frame.timestamp_us = 1'000'000;
  frame.tracking_id = 7;

  set_joint(frame, Joint::Head, 0.50F, 0.18F);
  set_joint(frame, Joint::ShoulderCenter, 0.50F, 0.30F);
  set_joint(frame, Joint::Spine, 0.50F, 0.43F);
  set_joint(frame, Joint::HipCenter, 0.50F, 0.55F);
  set_joint(frame, Joint::ShoulderLeft, 0.40F, 0.31F);
  set_joint(frame, Joint::ElbowLeft, 0.33F, 0.40F);
  set_joint(frame, Joint::WristLeft, 0.27F, 0.48F);
  set_joint(frame, Joint::HandLeft, 0.23F, 0.50F);
  set_joint(frame, Joint::ShoulderRight, 0.60F, 0.31F);
  set_joint(frame, Joint::ElbowRight, 0.67F, 0.40F);
  set_joint(frame, Joint::WristRight, 0.73F, 0.48F);
  set_joint(frame, Joint::HandRight, 0.77F, 0.50F);
  set_joint(frame, Joint::HipLeft, 0.45F, 0.55F);
  set_joint(frame, Joint::KneeLeft, 0.44F, 0.73F);
  set_joint(frame, Joint::AnkleLeft, 0.43F, 0.91F);
  set_joint(frame, Joint::FootLeft, 0.40F, 0.95F);
  set_joint(frame, Joint::HipRight, 0.55F, 0.55F);
  set_joint(frame, Joint::KneeRight, 0.56F, 0.73F);
  set_joint(frame, Joint::AnkleRight, 0.57F, 0.91F);
  set_joint(frame, Joint::FootRight, 0.60F, 0.95F);
  return frame;
}

void test_joint_layout() {
  expect(joint_index(Joint::HipCenter) == 0, "HipCenter ABI index");
  expect(joint_index(Joint::HandRight) == 11, "HandRight ABI index");
  expect(joint_index(Joint::FootRight) == 19, "FootRight ABI index");
  expect(fnk::nui::kJointCount == 20, "Kinect v1 has 20 joints");
}

void test_guest_abi_encoding() {
  fnk::nui::guest::U32 word{};
  word.set(0x1234ABCDU);
  expect(word.bytes[0] == 0x12 && word.bytes[1] == 0x34 && word.bytes[2] == 0xAB &&
             word.bytes[3] == 0xCD,
         "guest U32 is encoded big-endian");
  expect(word.value() == 0x1234ABCDU, "guest U32 round trip");

  fnk::nui::guest::F32 scalar{};
  scalar.set(1.25F);
  expect(near(scalar.value(), 1.25F), "guest float round trip");

  fnk::nui::guest::SkeletonFrame frame{};
  frame.skeleton_data[2].tracking_id.set(0x10203040U);
  expect(frame.skeleton_data[2].tracking_id.value() == 0x10203040U,
         "skeleton tracking ID round trip");
  expect(sizeof(frame) == 0xAB0, "recovered skeleton frame size");
  expect(sizeof(fnk::nui::guest::ImageFrame) == 0x28, "recovered image frame size");
}

void test_guest_marshalling() {
  fnk::nui::SkeletonFrame source{};
  source.timestamp_us = 1'234'567;
  source.tracking_id = 0x10203040U;
  source.state = SkeletonTrackingState::Tracked;
  source.position_m = {0.25F, 1.0F, 2.0F};
  auto& hand = source.joints[joint_index(Joint::HandRight)];
  hand.state = JointTrackingState::Tracked;
  hand.position_m = {0.75F, 1.25F, 1.5F};

  fnk::nui::SkeletonMarshalOptions options{};
  options.frame_number = 17;
  options.skeleton_slot = 2;
  options.user_index = 3;
  options.quality_flags = 5;
  const auto guest_frame = fnk::nui::marshal_skeleton_frame(source, options);
  const auto& guest_skeleton = guest_frame.skeleton_data[2];

  expect(guest_frame.timestamp.value() == 1234, "NUI timestamp uses milliseconds");
  expect(guest_frame.frame_number.value() == 17, "guest frame number is preserved");
  expect(guest_frame.skeleton_data[0].tracking_state.value() == 0,
         "unused guest skeleton slots are clear");
  expect(guest_skeleton.tracking_state.value() == 2, "tracked skeleton state is marshalled");
  expect(guest_skeleton.tracking_id.value() == 0x10203040U,
         "guest tracking ID is marshalled");
  expect(guest_skeleton.user_index.value() == 3, "guest user index is marshalled");
  expect(guest_skeleton.quality_flags.value() == 5, "guest quality flags are marshalled");
  expect(near(guest_skeleton.position.x.value(), 0.25F) &&
             near(guest_skeleton.position.w.value(), 1.0F),
         "guest skeleton position is a homogeneous Vector4");
  expect(guest_skeleton.position_tracking_states[joint_index(Joint::HandRight)].value() == 2,
         "joint tracking state is marshalled");
  expect(near(guest_skeleton.positions[joint_index(Joint::HandRight)].z.value(), 1.5F),
         "joint position is marshalled");

  const auto* bytes = reinterpret_cast<const std::uint8_t*>(&guest_frame);
  const std::size_t tracking_id_offset = 0x30U + 2U * 0x1C0U + 0x04U;
  expect(bytes[tracking_id_offset] == 0x10 && bytes[tracking_id_offset + 1] == 0x20 &&
             bytes[tracking_id_offset + 2] == 0x30 && bytes[tracking_id_offset + 3] == 0x40,
         "tracking ID lands at the recovered big-endian byte offset");

  fnk::nui::ImageFrameMarshalOptions image_options{};
  image_options.timestamp_us = 9'999;
  image_options.frame_number = 4;
  image_options.image_type = fnk::nui::guest::ImageType::Color;
  image_options.resolution = fnk::nui::guest::ImageResolution::R640x480;
  image_options.frame_texture = 0x82001000U;
  image_options.center_x = -7;
  const auto image = fnk::nui::marshal_image_frame(image_options);
  expect(image.timestamp.value() == 9 && image.frame_number.value() == 4,
         "image identity is marshalled");
  expect(image.image_type.value() == 1 && image.resolution.value() == 2,
         "image type and resolution are marshalled");
  expect(image.frame_texture.value() == 0x82001000U, "frame texture guest pointer is preserved");
  expect(image.view_area.center_x.value() == 0xFFFFFFF9U, "signed view offset keeps its bits");

  fnk::nui::DepthPlayerFrame depth{};
  depth.packed_depth = {0x1234U, 0xABCDU};
  const auto depth_bytes = fnk::nui::marshal_depth_pixels(depth);
  expect(depth_bytes.size() == 4 && depth_bytes[0] == 0x12 && depth_bytes[1] == 0x34 &&
             depth_bytes[2] == 0xAB && depth_bytes[3] == 0xCD,
         "16-bit depth pixels are marshalled big-endian");
}

void test_latest_frame_store() {
  fnk::nui::NuiFrameStore store;
  expect(!store.latest(), "frame store starts empty");

  fnk::nui::SkeletonFrame skeleton{};
  skeleton.timestamp_us = 10'000;
  skeleton.tracking_id = 1;
  skeleton.state = SkeletonTrackingState::Tracked;
  fnk::nui::DepthPlayerFrame depth{};
  depth.timestamp_us = skeleton.timestamp_us;
  depth.width = 2;
  depth.height = 1;
  depth.packed_depth = {0x0102U, 0x0304U};

  const std::uint64_t first_sequence = store.publish(skeleton, depth);
  const auto first = store.latest_after(0);
  expect(first && first->sequence == first_sequence, "first complete frame is published");
  expect(first && first->skeleton.frame_number.value() == 1,
         "frame store assigns the first NUI frame number");
  expect(first && first->depth_width == 2 && first->depth_height == 1 &&
             first->depth_pixels[0] == 0x01 && first->depth_pixels[3] == 0x04,
         "frame store publishes matching depth bytes and dimensions");
  expect(!store.latest_after(first_sequence), "consumed sequence is not returned again");

  skeleton.timestamp_us = 20'000;
  depth.timestamp_us = skeleton.timestamp_us;
  const std::uint64_t second_sequence = store.publish(skeleton, depth);
  const auto second = store.latest_after(first_sequence);
  expect(second && second->sequence == second_sequence &&
             second->skeleton.frame_number.value() == 2,
         "new publication atomically replaces the previous packet");
  expect(first && first->skeleton.timestamp.value() == 10,
         "readers retain an immutable snapshot after replacement");

  store.clear();
  expect(!store.latest(), "frame store can be cleared during NUI shutdown");
}

void test_nui_dispatcher() {
  TestGuestMemory memory;
  fnk::nui::NuiFrameStore store;
  fnk::nui::NuiDispatcher dispatcher(memory, store);

  fnk::nui::SkeletonFrame skeleton{};
  skeleton.timestamp_us = 42'000;
  skeleton.tracking_id = 0x11223344U;
  skeleton.state = SkeletonTrackingState::Tracked;
  skeleton.position_m = {0.0F, 1.0F, 2.0F};
  fnk::nui::DepthPlayerFrame depth{};
  depth.timestamp_us = skeleton.timestamp_us;
  depth.width = 320;
  depth.height = 240;
  depth.packed_depth.assign(320U * 240U, 0x1234U);
  store.publish(skeleton, depth);

  using namespace fnk::nui::guest;
  expect(dispatcher.initialize(kTitleInitializeWithoutColor, kDefaultHardwareThread) == kSOk,
         "NUI dispatcher initializes with the title mask");
  expect(dispatcher.skeleton_tracking_enable(0x55U,
                                              kSkeletonTrackingTitleSetsTrackedSkeletons) == kSOk,
         "NUI skeleton tracking is enabled");
  expect(dispatcher.skeleton_get_next_frame(0, 0x100U) == kSOk,
         "NuiSkeletonGetNextFrame writes the newest packet");
  expect(memory.read_u32(0x100U + 0x30U + 0x04U) == 0x11223344U,
         "dispatcher writes tracking ID at the recovered guest offset");
  expect(dispatcher.skeleton_get_next_frame(0, 0x100U) == kSFalse,
         "title-managed skeleton polling returns S_FALSE without a new frame");

  expect(dispatcher.image_stream_open(ImageType::DepthAndPlayerIndex,
                                      ImageResolution::R320x240, 0, 1, 0x66U,
                                      0x20U) == kSOk,
         "depth stream opens with the title parameters");
  const Handle stream = memory.read_u32(0x20U);
  expect(dispatcher.image_stream_get_next_frame(stream, 0, 0x24U) == kSOk,
         "NuiImageStreamGetNextFrame publishes depth metadata");
  const Address image_frame = memory.read_u32(0x24U);
  expect(image_frame != 0 && memory.read_u32(image_frame + 0x0CU) == 0 &&
             memory.read_u32(image_frame + 0x10U) == 1,
         "published NUI image frame has depth type and 320x240 resolution");
  const Address texture = memory.read_u32(image_frame + 0x14U);
  expect(dispatcher.texture_lock_rect(texture, 0, 0x30U, 0, 16) == kSOk,
         "synthetic NUI texture supports LockRect");
  const std::uint32_t pitch = memory.read_u32(0x30U);
  const Address pixels = memory.read_u32(0x34U);
  expect(pitch == 640 && pixels != 0, "depth LockRect returns pitch and guest bits pointer");
  expect(memory.at(pixels)[0] == 0x12 && memory.at(pixels)[1] == 0x34,
         "depth texture contains big-endian pixels");
  expect(dispatcher.texture_unlock_rect(texture, 0) == kSOk,
         "synthetic NUI texture supports UnlockRect");
  expect(dispatcher.image_stream_release_frame(stream, image_frame) == kSOk,
         "NuiImageStreamReleaseFrame releases the outstanding frame");
  expect(dispatcher.image_stream_get_next_frame(stream, 0, 0x24U) == kENuiFrameNoData,
         "depth polling reports no data until a newer packet arrives");

  expect(dispatcher.image_stream_open(ImageType::Color, ImageResolution::R640x480,
                                      0, 1, 0x77U, 0x28U) == kENuiStreamNotEnabled,
         "color stream respects the initialization feature mask");
  dispatcher.shutdown();
}

void test_projection_and_tracking() {
  SkeletonMapper mapper;
  const auto result = mapper.map(make_pose());

  expect(result.state == SkeletonTrackingState::Tracked, "complete pose is tracked");
  expect(result.tracking_id == 7, "tracking ID is preserved");
  expect(near(result.position_m.x, 0.0F), "center hip projects to x=0");
  expect(result.position_m.z >= 0.8F && result.position_m.z <= 4.0F,
         "estimated depth is calibrated and clamped");

  const auto& left = result.joints[joint_index(Joint::HandLeft)];
  const auto& right = result.joints[joint_index(Joint::HandRight)];
  expect(left.position_m.x < 0.0F, "image-left hand maps to negative camera x");
  expect(right.position_m.x > 0.0F, "image-right hand maps to positive camera x");
  expect(left.state == JointTrackingState::Tracked, "high-confidence joint is tracked");
}

void test_confidence_states() {
  auto pose = make_pose();
  pose.joints[joint_index(Joint::HandLeft)].confidence = 0.40F;
  pose.joints[joint_index(Joint::HandRight)].confidence = 0.10F;

  SkeletonMapper mapper;
  const auto result = mapper.map(pose);
  expect(result.joints[joint_index(Joint::HandLeft)].state == JointTrackingState::Tracked,
         "medium-confidence hand remains tracked during fast motion");
  expect(result.joints[joint_index(Joint::HandRight)].state == JointTrackingState::NotTracked,
         "low confidence becomes not tracked");
}

void test_smoothing_and_tracking_reset() {
  SkeletonMapper mapper;
  auto first_pose = make_pose();
  const auto first = mapper.map(first_pose);

  auto jittered_pose = first_pose;
  jittered_pose.timestamp_us += 16'667;
  jittered_pose.joints[joint_index(Joint::HandRight)].image.x += 0.01F;
  const auto jittered = mapper.map(jittered_pose);

  const float raw_motion = 0.01F * 2.0F * first.position_m.z *
                           std::tan(57.0F * 3.14159265358979323846F / 360.0F);
  const float filtered_motion =
      jittered.joints[joint_index(Joint::HandRight)].position_m.x -
      first.joints[joint_index(Joint::HandRight)].position_m.x;
  expect(filtered_motion > 0.0F && filtered_motion < raw_motion,
         "small image jitter is reduced without reversing motion");

  auto swiped_pose = first_pose;
  swiped_pose.timestamp_us += 33'334;
  swiped_pose.joints[joint_index(Joint::HandRight)].image.x += 0.20F;
  const auto swiped = mapper.map(swiped_pose);
  const float swipe_motion =
      swiped.joints[joint_index(Joint::HandRight)].position_m.x -
      jittered.joints[joint_index(Joint::HandRight)].position_m.x;
  expect(swipe_motion > raw_motion * 8.0F,
         "fast hand motion keeps enough amplitude for slash recognition");

  auto new_player = first_pose;
  new_player.tracking_id = 99;
  new_player.joints[joint_index(Joint::HandRight)].image.x = 0.90F;
  const auto reset = mapper.map(new_player);

  SkeletonMapper fresh_mapper;
  const auto fresh = fresh_mapper.map(new_player);
  expect(near(reset.joints[joint_index(Joint::HandRight)].position_m.x,
              fresh.joints[joint_index(Joint::HandRight)].position_m.x),
         "smoothing history resets when the tracked player changes");
}

void test_mediapipe_adapter() {
  MediaPipePoseFrame source{};
  source.timestamp_us = 55;
  source.tracking_id = 12;
  for (auto& landmark : source.landmarks) {
    landmark.x = 0.50F;
    landmark.y = 0.50F;
    landmark.visibility = 0.90F;
    landmark.presence = 0.80F;
  }

  const auto set = [&source](MediaPipeLandmark index, float x, float y, float z = 0.0F) {
    auto& landmark = source.landmarks[fnk::nui::landmark_index(index)];
    landmark.x = x;
    landmark.y = y;
    landmark.z = z;
  };
  set(MediaPipeLandmark::LeftShoulder, 0.35F, 0.30F);
  set(MediaPipeLandmark::RightShoulder, 0.65F, 0.30F);
  set(MediaPipeLandmark::LeftHip, 0.42F, 0.60F);
  set(MediaPipeLandmark::RightHip, 0.58F, 0.60F);
  set(MediaPipeLandmark::LeftPinky, 0.15F, 0.50F, -0.10F);
  set(MediaPipeLandmark::LeftIndex, 0.13F, 0.49F, -0.12F);
  set(MediaPipeLandmark::LeftThumb, 0.17F, 0.48F, -0.08F);

  const auto pose = MediaPipeAdapter{}.convert(source);
  const auto& shoulder_center = pose.joints[joint_index(Joint::ShoulderCenter)];
  const auto& hip_center = pose.joints[joint_index(Joint::HipCenter)];
  const auto& spine = pose.joints[joint_index(Joint::Spine)];
  const auto& hand = pose.joints[joint_index(Joint::HandLeft)];

  expect(pose.timestamp_us == 55 && pose.tracking_id == 12,
         "MediaPipe frame identity is preserved");
  expect(near(shoulder_center.image.x, 0.50F) && near(shoulder_center.image.y, 0.30F),
         "shoulder center is synthesized from both shoulders");
  expect(near(hip_center.image.x, 0.50F) && near(hip_center.image.y, 0.60F),
         "hip center is synthesized from both hips");
  expect(near(spine.image.y, 0.45F), "spine is synthesized between torso centers");
  expect(near(hand.image.x, 0.15F) && near(hand.relative_depth, -0.10F),
         "hand averages MediaPipe finger landmarks");
  expect(near(hand.confidence, 0.80F), "presence limits joint confidence");

  const auto mirrored = MediaPipeAdapter{MediaPipeAdapterOptions{true}}.convert(source);
  expect(near(mirrored.joints[joint_index(Joint::HandLeft)].image.x, 0.85F),
         "mirrored previews are unmirrored without swapping anatomy");
}

void test_movenet_adapter() {
  MoveNetPoseFrame source{};
  source.timestamp_us = 123;
  source.tracking_id = 4;
  for (auto& point : source.keypoints) {
    point = {0.50F, 0.50F, 0.90F};
  }
  const auto set = [&source](MoveNetKeypoint keypoint, float x, float y,
                             float confidence = 0.90F) {
    source.keypoints[fnk::nui::keypoint_index(keypoint)] = {x, y, confidence};
  };
  set(MoveNetKeypoint::LeftShoulder, 0.35F, 0.30F);
  set(MoveNetKeypoint::RightShoulder, 0.65F, 0.30F);
  set(MoveNetKeypoint::LeftHip, 0.42F, 0.60F);
  set(MoveNetKeypoint::RightHip, 0.58F, 0.60F);
  set(MoveNetKeypoint::LeftWrist, 0.15F, 0.48F, 0.75F);
  set(MoveNetKeypoint::LeftAnkle, 0.40F, 0.92F, 0.70F);

  const auto pose = MoveNetAdapter{}.convert(source);
  expect(pose.timestamp_us == 123 && pose.tracking_id == 4,
         "MoveNet frame identity is preserved");
  expect(near(pose.joints[joint_index(Joint::ShoulderCenter)].image.x, 0.50F),
         "MoveNet shoulder center is synthesized");
  expect(near(pose.joints[joint_index(Joint::HipCenter)].image.y, 0.60F),
         "MoveNet hip center is synthesized");
  expect(near(pose.joints[joint_index(Joint::Spine)].image.y, 0.45F),
         "MoveNet spine is synthesized");
  expect(near(pose.joints[joint_index(Joint::HandLeft)].image.x, 0.045F) &&
             near(pose.joints[joint_index(Joint::HandLeft)].confidence, 0.75F),
         "MoveNet wrist and forearm synthesize a Kinect palm position");
  expect(near(pose.joints[joint_index(Joint::FootLeft)].image.y, 0.92F),
         "MoveNet ankle supplies the Kinect foot fallback");

  source.keypoints[keypoint_index(MoveNetKeypoint::RightShoulder)] =
      {0.60F, 0.30F, 0.90F};
  source.keypoints[keypoint_index(MoveNetKeypoint::RightElbow)] =
      {0.72F, 0.42F, 0.80F};
  source.keypoints[keypoint_index(MoveNetKeypoint::RightWrist)] =
      {0.50F, 0.50F, 0.05F};
  const auto recovered = MoveNetAdapter{}.convert(source);
  const auto& recovered_wrist = recovered.joints[joint_index(Joint::WristRight)];
  const auto& recovered_hand = recovered.joints[joint_index(Joint::HandRight)];
  expect(recovered_wrist.image.x > 0.80F && recovered_wrist.confidence >= 0.35F,
         "lost fast wrist is recovered from the visible arm direction");
  expect(recovered_hand.image.x > recovered_wrist.image.x,
         "recovered palm extends beyond the reconstructed wrist");
}

void test_depth_synthesis() {
  const auto pose = make_pose();
  SkeletonMapper mapper;
  const auto skeleton = mapper.map(pose);
  const auto depth = DepthSynthesizer{}.synthesize(pose, skeleton, 3);

  expect(depth.width == 320 && depth.height == 240, "Kinect depth dimensions are used");
  expect(depth.packed_depth.size() == 320U * 240U, "depth buffer has exact dimensions");

  const std::size_t center = 120U * depth.width + 160U;
  expect(depth.packed_depth[center] != 0, "torso occupies the center of the player mask");
  expect(fnk::nui::unpack_player_index(depth.packed_depth[center]) == 3,
         "packed depth preserves player index");
  expect(fnk::nui::unpack_depth_mm(depth.packed_depth[center]) >= 800,
         "packed depth contains metric millimetres");
  expect(depth.packed_depth.front() == 0, "background remains zero");

  expect(fnk::nui::unpack_depth_mm(fnk::nui::pack_depth_player(9000, 9)) == 8191,
         "packed depth clamps to the 13-bit range");
  expect(fnk::nui::unpack_player_index(fnk::nui::pack_depth_player(1000, 9)) == 6,
         "player index clamps to Kinect's six-player range");
}

void test_game_space_calibration() {
  GameSpaceCalibrationOptions full_body_options;
  full_body_options.required_samples = 5;
  full_body_options.minimum_confidence = 0.35F;
  GameSpaceCalibrator calibrator(full_body_options);
  const auto pose = make_pose();
  for (int sample = 0; sample < 5; ++sample) {
    expect(calibrator.add_sample(pose, 640, 480), "valid calibration pose is accepted");
  }

  expect(calibrator.complete(), "calibration completes after the requested samples");
  const auto& profile = calibrator.profile();
  expect(profile.quality == CalibrationQuality::FullBody,
         "visible ankles produce a full-body calibration");
  expect(near(profile.center_x, 0.50F), "hip center defines game-space x origin");
  expect(near(profile.floor_y, 0.91F), "ankles define the observed floor");

  SkeletonMapper mapper;
  mapper.set_game_space_profile(profile);
  const auto skeleton = mapper.map(pose);
  expect(near(skeleton.position_m.x, 0.0F), "calibrated hip is centered in game space");
  expect(near(skeleton.joints[joint_index(Joint::AnkleLeft)].position_m.y, 0.0F,
              2.0e-2F),
         "calibrated ankle lies on the game floor");
  expect(near(skeleton.position_m.z, 2.0F, 2.0e-2F),
         "neutral pose maps to reference game depth");

  auto torso_only = pose;
  torso_only.joints[joint_index(Joint::AnkleLeft)].confidence = 0.0F;
  torso_only.joints[joint_index(Joint::AnkleRight)].confidence = 0.0F;
  GameSpaceCalibrationOptions torso_options;
  torso_options.required_samples = 3;
  torso_options.minimum_confidence = 0.35F;
  torso_options.reference_depth_m = 1.0F;
  torso_options.game_reference_depth_m = 2.0F;
  GameSpaceCalibrator fallback(torso_options);
  for (int sample = 0; sample < 3; ++sample) {
    fallback.add_sample(torso_only, 640, 480);
  }
  expect(fallback.profile().quality == CalibrationQuality::TorsoOnly,
         "cropped legs produce an explicitly marked torso-only profile");
  expect(fallback.profile().floor_y > 1.0F,
         "torso-only calibration can estimate a floor outside the image");

  SkeletonMapper close_range_mapper;
  close_range_mapper.set_game_space_profile(fallback.profile());
  const auto close_range_skeleton = close_range_mapper.map(torso_only);
  expect(near(close_range_skeleton.position_m.z, 2.0F, 2.0e-2F),
         "close-range webcam depth maps into the Kinect acquisition range");
}

}  // namespace

int main() {
  test_joint_layout();
  test_guest_abi_encoding();
  test_guest_marshalling();
  test_latest_frame_store();
  test_nui_dispatcher();
  test_projection_and_tracking();
  test_confidence_states();
  test_smoothing_and_tracking_reset();
  test_mediapipe_adapter();
  test_movenet_adapter();
  test_depth_synthesis();
  test_game_space_calibration();

  if (failures != 0) {
    std::cerr << failures << " test assertion(s) failed\n";
    return EXIT_FAILURE;
  }

  std::cout << "All NUI compatibility tests passed\n";
  return EXIT_SUCCESS;
}

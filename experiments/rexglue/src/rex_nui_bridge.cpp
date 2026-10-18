#include "rex_nui_bridge.h"

#include "generated/default.uncompressed/fruit_ninja_kinect_recomp_init.h"

#include "fnk/camera/media_foundation_camera.hpp"
#include "fnk/nui/depth_synthesizer.hpp"
#include "fnk/nui/dispatcher.hpp"
#include "fnk/nui/frame_store.hpp"
#include "fnk/nui/game_space_calibration.hpp"
#include "fnk/nui/guest_memory.hpp"
#include "fnk/nui/skeleton_mapper.hpp"
#include "fnk/pose/movenet_onnx.hpp"

#include <rex/logging/macros.h>
#include <rex/system/xmemory.h>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>

namespace {

class RexGuestMemory final : public fnk::nui::GuestMemory {
 public:
  explicit RexGuestMemory(rex::memory::Memory& memory) : memory_(memory) {}

  bool write(fnk::nui::guest::Address address,
             std::span<const std::uint8_t> bytes) override {
    if (address == 0 || bytes.empty()) {
      return bytes.empty();
    }
    std::memcpy(memory_.TranslateVirtual<std::uint8_t*>(address), bytes.data(), bytes.size());
    return true;
  }

  fnk::nui::guest::Address allocate(std::size_t size, std::size_t alignment) override {
    if (size > UINT32_MAX || alignment > UINT32_MAX) {
      return 0;
    }
    return memory_.SystemHeapAlloc(static_cast<std::uint32_t>(size),
                                   static_cast<std::uint32_t>(alignment));
  }

  void release(fnk::nui::guest::Address address) override {
    if (address != 0) {
      memory_.SystemHeapFree(address);
    }
  }

 private:
  rex::memory::Memory& memory_;
};

std::mutex g_mutex;
std::unique_ptr<RexGuestMemory> g_memory;
std::unique_ptr<fnk::nui::NuiFrameStore> g_frames;
std::unique_ptr<fnk::nui::NuiDispatcher> g_dispatcher;
std::jthread g_frame_publisher;

fnk::nui::NuiDispatcher* dispatcher() noexcept {
  std::lock_guard lock(g_mutex);
  return g_dispatcher.get();
}

fnk::nui::SkeletonFrame make_placeholder_skeleton(std::uint64_t timestamp_us) {
  using fnk::nui::Joint;
  using fnk::nui::JointTrackingState;

  fnk::nui::SkeletonFrame frame{};
  frame.timestamp_us = timestamp_us;
  frame.tracking_id = 1;
  frame.state = fnk::nui::SkeletonTrackingState::Tracked;
  frame.position_m = {0.0F, 0.0F, 2.2F};

  const auto set = [&](Joint joint, float x, float y, float z) {
    frame.joints[fnk::nui::joint_index(joint)] = {{x, y, z}, JointTrackingState::Tracked};
  };
  set(Joint::HipCenter, 0.0F, 0.0F, 2.2F);
  set(Joint::Spine, 0.0F, 0.25F, 2.2F);
  set(Joint::ShoulderCenter, 0.0F, 0.55F, 2.2F);
  set(Joint::Head, 0.0F, 0.82F, 2.2F);
  set(Joint::ShoulderLeft, -0.22F, 0.54F, 2.2F);
  set(Joint::ElbowLeft, -0.38F, 0.34F, 2.2F);
  set(Joint::WristLeft, -0.43F, 0.12F, 2.2F);
  set(Joint::HandLeft, -0.45F, 0.02F, 2.2F);
  set(Joint::ShoulderRight, 0.22F, 0.54F, 2.2F);
  set(Joint::ElbowRight, 0.38F, 0.34F, 2.2F);
  set(Joint::WristRight, 0.43F, 0.12F, 2.2F);
  set(Joint::HandRight, 0.45F, 0.02F, 2.2F);
  set(Joint::HipLeft, -0.14F, -0.04F, 2.2F);
  set(Joint::KneeLeft, -0.14F, -0.46F, 2.2F);
  set(Joint::AnkleLeft, -0.14F, -0.84F, 2.2F);
  set(Joint::FootLeft, -0.14F, -0.91F, 2.08F);
  set(Joint::HipRight, 0.14F, -0.04F, 2.2F);
  set(Joint::KneeRight, 0.14F, -0.46F, 2.2F);
  set(Joint::AnkleRight, 0.14F, -0.84F, 2.2F);
  set(Joint::FootRight, 0.14F, -0.91F, 2.08F);
  return frame;
}

void publish(const fnk::nui::SkeletonFrame& skeleton,
             const fnk::nui::DepthPlayerFrame& depth) {
  std::lock_guard lock(g_mutex);
  if (g_frames) {
    g_frames->publish(skeleton, depth);
  }
}

void publish_placeholder_frames(std::stop_token stop_token) {
  const auto started = std::chrono::steady_clock::now();
  while (!stop_token.stop_requested()) {
    const auto elapsed = std::chrono::steady_clock::now() - started;
    const auto timestamp_us =
        std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    auto skeleton = make_placeholder_skeleton(static_cast<std::uint64_t>(timestamp_us));
    fnk::nui::DepthPlayerFrame depth{};
    depth.timestamp_us = skeleton.timestamp_us;
    depth.width = 320;
    depth.height = 240;
    depth.packed_depth.assign(depth.width * depth.height,
                              fnk::nui::pack_depth_player(2200, 1));

    publish(skeleton, depth);
    std::this_thread::sleep_for(std::chrono::milliseconds(33));
  }
}

std::optional<std::uint32_t> environment_u32(const char* name) {
  const char* value = std::getenv(name);
  if (value == nullptr || *value == '\0') {
    return std::nullopt;
  }
  try {
    const unsigned long parsed = std::stoul(value);
    if (parsed <= UINT32_MAX) {
      return static_cast<std::uint32_t>(parsed);
    }
  } catch (...) {
  }
  REXLOG_WARN("FNK NUI: ignoring invalid {}={}", name, value);
  return std::nullopt;
}

std::filesystem::path environment_path(const char* name) {
  const char* value = std::getenv(name);
  return value != nullptr ? std::filesystem::path(value) : std::filesystem::path{};
}

std::filesystem::path find_pose_model() {
  if (auto configured = environment_path("FNK_POSE_MODEL"); !configured.empty()) {
    return configured;
  }
  constexpr std::string_view filename = "movenet-singlepose-lightning.onnx";
  const std::filesystem::path local(filename);
  if (std::filesystem::exists(local)) {
    return local;
  }
  return std::filesystem::path("tools/pose/models") / filename;
}

std::filesystem::path find_calibration_profile() {
  if (auto configured = environment_path("FNK_CALIBRATION_PROFILE");
      !configured.empty()) {
    return configured;
  }
  for (const auto& candidate : {
           std::filesystem::path("droidcam.toml"),
           std::filesystem::path("artifacts/calibration/droidcam.toml"),
           std::filesystem::path("../../artifacts/calibration/droidcam.toml"),
       }) {
    if (std::filesystem::exists(candidate)) {
      return candidate;
    }
  }
  return {};
}

void publish_live_frames(std::stop_token stop_token) {
  const auto model_path = find_pose_model();
  fnk::pose::MoveNetOnnx estimator(model_path);
  if (!estimator.ready()) {
    REXLOG_ERROR("FNK NUI: pose model unavailable at '{}': {}. Using synthetic input.",
                 model_path.string(), estimator.last_error());
    publish_placeholder_frames(stop_token);
    return;
  }

  fnk::camera::CameraOptions camera_options;
  camera_options.device_index = environment_u32("FNK_CAMERA_DEVICE").value_or(0);
  camera_options.requested_width = environment_u32("FNK_CAMERA_WIDTH").value_or(640);
  camera_options.requested_height = environment_u32("FNK_CAMERA_HEIGHT").value_or(480);
  camera_options.requested_fps = environment_u32("FNK_CAMERA_FPS").value_or(30);

  fnk::camera::MediaFoundationCamera camera;
  if (!camera.start(camera_options)) {
    REXLOG_ERROR("FNK NUI: camera {} failed: {}. Using synthetic input.",
                 camera_options.device_index, camera.last_error());
    publish_placeholder_frames(stop_token);
    return;
  }

  fnk::nui::SkeletonMapper mapper;
  const auto profile_path = find_calibration_profile();
  if (!profile_path.empty()) {
    fnk::nui::GameSpaceProfile profile;
    std::string error;
    if (fnk::nui::load_game_space_profile(profile_path, profile, &error)) {
      mapper.set_game_space_profile(profile);
      REXLOG_INFO("FNK NUI: loaded calibration profile '{}'.", profile_path.string());
    } else {
      REXLOG_WARN("FNK NUI: could not load calibration profile '{}': {}",
                  profile_path.string(), error);
    }
  }

  fnk::nui::DepthSynthesizer depth_synthesizer;
  fnk::camera::CameraFrame camera_frame;
  fnk::nui::PoseFrame pose;
  std::uint64_t previous_sequence = 0;
  bool tracking_announced = false;
  REXLOG_INFO("FNK NUI: live camera {} started (requested {}x{} at {} FPS).",
              camera_options.device_index, camera_options.requested_width,
              camera_options.requested_height, camera_options.requested_fps);

  while (!stop_token.stop_requested() && camera.running()) {
    if (!camera.latest_frame(camera_frame) || camera_frame.sequence == previous_sequence) {
      std::this_thread::sleep_for(std::chrono::milliseconds(2));
      continue;
    }
    previous_sequence = camera_frame.sequence;
    if (!estimator.infer(camera_frame, pose)) {
      REXLOG_WARN("FNK NUI: pose inference failed: {}", estimator.last_error());
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
      continue;
    }
    auto skeleton = mapper.map(pose);
    if (!tracking_announced &&
        skeleton.state != fnk::nui::SkeletonTrackingState::NotTracked) {
      tracking_announced = true;
      const auto stats = estimator.stats();
      REXLOG_INFO("FNK NUI: first live skeleton published after {} pose frames "
                  "(inference {:.2f} ms).",
                  stats.frames_processed, stats.last_inference_ms);
    }
    auto depth = depth_synthesizer.synthesize(pose, skeleton);
    publish(skeleton, depth);
  }

  camera.stop();
  if (!stop_token.stop_requested()) {
    REXLOG_ERROR("FNK NUI: camera stream ended: {}. Switching to synthetic input.",
                 camera.last_error());
    publish_placeholder_frames(stop_token);
  }
}

void return_hresult(PPCContext& ctx, fnk::nui::guest::HResult result) noexcept {
  ctx.r3.s64 = result;
}

}  // namespace

namespace fnk::rexbridge {

void initialize(rex::memory::Memory* memory) {
  shutdown();
  if (memory == nullptr) {
    return;
  }
  std::lock_guard lock(g_mutex);
  g_memory = std::make_unique<RexGuestMemory>(*memory);
  g_frames = std::make_unique<fnk::nui::NuiFrameStore>();
  g_dispatcher = std::make_unique<fnk::nui::NuiDispatcher>(*g_memory, *g_frames);
  g_frame_publisher = std::jthread(publish_live_frames);
}

void shutdown() {
  g_frame_publisher = {};
  std::lock_guard lock(g_mutex);
  g_dispatcher.reset();
  g_frames.reset();
  g_memory.reset();
}

}  // namespace fnk::rexbridge

extern "C" REX_FUNC(sub_821C2918) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->initialize(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

extern "C" REX_FUNC(sub_821B3720) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->skeleton_tracking_enable(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

extern "C" REX_FUNC(sub_821B3870) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->skeleton_get_next_frame(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

extern "C" REX_FUNC(sub_821C3CC0) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_open(
                            static_cast<fnk::nui::guest::ImageType>(ctx.r3.u32),
                            static_cast<fnk::nui::guest::ImageResolution>(ctx.r4.u32),
                            ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

extern "C" REX_FUNC(sub_821C3120) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_get_next_frame(ctx.r3.u32, ctx.r4.u32,
                                                          ctx.r5.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

extern "C" REX_FUNC(sub_821C3448) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_release_frame(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

extern "C" REX_FUNC(sub_8220FFD0) {
  if (auto* nui = dispatcher(); nui && nui->owns_texture(ctx.r3.u32)) {
    return_hresult(ctx, nui->texture_lock_rect(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                                               ctx.r6.u32, ctx.r7.u32));
  } else {
    __imp__sub_8220FFD0(ctx, base);
  }
}

extern "C" REX_FUNC(sub_8220E580) {
  if (auto* nui = dispatcher(); nui && nui->owns_texture(ctx.r3.u32)) {
    return_hresult(ctx, nui->texture_unlock_rect(ctx.r3.u32, ctx.r4.u32));
  } else {
    __imp__sub_8220E580(ctx, base);
  }
}

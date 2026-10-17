#include "fnk/nui/guest_marshaller.hpp"

#include <algorithm>
#include <bit>

namespace fnk::nui {
namespace {

void set_vector(guest::Vector4& destination, float x, float y, float z, float w) noexcept {
  destination.x.set(x);
  destination.y.set(y);
  destination.z.set(z);
  destination.w.set(w);
}

void set_vector(guest::Vector4& destination, const std::array<float, 4>& source) noexcept {
  set_vector(destination, source[0], source[1], source[2], source[3]);
}

void set_vector(guest::Vector4& destination, const Vec3& source, float w) noexcept {
  set_vector(destination, source.x, source.y, source.z, w);
}

}  // namespace

guest::SkeletonFrame marshal_skeleton_frame(const SkeletonFrame& source,
                                            const SkeletonMarshalOptions& options) {
  guest::SkeletonFrame destination{};
  destination.timestamp.set(nui_timestamp_ms(source.timestamp_us));
  destination.frame_number.set(options.frame_number);
  set_vector(destination.floor_clip_plane, options.floor_clip_plane);
  set_vector(destination.normal_to_gravity, options.normal_to_gravity);

  const std::size_t slot = std::min(options.skeleton_slot, guest::kSkeletonCount - 1U);
  guest::SkeletonData& skeleton = destination.skeleton_data[slot];
  skeleton.tracking_state.set(static_cast<std::uint32_t>(source.state));
  skeleton.tracking_id.set(source.tracking_id);
  skeleton.user_index.set(options.user_index);
  skeleton.quality_flags.set(options.quality_flags);

  if (source.state != SkeletonTrackingState::NotTracked) {
    set_vector(skeleton.position, source.position_m, 1.0F);
  }

  for (std::size_t index = 0; index < kJointCount; ++index) {
    const SkeletonJoint& joint = source.joints[index];
    skeleton.position_tracking_states[index].set(static_cast<std::uint32_t>(joint.state));
    if (joint.state != JointTrackingState::NotTracked) {
      set_vector(skeleton.positions[index], joint.position_m, 1.0F);
    }
  }

  return destination;
}

guest::ImageFrame marshal_image_frame(const ImageFrameMarshalOptions& options) {
  guest::ImageFrame destination{};
  destination.timestamp.set(nui_timestamp_ms(options.timestamp_us));
  destination.frame_number.set(options.frame_number);
  destination.image_type.set(static_cast<std::uint32_t>(options.image_type));
  destination.resolution.set(static_cast<std::uint32_t>(options.resolution));
  destination.frame_texture.set(options.frame_texture);
  destination.frame_flags.set(options.frame_flags);
  destination.view_area.digital_zoom.set(options.digital_zoom);
  destination.view_area.center_x.set(std::bit_cast<std::uint32_t>(options.center_x));
  destination.view_area.center_y.set(std::bit_cast<std::uint32_t>(options.center_y));
  return destination;
}

std::vector<std::uint8_t> marshal_depth_pixels(const DepthPlayerFrame& source) {
  std::vector<std::uint8_t> result(source.packed_depth.size() * 2U);
  for (std::size_t index = 0; index < source.packed_depth.size(); ++index) {
    const std::uint16_t pixel = source.packed_depth[index];
    result[index * 2U] = static_cast<std::uint8_t>(pixel >> 8U);
    result[index * 2U + 1U] = static_cast<std::uint8_t>(pixel);
  }
  return result;
}

}  // namespace fnk::nui

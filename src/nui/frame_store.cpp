#include "fnk/nui/frame_store.hpp"

namespace fnk::nui {

std::uint64_t NuiFrameStore::publish(const SkeletonFrame& skeleton,
                                     const DepthPlayerFrame& depth,
                                     SkeletonMarshalOptions options) {
  auto packet = std::make_shared<NuiFramePacket>();
  packet->depth_width = depth.width;
  packet->depth_height = depth.height;
  packet->depth_pixels = marshal_depth_pixels(depth);

  std::lock_guard lock(mutex_);
  packet->sequence = next_sequence_++;
  if (options.frame_number == 0) {
    options.frame_number = next_frame_number_;
  }
  next_frame_number_ = options.frame_number + 1U;
  packet->skeleton = marshal_skeleton_frame(skeleton, options);
  latest_ = packet;
  return packet->sequence;
}

std::shared_ptr<const NuiFramePacket> NuiFrameStore::latest() const {
  std::lock_guard lock(mutex_);
  return latest_;
}

std::shared_ptr<const NuiFramePacket> NuiFrameStore::latest_after(
    std::uint64_t sequence) const {
  std::lock_guard lock(mutex_);
  if (!latest_ || latest_->sequence <= sequence) {
    return {};
  }
  return latest_;
}

void NuiFrameStore::clear() {
  std::lock_guard lock(mutex_);
  latest_.reset();
}

}  // namespace fnk::nui

#pragma once

#include "fnk/nui/guest_abi.hpp"
#include "fnk/nui/guest_marshaller.hpp"

#include <cstdint>
#include <memory>
#include <mutex>
#include <vector>

namespace fnk::nui {

struct NuiFramePacket {
  std::uint64_t sequence{};
  guest::SkeletonFrame skeleton{};
  std::vector<std::uint8_t> depth_pixels{};
  std::uint32_t depth_width{};
  std::uint32_t depth_height{};
};

// A single-slot mailbox. Publishing never builds a backlog: a slow game thread
// observes the newest complete pose/depth pair and older packets are discarded.
class NuiFrameStore {
 public:
  std::uint64_t publish(const SkeletonFrame& skeleton, const DepthPlayerFrame& depth,
                        SkeletonMarshalOptions options = {});

  std::shared_ptr<const NuiFramePacket> latest() const;
  std::shared_ptr<const NuiFramePacket> latest_after(std::uint64_t sequence) const;

  void clear();

 private:
  mutable std::mutex mutex_{};
  std::shared_ptr<const NuiFramePacket> latest_{};
  std::uint64_t next_sequence_{1};
  std::uint32_t next_frame_number_{1};
};

}  // namespace fnk::nui

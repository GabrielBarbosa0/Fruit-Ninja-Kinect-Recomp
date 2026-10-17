#pragma once

#include "fnk/nui/types.hpp"

#include <cstdint>
#include <vector>

namespace fnk::nui {

struct DepthSynthesisOptions {
  std::uint32_t width{320};
  std::uint32_t height{240};
  float torso_radius_ratio{0.075F};
  float limb_radius_ratio{0.030F};
  float head_radius_ratio{0.055F};
  float extremity_radius_ratio{0.022F};
};

struct DepthPlayerFrame {
  std::uint64_t timestamp_us{};
  std::uint32_t width{};
  std::uint32_t height{};
  // Kinect v1 packed depth: depth in millimetres occupies the upper 13 bits;
  // the lower 3 bits contain the player index (1-6, zero for background).
  std::vector<std::uint16_t> packed_depth{};
};

class DepthSynthesizer {
 public:
  explicit DepthSynthesizer(DepthSynthesisOptions options = {}) : options_(options) {}

  DepthPlayerFrame synthesize(const PoseFrame& pose, const SkeletonFrame& skeleton,
                              std::uint8_t player_index = 1) const;

 private:
  DepthSynthesisOptions options_{};
};

constexpr std::uint16_t pack_depth_player(std::uint16_t depth_mm,
                                          std::uint8_t player_index) noexcept {
  const auto clamped_depth = depth_mm > 8191 ? 8191 : depth_mm;
  const auto clamped_player = player_index > 6 ? 6 : player_index;
  return static_cast<std::uint16_t>((clamped_depth << 3U) | clamped_player);
}

constexpr std::uint16_t unpack_depth_mm(std::uint16_t packed) noexcept {
  return static_cast<std::uint16_t>(packed >> 3U);
}

constexpr std::uint8_t unpack_player_index(std::uint16_t packed) noexcept {
  return static_cast<std::uint8_t>(packed & 0x7U);
}

}  // namespace fnk::nui

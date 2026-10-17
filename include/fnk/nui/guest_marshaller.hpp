#pragma once

#include "fnk/nui/depth_synthesizer.hpp"
#include "fnk/nui/guest_abi.hpp"
#include "fnk/nui/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace fnk::nui {

struct SkeletonMarshalOptions {
  std::uint32_t frame_number{};
  std::size_t skeleton_slot{};
  std::uint32_t user_index{1};
  std::uint32_t quality_flags{};
  std::array<float, 4> floor_clip_plane{0.0F, 1.0F, 0.0F, 0.0F};
  std::array<float, 4> normal_to_gravity{0.0F, 1.0F, 0.0F, 0.0F};
};

struct ImageFrameMarshalOptions {
  std::uint64_t timestamp_us{};
  std::uint32_t frame_number{};
  guest::ImageType image_type{guest::ImageType::DepthAndPlayerIndex};
  guest::ImageResolution resolution{guest::ImageResolution::R320x240};
  guest::Address frame_texture{};
  std::uint32_t frame_flags{};
  std::uint32_t digital_zoom{};
  std::int32_t center_x{};
  std::int32_t center_y{};
};

// Kinect NUI timestamps are milliseconds since the sensor was initialized.
constexpr std::uint64_t nui_timestamp_ms(std::uint64_t timestamp_us) noexcept {
  return timestamp_us / 1'000U;
}

guest::SkeletonFrame marshal_skeleton_frame(
    const SkeletonFrame& source, const SkeletonMarshalOptions& options = {});

guest::ImageFrame marshal_image_frame(const ImageFrameMarshalOptions& options);

// Produces the exact byte order expected by a Xenon title texture lock.
std::vector<std::uint8_t> marshal_depth_pixels(const DepthPlayerFrame& source);

}  // namespace fnk::nui

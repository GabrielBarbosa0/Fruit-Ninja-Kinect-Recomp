#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>

namespace fnk::nui::guest {

using Address = std::uint32_t;
using Handle = std::uint32_t;
using HResult = std::int32_t;

constexpr HResult make_hresult(std::uint32_t value) noexcept {
  return static_cast<HResult>(value);
}

inline constexpr HResult kSOk = 0;
inline constexpr HResult kSFalse = 1;
inline constexpr HResult kEPointer = make_hresult(0x80004003U);
inline constexpr HResult kEInvalidArgument = make_hresult(0x80070057U);
inline constexpr HResult kENuiFrameNoData = make_hresult(0x83010001U);
inline constexpr HResult kENuiStreamNotEnabled = make_hresult(0x83010002U);
inline constexpr HResult kENuiImageStreamInUse = make_hresult(0x83010003U);
inline constexpr HResult kENuiFeatureNotInitialized = make_hresult(0x83010005U);

template <class T>
struct BigEndian;

template <>
struct BigEndian<std::uint32_t> {
  std::array<std::uint8_t, 4> bytes{};

  constexpr std::uint32_t value() const noexcept {
    return (static_cast<std::uint32_t>(bytes[0]) << 24U) |
           (static_cast<std::uint32_t>(bytes[1]) << 16U) |
           (static_cast<std::uint32_t>(bytes[2]) << 8U) |
           static_cast<std::uint32_t>(bytes[3]);
  }

  constexpr void set(std::uint32_t value) noexcept {
    bytes = {static_cast<std::uint8_t>(value >> 24U),
             static_cast<std::uint8_t>(value >> 16U),
             static_cast<std::uint8_t>(value >> 8U),
             static_cast<std::uint8_t>(value)};
  }
};

template <>
struct BigEndian<std::uint64_t> {
  std::array<std::uint8_t, 8> bytes{};

  constexpr std::uint64_t value() const noexcept {
    std::uint64_t result = 0;
    for (std::uint8_t byte : bytes) {
      result = (result << 8U) | byte;
    }
    return result;
  }

  constexpr void set(std::uint64_t value) noexcept {
    for (std::size_t index = bytes.size(); index-- > 0;) {
      bytes[index] = static_cast<std::uint8_t>(value);
      value >>= 8U;
    }
  }
};

using U32 = BigEndian<std::uint32_t>;
using U64 = BigEndian<std::uint64_t>;

struct F32 {
  U32 bits{};

  constexpr float value() const noexcept {
    return std::bit_cast<float>(bits.value());
  }

  constexpr void set(float value) noexcept {
    bits.set(std::bit_cast<std::uint32_t>(value));
  }
};

enum class ImageType : std::uint32_t {
  DepthAndPlayerIndex = 0,
  Color = 1,
};

enum class ImageResolution : std::uint32_t {
  R80x60 = 0,
  R320x240 = 1,
  R640x480 = 2,
  R1280x960 = 3,
};

inline constexpr std::uint32_t kInitializeUsesDepthAndPlayerIndex = 0x00000001;
inline constexpr std::uint32_t kInitializeUsesColor = 0x00000002;
inline constexpr std::uint32_t kInitializeUsesSkeleton = 0x00000008;
inline constexpr std::uint32_t kInitializeXboxPrivate400 = 0x00000400;
inline constexpr std::uint32_t kInitializeXboxPrivate800 = 0x00000800;
inline constexpr std::uint32_t kTitleInitializeWithoutColor = 0x00000C09;
inline constexpr std::uint32_t kTitleInitializeWithColor = 0x00000C0B;
inline constexpr std::uint32_t kDefaultHardwareThread = 0xFFFFFFFF;
inline constexpr std::uint32_t kSkeletonTrackingTitleSetsTrackedSkeletons = 0x00000002;

inline constexpr std::size_t kSkeletonCount = 6;
inline constexpr std::size_t kSkeletonPositionCount = 20;

struct alignas(16) Vector4 {
  F32 x{};
  F32 y{};
  F32 z{};
  F32 w{};
};

struct alignas(16) SkeletonData {
  U32 tracking_state{};
  U32 tracking_id{};
  U32 enrollment_index{};
  U32 user_index{};
  Vector4 position{};
  std::array<Vector4, kSkeletonPositionCount> positions{};
  std::array<U32, kSkeletonPositionCount> position_tracking_states{};
  U32 quality_flags{};
  std::array<std::uint8_t, 12> reserved{};
};

struct alignas(16) SkeletonFrame {
  U64 timestamp{};
  U32 frame_number{};
  U32 flags{};
  Vector4 floor_clip_plane{};
  Vector4 normal_to_gravity{};
  std::array<SkeletonData, kSkeletonCount> skeleton_data{};
};

struct ImageViewArea {
  U32 digital_zoom{};
  U32 center_x{};
  U32 center_y{};
};

struct alignas(8) ImageFrame {
  U64 timestamp{};
  U32 frame_number{};
  U32 image_type{};
  U32 resolution{};
  U32 frame_texture{};
  U32 frame_flags{};
  ImageViewArea view_area{};
};

// D3D-style rectangle returned by INuiFrameTexture::LockRect in this title.
struct LockedRect {
  U32 pitch{};
  U32 bits{};
};

using NuiInitialize = HResult (*)(std::uint32_t flags, std::uint32_t hardware_thread);
using NuiSkeletonTrackingEnable = HResult (*)(Handle next_frame_event, std::uint32_t flags);
using NuiSkeletonGetNextFrame = HResult (*)(std::uint32_t wait_ms, Address frame);
using NuiImageStreamOpen = HResult (*)(ImageType type, ImageResolution resolution,
                                      std::uint32_t flags, std::uint32_t frame_limit,
                                      Handle next_frame_event, Address stream_handle_out);
using NuiImageStreamGetNextFrame = HResult (*)(Handle stream, std::uint32_t wait_ms,
                                               Address image_frame_pointer_out);
using NuiImageStreamReleaseFrame = HResult (*)(Handle stream, Address image_frame);

static_assert(sizeof(U32) == 4);
static_assert(sizeof(U64) == 8);
static_assert(sizeof(F32) == 4);
static_assert(sizeof(Vector4) == 0x10);
static_assert(sizeof(SkeletonData) == 0x1C0);
static_assert(offsetof(SkeletonData, tracking_id) == 0x004);
static_assert(offsetof(SkeletonData, position) == 0x010);
static_assert(offsetof(SkeletonData, positions) == 0x020);
static_assert(offsetof(SkeletonData, position_tracking_states) == 0x160);
static_assert(offsetof(SkeletonData, quality_flags) == 0x1B0);
static_assert(sizeof(SkeletonFrame) == 0xAB0);
static_assert(offsetof(SkeletonFrame, skeleton_data) == 0x030);
static_assert(sizeof(ImageViewArea) == 0x0C);
static_assert(sizeof(ImageFrame) == 0x28);
static_assert(offsetof(ImageFrame, frame_texture) == 0x14);
static_assert(offsetof(ImageFrame, view_area) == 0x1C);
static_assert(sizeof(LockedRect) == 8);

}  // namespace fnk::nui::guest

#pragma once

#include "fnk/nui/frame_store.hpp"
#include "fnk/nui/guest_memory.hpp"

#include <array>
#include <cstdint>
#include <mutex>

namespace fnk::nui {

class NuiDispatcher {
 public:
  NuiDispatcher(GuestMemory& memory, NuiFrameStore& frames)
      : memory_(memory), frames_(frames) {}
  ~NuiDispatcher();

  guest::HResult initialize(std::uint32_t flags, std::uint32_t hardware_thread);
  guest::HResult skeleton_tracking_enable(guest::Handle event, std::uint32_t flags);
  guest::HResult skeleton_get_next_frame(std::uint32_t wait_ms,
                                         guest::Address frame_out);
  guest::HResult image_stream_open(guest::ImageType type,
                                   guest::ImageResolution resolution,
                                   std::uint32_t flags, std::uint32_t frame_limit,
                                   guest::Handle event,
                                   guest::Address stream_handle_out);
  guest::HResult image_stream_get_next_frame(guest::Handle stream,
                                              std::uint32_t wait_ms,
                                              guest::Address image_frame_pointer_out);
  guest::HResult image_stream_release_frame(guest::Handle stream,
                                             guest::Address image_frame);

  guest::HResult texture_lock_rect(guest::Address texture, std::uint32_t level,
                                   guest::Address locked_rect_out,
                                   guest::Address rectangle,
                                   std::uint32_t flags);
  guest::HResult texture_unlock_rect(guest::Address texture, std::uint32_t level);

  void shutdown();

 private:
  struct Stream {
    guest::Handle handle{};
    guest::ImageType type{guest::ImageType::DepthAndPlayerIndex};
    guest::ImageResolution resolution{guest::ImageResolution::R320x240};
    guest::Handle event{};
    guest::Address image_frame{};
    guest::Address texture{};
    guest::Address pixels{};
    std::uint32_t width{};
    std::uint32_t height{};
    std::uint32_t bytes_per_pixel{};
    std::size_t pixel_capacity{};
    std::uint64_t last_sequence{};
    bool frame_outstanding{};
  };

  Stream* find_stream(guest::Handle handle) noexcept;
  Stream* find_texture(guest::Address texture) noexcept;
  void release_stream(Stream& stream);

  GuestMemory& memory_;
  NuiFrameStore& frames_;
  std::mutex mutex_{};
  std::array<Stream, 2> streams_{};
  std::uint32_t initialize_flags_{};
  std::uint32_t skeleton_flags_{};
  std::uint64_t last_skeleton_sequence_{};
  guest::Handle next_handle_{0xF0010001U};
  bool initialized_{};
  bool skeleton_enabled_{};
};

}  // namespace fnk::nui

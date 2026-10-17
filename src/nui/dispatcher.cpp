#include "fnk/nui/dispatcher.hpp"

#include <algorithm>

namespace fnk::nui {
namespace {

constexpr bool supports(guest::ImageType type, guest::ImageResolution resolution) noexcept {
  return (type == guest::ImageType::DepthAndPlayerIndex &&
          resolution == guest::ImageResolution::R320x240) ||
         (type == guest::ImageType::Color &&
          resolution == guest::ImageResolution::R640x480);
}

}  // namespace

NuiDispatcher::~NuiDispatcher() {
  shutdown();
}

guest::HResult NuiDispatcher::initialize(std::uint32_t flags,
                                         std::uint32_t hardware_thread) {
  if (hardware_thread != guest::kDefaultHardwareThread && hardware_thread >= 6U) {
    return guest::kEInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  for (Stream& stream : streams_) {
    release_stream(stream);
  }
  initialize_flags_ = flags;
  skeleton_flags_ = 0;
  last_skeleton_sequence_ = 0;
  skeleton_enabled_ = false;
  initialized_ = true;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::skeleton_tracking_enable(guest::Handle,
                                                        std::uint32_t flags) {
  std::lock_guard lock(mutex_);
  if (!initialized_ ||
      (initialize_flags_ & guest::kInitializeUsesSkeleton) == 0U) {
    return guest::kENuiFeatureNotInitialized;
  }
  if ((flags & ~0x0FU) != 0U) {
    return guest::kEInvalidArgument;
  }
  skeleton_flags_ = flags;
  last_skeleton_sequence_ = 0;
  skeleton_enabled_ = true;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::skeleton_get_next_frame(std::uint32_t,
                                                       guest::Address frame_out) {
  if (frame_out == 0) {
    return guest::kEPointer;
  }

  std::lock_guard lock(mutex_);
  if (!skeleton_enabled_) {
    return guest::kENuiStreamNotEnabled;
  }
  const auto packet = frames_.latest_after(last_skeleton_sequence_);
  if (!packet) {
    return (skeleton_flags_ & guest::kSkeletonTrackingTitleSetsTrackedSkeletons) != 0U
               ? guest::kSFalse
               : guest::kENuiFrameNoData;
  }
  if (!memory_.write_object(frame_out, packet->skeleton)) {
    return guest::kEPointer;
  }
  last_skeleton_sequence_ = packet->sequence;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::image_stream_open(
    guest::ImageType type, guest::ImageResolution resolution, std::uint32_t flags,
    std::uint32_t frame_limit, guest::Handle event,
    guest::Address stream_handle_out) {
  if (stream_handle_out == 0) {
    return guest::kEPointer;
  }
  if (flags != 0 || frame_limit == 0 || frame_limit > 48 || !supports(type, resolution)) {
    return guest::kEInvalidArgument;
  }

  std::lock_guard lock(mutex_);
  if (!initialized_) {
    return guest::kENuiFeatureNotInitialized;
  }
  const std::uint32_t required_flag =
      type == guest::ImageType::Color ? guest::kInitializeUsesColor
                                      : guest::kInitializeUsesDepthAndPlayerIndex;
  if ((initialize_flags_ & required_flag) == 0U) {
    return guest::kENuiStreamNotEnabled;
  }
  if (std::any_of(streams_.begin(), streams_.end(),
                  [type](const Stream& stream) {
                    return stream.handle != 0 && stream.type == type;
                  })) {
    return guest::kENuiImageStreamInUse;
  }

  auto free_stream = std::find_if(streams_.begin(), streams_.end(),
                                  [](const Stream& stream) { return stream.handle == 0; });
  if (free_stream == streams_.end()) {
    return guest::kENuiImageStreamInUse;
  }

  Stream candidate{};
  candidate.handle = next_handle_++;
  candidate.type = type;
  candidate.resolution = resolution;
  candidate.event = event;
  candidate.width = type == guest::ImageType::Color ? 640U : 320U;
  candidate.height = type == guest::ImageType::Color ? 480U : 240U;
  candidate.bytes_per_pixel = type == guest::ImageType::Color ? 4U : 2U;
  candidate.pixel_capacity = static_cast<std::size_t>(candidate.width) *
                             candidate.height * candidate.bytes_per_pixel;
  candidate.image_frame = memory_.allocate(sizeof(guest::ImageFrame), alignof(guest::ImageFrame));
  candidate.texture = memory_.allocate(4, 4);
  candidate.pixels = memory_.allocate(candidate.pixel_capacity, 16);
  if (candidate.image_frame == 0 || candidate.texture == 0 || candidate.pixels == 0) {
    release_stream(candidate);
    return guest::kENuiFeatureNotInitialized;
  }
  if (!memory_.write_u32(stream_handle_out, candidate.handle)) {
    release_stream(candidate);
    return guest::kEPointer;
  }
  *free_stream = candidate;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::image_stream_get_next_frame(
    guest::Handle handle, std::uint32_t, guest::Address image_frame_pointer_out) {
  if (image_frame_pointer_out == 0) {
    return guest::kEPointer;
  }
  if (!memory_.write_u32(image_frame_pointer_out, 0)) {
    return guest::kEPointer;
  }

  std::lock_guard lock(mutex_);
  Stream* stream = find_stream(handle);
  if (stream == nullptr) {
    return guest::kEInvalidArgument;
  }
  if (stream->frame_outstanding) {
    return guest::kENuiImageStreamInUse;
  }

  const auto packet = frames_.latest_after(stream->last_sequence);
  if (!packet) {
    return guest::kENuiFrameNoData;
  }
  if (stream->type == guest::ImageType::Color) {
    // Color publication is intentionally deferred until the byte layout of the
    // title's X8R8G8B8 surface has been verified against the original hardware.
    return guest::kENuiFrameNoData;
  }
  if (packet->depth_width != stream->width || packet->depth_height != stream->height ||
      packet->depth_pixels.size() != stream->pixel_capacity) {
    return guest::kEInvalidArgument;
  }
  if (!memory_.write(stream->pixels, packet->depth_pixels)) {
    return guest::kEPointer;
  }

  ImageFrameMarshalOptions options{};
  options.timestamp_us = packet->skeleton.timestamp.value() * 1'000U;
  options.frame_number = packet->skeleton.frame_number.value();
  options.image_type = stream->type;
  options.resolution = stream->resolution;
  options.frame_texture = stream->texture;
  const guest::ImageFrame frame = marshal_image_frame(options);
  if (!memory_.write_object(stream->image_frame, frame) ||
      !memory_.write_u32(image_frame_pointer_out, stream->image_frame)) {
    return guest::kEPointer;
  }
  stream->last_sequence = packet->sequence;
  stream->frame_outstanding = true;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::image_stream_release_frame(guest::Handle handle,
                                                          guest::Address image_frame) {
  std::lock_guard lock(mutex_);
  Stream* stream = find_stream(handle);
  if (stream == nullptr || image_frame != stream->image_frame ||
      !stream->frame_outstanding) {
    return guest::kEInvalidArgument;
  }
  stream->frame_outstanding = false;
  return guest::kSOk;
}

guest::HResult NuiDispatcher::texture_lock_rect(guest::Address texture,
                                                 std::uint32_t level,
                                                 guest::Address locked_rect_out,
                                                 guest::Address rectangle,
                                                 std::uint32_t) {
  if (locked_rect_out == 0) {
    return guest::kEPointer;
  }
  std::lock_guard lock(mutex_);
  Stream* stream = find_texture(texture);
  if (stream == nullptr || level != 0 || rectangle != 0) {
    return guest::kEInvalidArgument;
  }
  guest::LockedRect rectangle_out{};
  rectangle_out.pitch.set(stream->width * stream->bytes_per_pixel);
  rectangle_out.bits.set(stream->pixels);
  return memory_.write_object(locked_rect_out, rectangle_out) ? guest::kSOk
                                                               : guest::kEPointer;
}

guest::HResult NuiDispatcher::texture_unlock_rect(guest::Address texture,
                                                   std::uint32_t level) {
  std::lock_guard lock(mutex_);
  return find_texture(texture) != nullptr && level == 0 ? guest::kSOk
                                                        : guest::kEInvalidArgument;
}

void NuiDispatcher::shutdown() {
  std::lock_guard lock(mutex_);
  for (Stream& stream : streams_) {
    release_stream(stream);
  }
  initialize_flags_ = 0;
  skeleton_flags_ = 0;
  last_skeleton_sequence_ = 0;
  initialized_ = false;
  skeleton_enabled_ = false;
}

NuiDispatcher::Stream* NuiDispatcher::find_stream(guest::Handle handle) noexcept {
  const auto found = std::find_if(streams_.begin(), streams_.end(),
                                  [handle](const Stream& stream) {
                                    return stream.handle == handle;
                                  });
  return found == streams_.end() ? nullptr : &*found;
}

NuiDispatcher::Stream* NuiDispatcher::find_texture(guest::Address texture) noexcept {
  const auto found = std::find_if(streams_.begin(), streams_.end(),
                                  [texture](const Stream& stream) {
                                    return stream.handle != 0 && stream.texture == texture;
                                  });
  return found == streams_.end() ? nullptr : &*found;
}

void NuiDispatcher::release_stream(Stream& stream) {
  if (stream.image_frame != 0) {
    memory_.release(stream.image_frame);
  }
  if (stream.texture != 0) {
    memory_.release(stream.texture);
  }
  if (stream.pixels != 0) {
    memory_.release(stream.pixels);
  }
  stream = {};
}

}  // namespace fnk::nui

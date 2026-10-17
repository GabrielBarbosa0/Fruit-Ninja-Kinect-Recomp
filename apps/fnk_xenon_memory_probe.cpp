#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <span>

#include "fnk/nui/dispatcher.hpp"
#include "fnk/nui/xenon_address_space_memory.hpp"
#include "fnk/nui/xenon_registry_overlay.hpp"
#include "xenon/memory/address_space.hpp"

namespace {

xenon::cpu::ExecutionResult fallback_function(
    xenon::cpu::ExecutionContext& context) {
  return {xenon::cpu::FlowReason::Return,
          static_cast<xenon::cpu::GuestAddress>(context.state.lr), 0};
}

xenon::cpu::NativeCompiledEntry fallback_lookup(
    void*, xenon::cpu::ExecutionContext&, xenon::cpu::GuestAddress,
    xenon::cpu::CompiledLookupKind) {
  return &fallback_function;
}

bool check(bool condition, const char* message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
  }
  return condition;
}

}  // namespace

int main() {
  xenon::memory::AddressSpace address_space{
      xenon::memory::GuestTranslationMode::Compact};
  if (!check(address_space.initialize(), "initialize Xenon AddressSpace")) {
    return 1;
  }

  fnk::nui::XenonAddressSpaceGuestMemory memory{address_space};
  fnk::nui::NuiFrameStore frames;
  fnk::nui::NuiDispatcher dispatcher{memory, frames};
  xenon::cpu::CpuState cpu{};
  xenon::cpu::NullRuntimeServices runtime;
  xenon::cpu::ExecutionContext context{cpu, address_space, runtime};
  context.compiled_lookup = &fallback_lookup;
  fnk::nui::XenonNuiRegistryOverlay overlay;
  fnk::nui::install_xenon_nui_registry_overlay(context, overlay, dispatcher);

  if (!check(context.lookup_compiled(0x82000000U) == &fallback_function,
             "delegate unrelated compiled lookup") ||
      !check(context.lookup_compiled(fnk::nui::xenon_address::kNuiInitialize) !=
                 &fallback_function,
             "override recovered NUI address")) {
    return 1;
  }
  cpu.gpr[3] = 0x12345678U;
  const auto texture_fallback = context.lookup_compiled(
      fnk::nui::xenon_address::kTextureLockRect)(context);
  if (!check(texture_fallback.reason == xenon::cpu::FlowReason::Return,
             "delegate LockRect for a non-NUI texture")) {
    return 1;
  }

  const auto result_address = memory.allocate(0x1000U, 16U);
  if (!check(result_address != 0, "allocate guest result page")) {
    return 1;
  }

  using namespace fnk::nui;
  using namespace fnk::nui::guest;
  cpu.gpr[3] = kTitleInitializeWithoutColor;
  cpu.gpr[4] = kDefaultHardwareThread;
  context.lookup_compiled(xenon_address::kNuiInitialize)(context);
  if (!check(static_cast<std::uint32_t>(cpu.gpr[3]) ==
                 static_cast<std::uint32_t>(kSOk),
             "initialize through CPU V2 registry overlay")) {
    return 1;
  }
  cpu.gpr[3] = 0;
  cpu.gpr[4] = 0;
  context.lookup_compiled(xenon_address::kNuiSkeletonTrackingEnable)(context);
  if (!check(static_cast<std::uint32_t>(cpu.gpr[3]) ==
                 static_cast<std::uint32_t>(kSOk),
             "enable skeleton through CPU V2 registry overlay")) {
    return 1;
  }

  fnk::nui::SkeletonFrame skeleton{};
  skeleton.timestamp_us = 1'000'000U;
  skeleton.tracking_id = 7U;
  skeleton.state = SkeletonTrackingState::Tracked;

  DepthPlayerFrame depth{};
  depth.timestamp_us = skeleton.timestamp_us;
  depth.width = 320;
  depth.height = 240;
  depth.packed_depth.assign(depth.width * depth.height,
                            pack_depth_player(2200U, 1U));
  frames.publish(skeleton, depth);

  if (!check(dispatcher.skeleton_get_next_frame(0, result_address) == kSOk,
             "marshal skeleton into AddressSpace")) {
    return 1;
  }
  const auto frame_number = address_space.read32_be(result_address + 8U);
  if (!check(frame_number == 1U,
             "read marshalled skeleton through AddressSpace")) {
    return 1;
  }

  const auto stream_out = result_address + sizeof(guest::SkeletonFrame);
  if (!check(dispatcher.image_stream_open(ImageType::DepthAndPlayerIndex,
                                          ImageResolution::R320x240, 0, 2, 0,
                                          stream_out) == kSOk,
             "open synthetic depth stream")) {
    return 1;
  }
  const auto stream = address_space.read32_be(stream_out);

  // The skeleton consumer has seen the current packet, but the independent
  // image stream has not. It must receive the same complete pose/depth pair.
  const auto image_out = stream_out + 4U;
  if (!check(
          dispatcher.image_stream_get_next_frame(stream, 0, image_out) == kSOk,
          "publish depth frame into AddressSpace")) {
    return 1;
  }
  const auto image_frame = address_space.read32_be(image_out);
  const auto texture = address_space.read32_be(image_frame + 0x14U);
  const auto locked_rect = image_out + 8U;
  if (!check(
          dispatcher.texture_lock_rect(texture, 0, locked_rect, 0, 0) == kSOk,
          "lock synthetic depth texture")) {
    return 1;
  }

  const auto pitch = address_space.read32_be(locked_rect);
  const auto pixels = address_space.read32_be(locked_rect + 4U);
  std::array<std::byte, 2> first_pixel{};
  address_space.read_bytes(pixels, first_pixel);
  const auto packed = static_cast<std::uint16_t>(
      (std::to_integer<std::uint16_t>(first_pixel[0]) << 8U) |
      std::to_integer<std::uint16_t>(first_pixel[1]));

  const bool valid =
      check(pitch == 640U, "depth texture pitch is 640 bytes") &&
      check(unpack_depth_mm(packed) == 2200U,
            "depth bytes retain the synthesized millimetres") &&
      check(unpack_player_index(packed) == 1U,
            "depth bytes retain player index") &&
      check(dispatcher.image_stream_release_frame(stream, image_frame) == kSOk,
            "release synthetic depth frame");

  dispatcher.shutdown();
  memory.release(result_address);
  if (!valid) {
    return 1;
  }

  std::cout << "Xenon AddressSpace NUI bridge OK: stream=0x" << std::hex
            << stream << " image=0x" << image_frame << " texture=0x" << texture
            << " pixels=0x" << pixels << std::dec << " pitch=" << pitch << '\n';
  return 0;
}

#include "fnk/nui/xenon_registry_overlay.hpp"

#include "fnk/nui/dispatcher.hpp"

namespace fnk::nui {
namespace {

using xenon::cpu::CompiledLookupKind;
using xenon::cpu::ExecutionContext;
using xenon::cpu::ExecutionResult;
using xenon::cpu::FlowReason;
using xenon::cpu::GuestAddress;
using xenon::cpu::NativeCompiledEntry;

XenonNuiRegistryOverlay* overlay(ExecutionContext& context) noexcept {
  return static_cast<XenonNuiRegistryOverlay*>(context.compiled_registry);
}

ExecutionResult guest_return(ExecutionContext& context) noexcept {
  return {FlowReason::Return,
          static_cast<GuestAddress>(context.state.lr & ~std::uint64_t{3}), 0};
}

ExecutionResult return_hresult(ExecutionContext& context,
                               guest::HResult result) noexcept {
  context.state.gpr[3] =
      static_cast<std::uint64_t>(static_cast<std::int64_t>(result));
  return guest_return(context);
}

ExecutionResult missing_fallback(ExecutionContext& context,
                                 GuestAddress address) noexcept {
  return {FlowReason::Trap, context.state.cia, address};
}

NativeCompiledEntry fallback_entry(ExecutionContext& context,
                                   GuestAddress address) noexcept {
  auto* state = overlay(context);
  return state != nullptr && state->fallback_lookup != nullptr
             ? state->fallback_lookup(state->fallback_registry, context,
                                      address, CompiledLookupKind::Call)
             : nullptr;
}

ExecutionResult nui_initialize(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context, state != nullptr && state->dispatcher != nullptr
                   ? state->dispatcher->initialize(
                         static_cast<std::uint32_t>(context.state.gpr[3]),
                         static_cast<std::uint32_t>(context.state.gpr[4]))
                   : guest::kENuiFeatureNotInitialized);
}

ExecutionResult skeleton_tracking_enable(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context, state != nullptr && state->dispatcher != nullptr
                   ? state->dispatcher->skeleton_tracking_enable(
                         static_cast<guest::Handle>(context.state.gpr[3]),
                         static_cast<std::uint32_t>(context.state.gpr[4]))
                   : guest::kENuiFeatureNotInitialized);
}

ExecutionResult skeleton_get_next_frame(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context, state != nullptr && state->dispatcher != nullptr
                   ? state->dispatcher->skeleton_get_next_frame(
                         static_cast<std::uint32_t>(context.state.gpr[3]),
                         static_cast<guest::Address>(context.state.gpr[4]))
                   : guest::kENuiStreamNotEnabled);
}

ExecutionResult image_stream_open(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context,
      state != nullptr && state->dispatcher != nullptr
          ? state->dispatcher->image_stream_open(
                static_cast<guest::ImageType>(context.state.gpr[3]),
                static_cast<guest::ImageResolution>(context.state.gpr[4]),
                static_cast<std::uint32_t>(context.state.gpr[5]),
                static_cast<std::uint32_t>(context.state.gpr[6]),
                static_cast<guest::Handle>(context.state.gpr[7]),
                static_cast<guest::Address>(context.state.gpr[8]))
          : guest::kENuiFeatureNotInitialized);
}

ExecutionResult image_stream_get_next_frame(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context, state != nullptr && state->dispatcher != nullptr
                   ? state->dispatcher->image_stream_get_next_frame(
                         static_cast<guest::Handle>(context.state.gpr[3]),
                         static_cast<std::uint32_t>(context.state.gpr[4]),
                         static_cast<guest::Address>(context.state.gpr[5]))
                   : guest::kENuiStreamNotEnabled);
}

ExecutionResult image_stream_release_frame(ExecutionContext& context) {
  auto* state = overlay(context);
  return return_hresult(
      context, state != nullptr && state->dispatcher != nullptr
                   ? state->dispatcher->image_stream_release_frame(
                         static_cast<guest::Handle>(context.state.gpr[3]),
                         static_cast<guest::Address>(context.state.gpr[4]))
                   : guest::kENuiStreamNotEnabled);
}

ExecutionResult texture_lock_rect(ExecutionContext& context) {
  auto* state = overlay(context);
  const auto texture = static_cast<guest::Address>(context.state.gpr[3]);
  if (state != nullptr && state->dispatcher != nullptr &&
      state->dispatcher->owns_texture(texture)) {
    return return_hresult(
        context, state->dispatcher->texture_lock_rect(
                     texture, static_cast<std::uint32_t>(context.state.gpr[4]),
                     static_cast<guest::Address>(context.state.gpr[5]),
                     static_cast<guest::Address>(context.state.gpr[6]),
                     static_cast<std::uint32_t>(context.state.gpr[7])));
  }
  auto* fallback = fallback_entry(context, xenon_address::kTextureLockRect);
  return fallback != nullptr
             ? fallback(context)
             : missing_fallback(context, xenon_address::kTextureLockRect);
}

ExecutionResult texture_unlock_rect(ExecutionContext& context) {
  auto* state = overlay(context);
  const auto texture = static_cast<guest::Address>(context.state.gpr[3]);
  if (state != nullptr && state->dispatcher != nullptr &&
      state->dispatcher->owns_texture(texture)) {
    return return_hresult(
        context,
        state->dispatcher->texture_unlock_rect(
            texture, static_cast<std::uint32_t>(context.state.gpr[4])));
  }
  auto* fallback = fallback_entry(context, xenon_address::kTextureUnlockRect);
  return fallback != nullptr
             ? fallback(context)
             : missing_fallback(context, xenon_address::kTextureUnlockRect);
}

NativeCompiledEntry lookup(void* registry, ExecutionContext& context,
                           GuestAddress target, CompiledLookupKind kind) {
  auto* state = static_cast<XenonNuiRegistryOverlay*>(registry);
  switch (target) {
    case xenon_address::kNuiInitialize:
      return &nui_initialize;
    case xenon_address::kNuiSkeletonTrackingEnable:
      return &skeleton_tracking_enable;
    case xenon_address::kNuiSkeletonGetNextFrame:
      return &skeleton_get_next_frame;
    case xenon_address::kNuiImageStreamOpen:
      return &image_stream_open;
    case xenon_address::kNuiImageStreamGetNextFrame:
      return &image_stream_get_next_frame;
    case xenon_address::kNuiImageStreamReleaseFrame:
      return &image_stream_release_frame;
    case xenon_address::kTextureLockRect:
      return &texture_lock_rect;
    case xenon_address::kTextureUnlockRect:
      return &texture_unlock_rect;
    default:
      return state != nullptr && state->fallback_lookup != nullptr
                 ? state->fallback_lookup(state->fallback_registry, context,
                                          target, kind)
                 : nullptr;
  }
}

}  // namespace

void install_xenon_nui_registry_overlay(xenon::cpu::ExecutionContext& context,
                                        XenonNuiRegistryOverlay& state,
                                        NuiDispatcher& dispatcher) noexcept {
  if (context.compiled_registry == &state &&
      context.compiled_lookup == &lookup) {
    state.dispatcher = &dispatcher;
    return;
  }
  state.fallback_registry = context.compiled_registry;
  state.fallback_lookup = context.compiled_lookup;
  state.dispatcher = &dispatcher;
  context.compiled_registry = &state;
  context.compiled_lookup = &lookup;
}

}  // namespace fnk::nui

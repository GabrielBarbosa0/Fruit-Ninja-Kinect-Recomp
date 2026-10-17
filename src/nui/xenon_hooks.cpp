#include "fnk/nui/xenon_hooks.hpp"

#include "fnk/nui/dispatcher.hpp"

#include <ppc_context.h>

#include <atomic>

namespace {

std::atomic<fnk::nui::NuiDispatcher*> g_dispatcher{};

fnk::nui::NuiDispatcher* dispatcher() noexcept {
  return g_dispatcher.load(std::memory_order_acquire);
}

void return_hresult(PPCContext& context, fnk::nui::guest::HResult result) noexcept {
  context.r3.s64 = result;
}

}  // namespace

namespace fnk::nui {

void bind_xenon_nui_dispatcher(NuiDispatcher* value) noexcept {
  g_dispatcher.store(value, std::memory_order_release);
}

}  // namespace fnk::nui

// Original implementations are retained for non-synthetic textures.
PPC_FUNC_IMPL(__imp__sub_8220FFD0);
PPC_FUNC_IMPL(__imp__sub_8220E580);

PPC_FUNC(sub_821C2918) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->initialize(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

PPC_FUNC(sub_821B3720) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->skeleton_tracking_enable(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

PPC_FUNC(sub_821B3870) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->skeleton_get_next_frame(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

PPC_FUNC(sub_821C3CC0) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_open(
                            static_cast<fnk::nui::guest::ImageType>(ctx.r3.u32),
                            static_cast<fnk::nui::guest::ImageResolution>(ctx.r4.u32),
                            ctx.r5.u32, ctx.r6.u32, ctx.r7.u32, ctx.r8.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiFeatureNotInitialized);
  }
}

PPC_FUNC(sub_821C3120) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_get_next_frame(
                            ctx.r3.u32, ctx.r4.u32, ctx.r5.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

PPC_FUNC(sub_821C3448) {
  if (auto* nui = dispatcher()) {
    return_hresult(ctx, nui->image_stream_release_frame(ctx.r3.u32, ctx.r4.u32));
  } else {
    return_hresult(ctx, fnk::nui::guest::kENuiStreamNotEnabled);
  }
}

PPC_FUNC(sub_8220FFD0) {
  if (auto* nui = dispatcher(); nui != nullptr && nui->owns_texture(ctx.r3.u32)) {
    return_hresult(ctx, nui->texture_lock_rect(ctx.r3.u32, ctx.r4.u32, ctx.r5.u32,
                                               ctx.r6.u32, ctx.r7.u32));
  } else {
    __imp__sub_8220FFD0(ctx, base);
  }
}

PPC_FUNC(sub_8220E580) {
  if (auto* nui = dispatcher(); nui != nullptr && nui->owns_texture(ctx.r3.u32)) {
    return_hresult(ctx, nui->texture_unlock_rect(ctx.r3.u32, ctx.r4.u32));
  } else {
    __imp__sub_8220E580(ctx, base);
  }
}

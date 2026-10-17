#pragma once

#include <cstdint>

#include "xenon/cpu/runtime.hpp"

namespace fnk::nui {

class NuiDispatcher;

namespace xenon_address {
inline constexpr std::uint32_t kNuiInitialize = 0x821C2918U;
inline constexpr std::uint32_t kNuiSkeletonTrackingEnable = 0x821B3720U;
inline constexpr std::uint32_t kNuiSkeletonGetNextFrame = 0x821B3870U;
inline constexpr std::uint32_t kNuiImageStreamOpen = 0x821C3CC0U;
inline constexpr std::uint32_t kNuiImageStreamGetNextFrame = 0x821C3120U;
inline constexpr std::uint32_t kNuiImageStreamReleaseFrame = 0x821C3448U;
inline constexpr std::uint32_t kTextureLockRect = 0x8220FFD0U;
inline constexpr std::uint32_t kTextureUnlockRect = 0x8220E580U;
}  // namespace xenon_address

// Per-ExecutionContext lookup wrapper for the modern Xenon-Recomp CPU V2 ABI.
// The caller owns this object and must keep it alive as long as the context.
struct XenonNuiRegistryOverlay {
  void* fallback_registry{};
  xenon::cpu::CompiledLookupCallback fallback_lookup{};
  NuiDispatcher* dispatcher{};
};

// Call after the generated module's bind_compiled_registry(). Known Fruit
// Ninja NUI addresses are redirected to native handlers; every other lookup
// is delegated to the generated registry unchanged.
void install_xenon_nui_registry_overlay(xenon::cpu::ExecutionContext& context,
                                        XenonNuiRegistryOverlay& overlay,
                                        NuiDispatcher& dispatcher) noexcept;

}  // namespace fnk::nui

#include "fnk/nui/xenon_module_bridge.hpp"

#include "fnk/nui/dispatcher.hpp"
#include "fnk/nui/xenon_address_space_memory.hpp"
#include "fnk/nui/xenon_registry_overlay.hpp"

#include "xenon/memory/address_space.hpp"

#include <memory>
#include <mutex>

namespace fnk::nui {
namespace {

struct ModuleState {
  xenon::memory::AddressSpace* address_space{};
  std::unique_ptr<XenonAddressSpaceGuestMemory> memory{};
  std::unique_ptr<NuiFrameStore> frames{};
  std::unique_ptr<NuiDispatcher> dispatcher{};
};

std::mutex g_state_mutex;
ModuleState g_state;
thread_local XenonNuiRegistryOverlay g_thread_overlay;

bool ensure_state(xenon::memory::AddressSpace& address_space) {
  if (g_state.address_space == &address_space && g_state.dispatcher != nullptr) {
    return true;
  }

  if (g_state.address_space != nullptr && g_state.address_space != &address_space) {
    // A module DLL is bound to one active Xenon session at a time. Refuse to
    // invalidate dispatchers that may still be referenced by old guest threads.
    return false;
  }

  auto memory = std::make_unique<XenonAddressSpaceGuestMemory>(address_space);
  auto frames = std::make_unique<NuiFrameStore>();
  auto dispatcher = std::make_unique<NuiDispatcher>(*memory, *frames);
  g_state.address_space = &address_space;
  g_state.memory = std::move(memory);
  g_state.frames = std::move(frames);
  g_state.dispatcher = std::move(dispatcher);
  return true;
}

}  // namespace

bool bind_xenon_nui_module(xenon::cpu::ExecutionContext& context) noexcept {
  try {
    auto* address_space =
        dynamic_cast<xenon::memory::AddressSpace*>(&context.memory);
    if (address_space == nullptr) {
      return false;
    }

    std::lock_guard lock(g_state_mutex);
    if (!ensure_state(*address_space)) {
      return false;
    }
    install_xenon_nui_registry_overlay(context, g_thread_overlay,
                                       *g_state.dispatcher);
    return true;
  } catch (...) {
    return false;
  }
}

NuiFrameStore* xenon_nui_frame_store() noexcept {
  std::lock_guard lock(g_state_mutex);
  return g_state.frames.get();
}

NuiDispatcher* xenon_nui_dispatcher() noexcept {
  std::lock_guard lock(g_state_mutex);
  return g_state.dispatcher.get();
}

}  // namespace fnk::nui

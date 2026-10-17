#pragma once

namespace fnk::nui {

class NuiDispatcher;

// Must be called once after the guest mapping and NUI provider are created.
// The dispatcher must outlive all recompiled title threads.
void bind_xenon_nui_dispatcher(NuiDispatcher* dispatcher) noexcept;

}  // namespace fnk::nui

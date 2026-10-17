#pragma once

namespace xenon::cpu {
struct ExecutionContext;
}

namespace fnk::nui {

class NuiDispatcher;
class NuiFrameStore;

// Installs the Fruit Ninja NUI lookup overlay into a CPU V2 execution
// context. Call immediately after the generated bind_compiled_registry().
// Runtime state is shared by all guest threads; the overlay itself is
// thread-local because ExecutionContext instances are thread-local.
bool bind_xenon_nui_module(xenon::cpu::ExecutionContext& context) noexcept;

// Producer-side accessors. They become non-null after the first successful
// bind and let the webcam/pose worker publish frames into the active session.
NuiFrameStore* xenon_nui_frame_store() noexcept;
NuiDispatcher* xenon_nui_dispatcher() noexcept;

}  // namespace fnk::nui

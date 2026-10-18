#pragma once

namespace rex::memory {
class Memory;
}
namespace rex::system {
class KernelState;
}

namespace fnk::rexbridge {

void initialize(rex::memory::Memory* memory, rex::system::KernelState* kernel_state);
void shutdown();

}  // namespace fnk::rexbridge

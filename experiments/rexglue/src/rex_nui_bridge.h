#pragma once

namespace rex::memory {
class Memory;
}

namespace fnk::rexbridge {

void initialize(rex::memory::Memory* memory);
void shutdown();

}  // namespace fnk::rexbridge

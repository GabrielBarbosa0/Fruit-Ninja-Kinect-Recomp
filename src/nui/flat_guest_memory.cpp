#include "fnk/nui/flat_guest_memory.hpp"

#include <cstring>
#include <limits>

namespace fnk::nui {

FlatGuestMemory::FlatGuestMemory(std::uint8_t* base, guest::Address arena_begin,
                                 std::size_t arena_size)
    : base_(base), arena_begin_(arena_begin), arena_size_(arena_size) {}

bool FlatGuestMemory::write(guest::Address address,
                            std::span<const std::uint8_t> bytes) {
  if (base_ == nullptr ||
      bytes.size() > std::numeric_limits<std::uint32_t>::max() - address) {
    return false;
  }
  std::memcpy(base_ + address, bytes.data(), bytes.size());
  return true;
}

guest::Address FlatGuestMemory::allocate(std::size_t size, std::size_t alignment) {
  if (base_ == nullptr || alignment == 0 || (alignment & (alignment - 1U)) != 0U) {
    return 0;
  }
  std::lock_guard lock(mutex_);
  const std::size_t aligned = (arena_offset_ + alignment - 1U) & ~(alignment - 1U);
  if (aligned > arena_size_ || size > arena_size_ - aligned ||
      aligned + size > std::numeric_limits<std::uint32_t>::max() - arena_begin_) {
    return 0;
  }
  const guest::Address result = arena_begin_ + static_cast<guest::Address>(aligned);
  arena_offset_ = aligned + size;
  return result;
}

void FlatGuestMemory::release(guest::Address) {
  // The arena is intentionally monotonic. NUI owns at most two streams and
  // reset() reclaims everything during runtime reinitialization.
}

void FlatGuestMemory::reset() {
  std::lock_guard lock(mutex_);
  arena_offset_ = 0;
}

}  // namespace fnk::nui

#pragma once

#include "fnk/nui/guest_memory.hpp"

#include <cstddef>
#include <cstdint>
#include <mutex>

namespace fnk::nui {

// Adapter for XenonRecomp's 4 GiB base-address mapping. The runtime owns and
// commits the mapping; this class owns only a caller-reserved scratch arena.
class FlatGuestMemory final : public GuestMemory {
 public:
  FlatGuestMemory(std::uint8_t* base, guest::Address arena_begin,
                  std::size_t arena_size);

  bool write(guest::Address address,
             std::span<const std::uint8_t> bytes) override;
  guest::Address allocate(std::size_t size, std::size_t alignment) override;
  void release(guest::Address address) override;

  void reset();

 private:
  std::uint8_t* base_{};
  guest::Address arena_begin_{};
  std::size_t arena_size_{};
  std::size_t arena_offset_{};
  std::mutex mutex_{};
};

}  // namespace fnk::nui

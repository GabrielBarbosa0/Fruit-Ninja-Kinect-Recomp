#pragma once

#include "fnk/nui/guest_abi.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace fnk::nui {

class GuestMemory {
 public:
  virtual ~GuestMemory() = default;

  virtual bool write(guest::Address address,
                     std::span<const std::uint8_t> bytes) = 0;
  virtual guest::Address allocate(std::size_t size, std::size_t alignment) = 0;
  virtual void release(guest::Address address) = 0;

  template <class T>
  bool write_object(guest::Address address, const T& object) {
    return write(address, {reinterpret_cast<const std::uint8_t*>(&object), sizeof(T)});
  }

  bool write_u32(guest::Address address, std::uint32_t value) {
    guest::U32 encoded{};
    encoded.set(value);
    return write_object(address, encoded);
  }
};

}  // namespace fnk::nui

#include "fnk/nui/xenon_address_space_memory.hpp"

#include <limits>

#include "xenon/memory/address_space.hpp"

namespace fnk::nui {

bool XenonAddressSpaceGuestMemory::write(guest::Address address,
                                         std::span<const std::uint8_t> bytes) {
  if (bytes.size() > std::numeric_limits<std::uint32_t>::max() - address) {
    return false;
  }

  try {
    address_space_.write_bytes(
        address,
        {reinterpret_cast<const std::byte*>(bytes.data()), bytes.size()});
    return true;
  } catch (...) {
    // Guest faults are converted to the boolean GuestMemory contract. The NUI
    // dispatcher will expose these failures to the title as E_POINTER.
    return false;
  }
}

guest::Address XenonAddressSpaceGuestMemory::allocate(std::size_t size,
                                                      std::size_t alignment) {
  if (size == 0 || size > std::numeric_limits<std::uint32_t>::max() ||
      alignment == 0 || alignment > std::numeric_limits<std::uint32_t>::max() ||
      (alignment & (alignment - 1U)) != 0U) {
    return 0;
  }

  guest::Address address{};
  const bool allocated = address_space_.allocate(
      static_cast<std::uint32_t>(size), static_cast<std::uint32_t>(alignment),
      xenon::memory::kReadWrite, true, address);
  return allocated ? address : 0;
}

void XenonAddressSpaceGuestMemory::release(guest::Address address) {
  if (address != 0) {
    (void)address_space_.release(address);
  }
}

}  // namespace fnk::nui

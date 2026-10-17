#pragma once

#include "fnk/nui/guest_memory.hpp"

namespace xenon::memory {
class AddressSpace;
}

namespace fnk::nui {

// GuestMemory implementation for the modern tools/Xenon-Recomp runtime.
// AddressSpace owns the mappings; this adapter only translates the small API
// used by the NUI dispatcher into runtime allocations and byte transfers.
class XenonAddressSpaceGuestMemory final : public GuestMemory {
 public:
  explicit XenonAddressSpaceGuestMemory(
      xenon::memory::AddressSpace& address_space)
      : address_space_(address_space) {}

  bool write(guest::Address address,
             std::span<const std::uint8_t> bytes) override;
  guest::Address allocate(std::size_t size, std::size_t alignment) override;
  void release(guest::Address address) override;

 private:
  xenon::memory::AddressSpace& address_space_;
};

}  // namespace fnk::nui

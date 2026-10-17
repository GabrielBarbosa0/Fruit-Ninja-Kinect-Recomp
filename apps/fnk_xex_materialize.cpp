#include <image.h>
#include <TinySHA1.hpp>
#include <xex.h>

#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: fnk_xex_materialize <input.xex> <output.xex>\n";
    return 2;
  }

  const std::filesystem::path input_path = argv[1];
  const std::filesystem::path output_path = argv[2];
  std::ifstream input(input_path, std::ios::binary);
  if (!input) {
    std::cerr << "unable to open input XEX: " << input_path << '\n';
    return 1;
  }
  const std::vector<std::uint8_t> source{
      std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
  if (source.size() < sizeof(Xex2Header)) {
    std::cerr << "input is too small to contain an XEX2 header\n";
    return 1;
  }

  const auto* header = reinterpret_cast<const Xex2Header*>(source.data());
  const std::uint32_t header_size = header->headerSize;
  if (header->magic != 0x58455832U || header_size > source.size()) {
    std::cerr << "input is not a valid XEX2 image\n";
    return 1;
  }

  Image image = Xex2LoadImage(source.data(), source.size());
  if (!image.data || image.size == 0) {
    std::cerr << "the legacy loader could not decrypt/decompress this XEX\n";
    return 1;
  }

  std::vector<std::uint8_t> materialized(
      static_cast<std::size_t>(header_size) + image.size);
  std::memcpy(materialized.data(), source.data(), header_size);
  std::memcpy(materialized.data() + header_size, image.data.get(), image.size);

  auto* format = const_cast<Xex2OptFileFormatInfo*>(
      static_cast<const Xex2OptFileFormatInfo*>(
          getOptHeaderPtr(materialized.data(), XEX_HEADER_FILE_FORMAT_INFO)));
  if (format == nullptr) {
    std::cerr << "XEX has no file-format optional header\n";
    return 1;
  }
  format->infoSize = sizeof(Xex2OptFileFormatInfo);
  format->encryptionType = XEX_ENCRYPTION_NONE;
  format->compressionType = XEX_COMPRESSION_NONE;

  // The modern loader validates a derived first-page digest before PE
  // parsing. Recompute it for the materialized body while retaining the
  // original page descriptor and its per-run digest.
  auto* materialized_header = reinterpret_cast<Xex2Header*>(materialized.data());
  auto* security = reinterpret_cast<Xex2SecurityInfo*>(
      materialized.data() +
      static_cast<std::uint32_t>(materialized_header->securityOffset));
  if (security->pageDescriptorCount != 0) {
    auto* first_descriptor = reinterpret_cast<Xex2PageDescriptor*>(security + 1);
    const std::uint32_t descriptor_value = ByteSwap(first_descriptor->beValue);
    const std::uint64_t page_size =
        (static_cast<std::uint32_t>(security->imageFlags) & 0x10000000U) != 0U
            ? 4096U
            : 65536U;
    const std::uint64_t first_run_size =
        static_cast<std::uint64_t>(descriptor_value >> 4U) * page_size;
    if (first_run_size <= image.size) {
      sha1::SHA1 digest;
      digest.processBytes(image.data.get(),
                          static_cast<std::size_t>(first_run_size));
      digest.processBytes(first_descriptor, sizeof(Xex2PageDescriptor));
      sha1::SHA1::digest8_t bytes{};
      digest.finalize(bytes);
      std::memcpy(security->sectionDigest, bytes, sizeof(bytes));
    }
  }

  if (!output_path.parent_path().empty()) {
    std::filesystem::create_directories(output_path.parent_path());
  }
  std::ofstream output(output_path, std::ios::binary | std::ios::trunc);
  output.write(reinterpret_cast<const char*>(materialized.data()),
               static_cast<std::streamsize>(materialized.size()));
  if (!output) {
    std::cerr << "unable to write materialized XEX: " << output_path << '\n';
    return 1;
  }

  std::cout << "Materialized XEX2: " << output_path << " (header="
            << header_size << ", image=" << image.size << ", total="
            << materialized.size() << ")\n";
  return 0;
}

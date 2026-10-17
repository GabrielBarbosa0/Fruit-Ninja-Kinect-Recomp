#include <cstdint>

#include <byteswap.h>
#include <disasm.h>
#include <file.h>
#include <image.h>
#include <xbox.h>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

struct FunctionRange {
  std::uint32_t begin{};
  std::uint32_t size{};
};

std::string lower(std::string_view value) {
  std::string result(value);
  std::transform(result.begin(), result.end(), result.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return result;
}

std::size_t mapped_size(const Image& image, const Section& section) {
  const std::size_t image_end = image.base + image.size;
  if (section.base >= image_end) {
    return 0;
  }
  return std::min<std::size_t>(section.size, image_end - section.base);
}

std::vector<FunctionRange> read_functions(const Image& image) {
  std::vector<FunctionRange> result;
  const Section* pdata = image.Find(".pdata");
  if (pdata == nullptr) {
    return result;
  }
  const auto* entries = reinterpret_cast<const IMAGE_CE_RUNTIME_FUNCTION*>(pdata->data);
  const std::size_t count = pdata->size / sizeof(IMAGE_CE_RUNTIME_FUNCTION);
  result.reserve(count);
  for (std::size_t index = 0; index < count; ++index) {
    IMAGE_CE_RUNTIME_FUNCTION entry = entries[index];
    entry.BeginAddress = ByteSwap(entry.BeginAddress);
    entry.Data = ByteSwap(entry.Data);
    if (entry.BeginAddress != 0 && entry.FunctionLength != 0) {
      result.push_back({entry.BeginAddress, entry.FunctionLength * 4U});
    }
  }
  std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
    return left.begin < right.begin;
  });
  return result;
}

const FunctionRange* containing_function(const std::vector<FunctionRange>& functions,
                                         std::uint32_t address) {
  const auto after = std::upper_bound(
      functions.begin(), functions.end(), address,
      [](std::uint32_t value, const FunctionRange& function) { return value < function.begin; });
  if (after == functions.begin()) {
    return nullptr;
  }
  const FunctionRange& candidate = *std::prev(after);
  return address < candidate.begin + candidate.size ? &candidate : nullptr;
}

std::vector<std::uint32_t> find_strings(const Image& image,
                                        const std::vector<std::string>& needles) {
  std::vector<std::uint32_t> result;
  for (const Section& section : image.sections) {
    if ((section.flags & SectionFlags_Code) != 0) {
      continue;
    }
    const std::size_t size = mapped_size(image, section);
    std::size_t cursor = 0;
    while (cursor < size) {
      while (cursor < size &&
             (section.data[cursor] < 0x20 || section.data[cursor] > 0x7E)) {
        ++cursor;
      }
      const std::size_t start = cursor;
      while (cursor < size && section.data[cursor] >= 0x20 &&
             section.data[cursor] <= 0x7E) {
        ++cursor;
      }
      if (cursor - start < 4) {
        continue;
      }
      const std::string_view text(reinterpret_cast<const char*>(section.data + start),
                                  cursor - start);
      const std::string folded = lower(text);
      bool matches = needles.empty();
      for (const std::string& needle : needles) {
        if (folded.find(lower(needle)) != std::string::npos) {
          matches = true;
          break;
        }
      }
      if (matches) {
        const auto address = static_cast<std::uint32_t>(section.base + start);
        std::cout << "STRING 0x" << std::hex << std::uppercase << address << " " << text
                  << '\n';
        result.push_back(address);
      }
    }
  }
  return result;
}

std::vector<std::uint32_t> pointer_aliases(const Image& image,
                                           const std::vector<std::uint32_t>& targets) {
  std::vector<std::uint32_t> result = targets;
  for (const Section& section : image.sections) {
    if ((section.flags & SectionFlags_Code) != 0) {
      continue;
    }
    const std::size_t size = mapped_size(image, section);
    for (std::size_t offset = 0; offset + 4 <= size; offset += 4) {
      const std::uint32_t value = ByteSwap(*reinterpret_cast<const std::uint32_t*>(
          section.data + offset));
      if (std::find(targets.begin(), targets.end(), value) != targets.end()) {
        const auto alias = static_cast<std::uint32_t>(section.base + offset);
        std::cout << "POINTER 0x" << std::hex << std::uppercase << alias << " -> 0x" << value
                  << '\n';
        result.push_back(alias);
      }
    }
  }
  std::sort(result.begin(), result.end());
  result.erase(std::unique(result.begin(), result.end()), result.end());
  return result;
}

std::set<std::uint32_t> find_code_references(const Image& image,
                                             const std::vector<std::uint32_t>& targets) {
  std::set<std::uint32_t> result;
  for (const Section& section : image.sections) {
    if ((section.flags & SectionFlags_Code) == 0) {
      continue;
    }
    const std::size_t instruction_count = mapped_size(image, section) / 4U;
    const auto* words = reinterpret_cast<const std::uint32_t*>(section.data);
    for (std::size_t index = 0; index < instruction_count; ++index) {
      const std::uint32_t first = ByteSwap(words[index]);
      if ((first >> 26U) != 15U || ((first >> 16U) & 31U) != 0U) {
        continue;
      }
      const std::uint32_t reg = (first >> 21U) & 31U;
      const std::uint32_t high = (first & 0xFFFFU) << 16U;
      const std::size_t end = std::min(instruction_count, index + 7U);
      for (std::size_t next = index + 1U; next < end; ++next) {
        const std::uint32_t second = ByteSwap(words[next]);
        const std::uint32_t opcode = second >> 26U;
        const std::uint32_t base = (second >> 16U) & 31U;
        if (base != reg || (opcode != 14U && opcode != 24U)) {
          continue;
        }
        const std::uint32_t low = second & 0xFFFFU;
        const std::uint32_t value = opcode == 14U
                                        ? high + static_cast<std::int16_t>(low)
                                        : high | low;
        if (std::find(targets.begin(), targets.end(), value) != targets.end()) {
          const auto address = static_cast<std::uint32_t>(section.base + index * 4U);
          std::cout << "XREF 0x" << std::hex << std::uppercase << address << " -> 0x" << value
                    << '\n';
          result.insert(address);
        }
      }
    }
  }
  return result;
}

std::set<std::uint32_t> find_direct_callers(
    const Image& image, const std::vector<std::uint32_t>& targets) {
  std::set<std::uint32_t> result;
  for (const Section& section : image.sections) {
    if ((section.flags & SectionFlags_Code) == 0) {
      continue;
    }
    const std::size_t instruction_count = mapped_size(image, section) / 4U;
    const auto* words = reinterpret_cast<const std::uint32_t*>(section.data);
    for (std::size_t index = 0; index < instruction_count; ++index) {
      const std::uint32_t instruction = ByteSwap(words[index]);
      if ((instruction >> 26U) != 18U || (instruction & 1U) == 0U) {
        continue;
      }
      std::int32_t displacement = static_cast<std::int32_t>(instruction & 0x03FFFFFCU);
      if ((displacement & 0x02000000) != 0) {
        displacement |= static_cast<std::int32_t>(0xFC000000U);
      }
      const auto call_address = static_cast<std::uint32_t>(section.base + index * 4U);
      const std::uint32_t target = (instruction & 2U) != 0U
                                       ? static_cast<std::uint32_t>(displacement)
                                       : call_address + displacement;
      if (std::find(targets.begin(), targets.end(), target) != targets.end()) {
        std::cout << "CALL 0x" << std::hex << std::uppercase << call_address << " -> 0x"
                  << target << '\n';
        result.insert(call_address);
      }
    }
  }
  return result;
}

void disassemble_function(const Image& image, const FunctionRange& function) {
  const auto* code = static_cast<const std::uint32_t*>(image.Find(function.begin));
  std::cout << "\nFUNCTION 0x" << std::hex << std::uppercase << function.begin << " size 0x"
            << function.size << '\n';
  for (std::uint32_t offset = 0; offset < function.size; offset += 4U) {
    ppc_insn instruction{};
    ppc::Disassemble(code + offset / 4U, function.begin + offset, instruction);
    std::cout << "  " << std::setw(8) << std::setfill('0') << function.begin + offset << "  "
              << std::setw(8) << ByteSwap(code[offset / 4U]) << "  ";
    if (instruction.opcode != nullptr) {
      std::cout << std::left << std::setw(12) << std::setfill(' ') << instruction.opcode->name
                << instruction.op_str;
    } else {
      std::cout << "<invalid>";
    }
    std::cout << '\n' << std::right;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "Usage: fnk_nui_abi_probe default.xex report.txt [search terms...]\n";
    return EXIT_FAILURE;
  }
  const auto bytes = LoadFile(argv[1]);
  if (bytes.empty()) {
    std::cerr << "Could not read XEX.\n";
    return EXIT_FAILURE;
  }
  Image image = Image::ParseImage(bytes.data(), bytes.size());
  if (!image.data) {
    std::cerr << "Could not decrypt/decompress XEX.\n";
    return EXIT_FAILURE;
  }

  std::ofstream report(argv[2]);
  if (!report) {
    std::cerr << "Could not create report.\n";
    return EXIT_FAILURE;
  }
  std::streambuf* original = std::cout.rdbuf(report.rdbuf());
  std::cout << "IMAGE base=0x" << std::hex << std::uppercase << image.base << " size=0x"
            << image.size << " entry=0x" << image.entry_point << '\n';
  for (const Section& section : image.sections) {
    std::cout << "SECTION " << section.name << " base=0x" << section.base << " size=0x"
              << section.size << " flags=" << static_cast<unsigned>(section.flags) << '\n';
  }

  std::vector<std::string> needles;
  std::vector<std::uint32_t> requested_addresses;
  for (int index = 3; index < argc; ++index) {
    const std::string argument(argv[index]);
    if (argument.starts_with("0x") || argument.starts_with("0X")) {
      requested_addresses.push_back(
          static_cast<std::uint32_t>(std::stoul(argument, nullptr, 16)));
    } else {
      needles.push_back(argument);
    }
  }
  if (needles.empty() && requested_addresses.empty()) {
    needles = {"nui", "kinect", "skeleton", "depth", "camera", "player index"};
  }

  auto targets = needles.empty() ? std::vector<std::uint32_t>{}
                                 : find_strings(image, needles);
  targets.insert(targets.end(), requested_addresses.begin(), requested_addresses.end());
  targets = pointer_aliases(image, targets);
  const auto references = find_code_references(image, targets);
  const auto functions = read_functions(image);
  std::set<std::uint32_t> emitted;
  for (std::uint32_t reference : references) {
    const FunctionRange* function = containing_function(functions, reference);
    if (function != nullptr && emitted.insert(function->begin).second) {
      disassemble_function(image, *function);
    }
  }
  const auto callers = find_direct_callers(image, requested_addresses);
  for (std::uint32_t caller : callers) {
    const FunctionRange* function = containing_function(functions, caller);
    if (function != nullptr && emitted.insert(function->begin).second) {
      disassemble_function(image, *function);
    }
  }
  for (std::uint32_t address : requested_addresses) {
    const FunctionRange* function = containing_function(functions, address);
    if (function != nullptr && emitted.insert(function->begin).second) {
      disassemble_function(image, *function);
    } else if (function == nullptr) {
      std::cout << "NO_FUNCTION 0x" << std::hex << std::uppercase << address << '\n';
    }
  }
  std::cout.rdbuf(original);
  std::cout << "Wrote ABI probe report to " << argv[2] << '\n';
  return EXIT_SUCCESS;
}

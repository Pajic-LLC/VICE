#include <array>
#include <cstddef>
#include <cstdint>

namespace {

constexpr std::size_t stamp_size = 320U;
constexpr std::size_t end_magic_offset = 312U;

[[nodiscard]] constexpr std::array<std::uint8_t, stamp_size> make_package_metadata_slot() {
    std::array<std::uint8_t, stamp_size> bytes{};
    constexpr std::array<std::uint8_t, 8> start{
        'V', 'I', 'C', 'E', 'P', 'K', 'G', '0'
    };
    constexpr std::array<std::uint8_t, 8> end{
        '0', 'G', 'K', 'P', 'E', 'C', 'I', 'V'
    };

    for (std::size_t index = 0; index < start.size(); ++index) {
        bytes[index] = start[index];
        bytes[end_magic_offset + index] = end[index];
    }
    return bytes;
}

alignas(16) std::array<std::uint8_t, stamp_size> package_metadata_slot =
    make_package_metadata_slot();

struct KeepPackageMetadataSlot {
    KeepPackageMetadataSlot() noexcept {
        volatile const std::uint8_t* byte = package_metadata_slot.data();
        (void)*byte;
    }
};

KeepPackageMetadataSlot keep_package_metadata_slot;

} // namespace

#include <vice/world/persistent_id.hpp>

#include <array>
#include <random>

namespace vice::world {
namespace {

std::optional<std::uint8_t> hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') {
        return static_cast<std::uint8_t>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<std::uint8_t>(value - 'a' + 10);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<std::uint8_t>(value - 'A' + 10);
    }
    return std::nullopt;
}

} // namespace

PersistentId PersistentId::random() {
    std::random_device random;
    Bytes bytes{};

    for (auto& byte : bytes) {
        byte = static_cast<std::uint8_t>(random());
    }

    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0fU) | 0x40U);
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3fU) | 0x80U);

    return PersistentId{bytes};
}

std::optional<PersistentId> PersistentId::parse(const std::string_view text) noexcept {
    if (text.size() != 36 || text[8] != '-' || text[13] != '-' || text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }

    std::array<char, 32> compact{};
    std::size_t compact_index = 0;

    for (std::size_t index = 0; index < text.size(); ++index) {
        if (index == 8 || index == 13 || index == 18 || index == 23) {
            continue;
        }
        compact[compact_index++] = text[index];
    }

    Bytes bytes{};
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        const auto high = hex_value(compact[index * 2]);
        const auto low = hex_value(compact[index * 2 + 1]);
        if (!high || !low) {
            return std::nullopt;
        }
        bytes[index] = static_cast<std::uint8_t>((*high << 4U) | *low);
    }

    return PersistentId{bytes};
}

std::string PersistentId::to_string() const {
    static constexpr char hex[] = "0123456789abcdef";

    std::string text;
    text.reserve(36);

    for (std::size_t index = 0; index < bytes_.size(); ++index) {
        if (index == 4 || index == 6 || index == 8 || index == 10) {
            text.push_back('-');
        }

        const auto byte = bytes_[index];
        text.push_back(hex[(byte >> 4U) & 0x0fU]);
        text.push_back(hex[byte & 0x0fU]);
    }

    return text;
}

std::size_t PersistentIdHash::operator()(const PersistentId& id) const noexcept {
    std::size_t hash = static_cast<std::size_t>(1469598103934665603ULL);

    for (const auto byte : id.bytes()) {
        hash ^= static_cast<std::size_t>(byte);
        hash *= static_cast<std::size_t>(1099511628211ULL);
    }

    return hash;
}

} // namespace vice::world

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vice::world {

class PersistentId {
public:
    using Bytes = std::array<std::uint8_t, 16>;

    constexpr PersistentId() noexcept = default;
    explicit constexpr PersistentId(Bytes bytes) noexcept : bytes_{bytes} {}

    [[nodiscard]] static PersistentId random();
    [[nodiscard]] static std::optional<PersistentId> parse(std::string_view text) noexcept;

    [[nodiscard]] constexpr const Bytes& bytes() const noexcept {
        return bytes_;
    }

    [[nodiscard]] constexpr bool is_nil() const noexcept {
        for (const auto byte : bytes_) {
            if (byte != 0) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] std::string to_string() const;

    friend constexpr bool operator==(const PersistentId&, const PersistentId&) noexcept = default;

private:
    Bytes bytes_{};
};

struct PersistentIdHash {
    [[nodiscard]] std::size_t operator()(const PersistentId& id) const noexcept;
};

} // namespace vice::world

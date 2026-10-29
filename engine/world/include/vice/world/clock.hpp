#pragma once
#include <cstdint>
#include <iosfwd>
#include <string>
namespace vice::world {
inline constexpr std::uint64_t world_ticks_per_second = 1'000'000;
struct WorldClock {
    std::uint64_t ticks = 0;
    std::uint32_t rate_milli = 1000, rate_remainder = 0;
    double input_fraction = 0;
    bool paused = false, automatic = true;
    friend bool operator==(const WorldClock &, const WorldClock &) = default;
};
[[nodiscard]] bool valid(const WorldClock &) noexcept;
bool read_world_clock(std::istream &, WorldClock &);
void write_world_clock(std::ostream &, const WorldClock &);
[[nodiscard]] std::string serialize_world_clock(const WorldClock &);
} // namespace vice::world

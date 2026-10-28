#include <charconv>
#include <cmath>
#include <iomanip>
#include <istream>
#include <limits>
#include <locale>
#include <sstream>
#include <vice/world/world.hpp>
namespace vice::world {
bool valid(const WorldClock &c) noexcept {
    return c.rate_milli >= 1 && c.rate_milli <= 1'000'000 && c.rate_remainder < 1000 &&
           std::isfinite(c.input_fraction) && c.input_fraction >= 0 && c.input_fraction < 1;
}
bool World::configure_world_clock(bool paused, std::uint32_t rate, bool automatic) {
    if (!rate || rate > 1'000'000)
        return false;
    clock_.paused = paused;
    clock_.rate_milli = rate;
    clock_.automatic = automatic;
    return true;
}
bool World::step_world_clock(std::uint64_t ticks) {
    if (ticks > std::numeric_limits<std::uint64_t>::max() - clock_.ticks)
        return false;
    clock_.ticks += ticks;
    prune_world_signals();
    return true;
}
bool World::advance_world_clock_ticks(std::uint64_t input) {
    if (clock_.paused)
        return true;
    const auto q = input / 1000, r = input % 1000;
    const auto maximum = std::numeric_limits<std::uint64_t>::max();
    if (q > maximum / clock_.rate_milli)
        return false;
    const auto whole = q * clock_.rate_milli;
    const auto fraction = r * clock_.rate_milli + clock_.rate_remainder;
    const auto extra = fraction / 1000;
    if (whole > maximum - extra || whole + extra > maximum - clock_.ticks)
        return false;
    clock_.ticks += whole + extra;
    prune_world_signals();
    clock_.rate_remainder = static_cast<std::uint32_t>(fraction % 1000);
    return true;
}
bool World::advance_world_clock(double seconds) {
    if (!std::isfinite(seconds) || seconds < 0 || seconds > .25)
        return false;
    if (!clock_.automatic || clock_.paused)
        return true;
    const double raw =
        seconds * static_cast<double>(world_ticks_per_second) + clock_.input_fraction;
    const auto ticks = static_cast<std::uint64_t>(raw);
    const auto fraction = raw - static_cast<double>(ticks);
    if (!advance_world_clock_ticks(ticks))
        return false;
    clock_.input_fraction = fraction == 0 ? 0 : fraction;
    return true;
}
namespace {
template <class T> bool number(std::istream &in, T &n) {
    std::string s;
    if (!(in >> s))
        return false;
    const auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    return r.ec == std::errc{} && r.ptr == s.data() + s.size();
}
} // namespace
bool read_world_clock(std::istream &in, WorldClock &c) {
    in.imbue(std::locale::classic());
    WorldClock next;
    unsigned paused = 0, automatic = 0;
    if (!number(in, next.ticks) || !number(in, next.rate_milli) ||
        !number(in, next.rate_remainder) || !(in >> next.input_fraction) || !number(in, paused) ||
        paused > 1 || !number(in, automatic) || automatic > 1)
        return false;
    next.paused = paused != 0;
    next.automatic = automatic != 0;
    if (!valid(next))
        return false;
    c = next;
    return true;
}
void write_world_clock(std::ostream &out, const WorldClock &c) {
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << c.ticks << ' ' << c.rate_milli << ' ' << c.rate_remainder << ' '
        << c.input_fraction << ' ' << c.paused << ' ' << c.automatic;
}
std::string serialize_world_clock(const WorldClock &c) {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"ticks\":\"" << c.ticks
        << "\",\"ticks_per_second\":" << world_ticks_per_second
        << ",\"rate_milli\":" << c.rate_milli << ",\"rate_remainder\":" << c.rate_remainder
        << ",\"input_fraction\":" << c.input_fraction
        << ",\"paused\":" << (c.paused ? "true" : "false")
        << ",\"automatic\":" << (c.automatic ? "true" : "false") << '}';
    return out.str();
}
} // namespace vice::world

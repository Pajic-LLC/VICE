#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *s) {
    if (!ok) {
        std::cerr << "Clock: " << s << '\n';
        std::exit(1);
    }
}
} // namespace
int main() {
    using namespace vice;
    world::World w;
    require(w.configure_world_clock(false, 500, true), "rate");
    require(w.advance_world_clock_ticks(1) && w.world_clock().ticks == 0 &&
                w.world_clock().rate_remainder == 500,
            "fixed-point fraction");
    auto snapshot = w.snapshot();
    world::World restored;
    require(restored.restore(snapshot) && restored.advance_world_clock_ticks(1) &&
                restored.world_clock().ticks == 1,
            "persist rate remainder");
    require(w.advance_world_clock_ticks(1) && w.world_clock() == restored.world_clock(),
            "split/resume identical");
    require(w.configure_world_clock(true, 2000, true) && w.advance_world_clock(.25) &&
                w.world_clock().ticks == 1 && w.step_world_clock(7) && w.world_clock().ticks == 8,
            "paused source, exact manual step");
    require(!w.configure_world_clock(false, 0, true) &&
                !w.advance_world_clock(std::numeric_limits<double>::quiet_NaN()),
            "invalid input");
    world::World fractional;
    for (unsigned i = 0; i < 3; ++i)
        require(fractional.advance_world_clock(.00000025), "sub-tick input");
    require(fractional.world_clock().ticks == 0 && fractional.world_clock().input_fraction == .75,
            "input fraction");
    world::World resumed;
    const auto encoded = world::encode_world_snapshot(fractional.snapshot());
    require(encoded && resumed.restore(*world::decode_world_snapshot(encoded.bytes).snapshot) &&
                resumed.advance_world_clock(.00000025) && resumed.world_clock().ticks == 1,
            "persist input fraction");
    const auto source = tools::world_cooker::serialize_source_manifest(w.snapshot());
    const auto parsed = tools::world_cooker::parse_source_manifest(source);
    require(parsed.snapshot && parsed.snapshot->clock == w.world_clock() &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == source,
            "source canonical clock");
    auto bad = w.snapshot();
    bad.clock.rate_remainder = 1000;
    require(!w.restore(bad) && w.world_clock().ticks == 8, "atomic invalid clock restore");
    world::World overflow;
    require(overflow.step_world_clock(std::numeric_limits<std::uint64_t>::max()), "maximum tick");
    const auto before = overflow.world_clock();
    require(!overflow.step_world_clock(1) && !overflow.advance_world_clock_ticks(1) &&
                overflow.world_clock() == before,
            "overflow is transactional");
    world::World accelerated;
    require(accelerated.configure_world_clock(false, 1'000'000, true) &&
                accelerated.advance_world_clock_ticks(1000) &&
                accelerated.world_clock().ticks == 1'000'000,
            "accelerated generic time");
    require(accelerated.configure_world_clock(false, 1000, false) &&
                accelerated.advance_world_clock(.25) &&
                accelerated.world_clock().ticks == 1'000'000,
            "automatic disabled");
    tools::world_authoring::Transaction tx;
    tx.operations = {tools::world_authoring::SetWorldClock{w.world_clock()}};
    require(static_cast<bool>(tools::world_authoring::apply(world::World{}.snapshot(), tx)),
            "typed clock authoring");
    auto legacy = world::encode_world_snapshot(world::World{}.snapshot()).bytes;
    legacy.resize(legacy.size() - 26);
    legacy[8] = std::byte{17};
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < legacy.size(); ++i) {
        hash ^= std::to_integer<unsigned char>(legacy[i]);
        hash *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        legacy[32 + i] = static_cast<std::byte>(hash >> (8 * i));
    const auto old = world::decode_world_snapshot(legacy);
    require(old.snapshot && old.snapshot->clock == world::WorldClock{}, "v17 default clock");
    std::cout << "World clock integer stepping, fractions, pause/rate and persistence passed\n";
}

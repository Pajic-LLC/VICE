#pragma once

#include <vice/world/snapshot.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace vice::tools::world_cooker {

struct ParseResult {
    std::optional<world::WorldSnapshot> snapshot;
    std::string diagnostics;
};

[[nodiscard]] ParseResult parse_source_manifest(std::string_view source);

} // namespace vice::tools::world_cooker

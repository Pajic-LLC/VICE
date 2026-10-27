#pragma once
#include <vice/runtime/dev_session.hpp>
#include <vice/world/snapshot_file.hpp>
#include <string>
#include <string_view>

namespace vice::runtime {
// Only a fixed session-owned file is published; requests never carry host paths.
// The response binds exact snapshot bytes to the correlated debug request.
[[nodiscard]] std::string publish_runtime_world_checkpoint(
    const RuntimeDevSession& session, const world::WorldSnapshot& snapshot,
    std::string* diagnostics = nullptr);
[[nodiscard]] world::WorldSnapshotEncodeResult read_runtime_world_checkpoint(
    const RuntimeDevSession& session, std::string_view descriptor);
} // namespace vice::runtime

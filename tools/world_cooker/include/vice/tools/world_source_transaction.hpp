#pragma once

#include <vice/tools/world_authoring.hpp>

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace vice::tools::world_authoring {

inline constexpr std::uintmax_t max_authored_world_source_bytes = 16U * 1024U * 1024U;

struct SourceDocument {
    bool success = false;
    world::WorldSnapshot snapshot;
    std::string source;
    std::string revision;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

struct SourceTransactionRequest {
    std::filesystem::path source_path;
    std::string expected_revision;
    Transaction transaction;
    bool dry_run = false;
};

struct SourceTransactionResult {
    bool success = false;
    bool changed = false;
    bool published = false;
    std::size_t applied_operations = 0U;
    std::string revision_before;
    std::string revision_after;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept { return success; }
};

// Stable content precondition token for authored World source. This is a
// concurrency/version token, not a security or authenticity hash.
[[nodiscard]] std::string source_revision(std::string_view source);

[[nodiscard]] SourceDocument load_source_document(
    const std::filesystem::path& source_path
);

[[nodiscard]] SourceTransactionResult apply_source_transaction(
    const SourceTransactionRequest& request
);

} // namespace vice::tools::world_authoring

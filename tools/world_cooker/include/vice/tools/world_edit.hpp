#pragma once

#include <vice/tools/world_authoring.hpp>

#include <filesystem>
#include <string_view>

namespace vice::tools::world_authoring {

inline constexpr std::size_t max_edit_request_bytes = 64U * 1024U;

struct EditRequest {
    std::string expected_revision;
    Transaction transaction;
};

struct EditRequestResult {
    std::optional<EditRequest> request;
    std::string diagnostics;
    std::optional<std::size_t> failed_operation;
};

// Versioned, bounded semantic operations, independent of editor or backend state.
[[nodiscard]] EditRequestResult parse_edit_request(std::string_view source);

struct FileEditResult {
    bool success = false;
    bool changed = false;
    bool published = false;
    bool dry_run = true;
    std::string code;
    std::string previous_revision;
    std::string revision;
    std::string proposed_source;
    std::size_t validated_operations = 0;
    std::vector<world::PersistentId> affected_entities;
    std::vector<world::RegionId> affected_regions;
    std::string diagnostics;
    std::optional<std::size_t> failed_operation;
};

[[nodiscard]] FileEditResult edit_file(
    const std::filesystem::path& path,
    const EditRequest& request,
    bool dry_run
);

} // namespace vice::tools::world_authoring

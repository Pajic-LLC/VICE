#include <vice/editor/project_session.hpp>

#include <vice/platform/file.hpp>
#include <vice/tools/world_authoring.hpp>
#include <vice/tools/world_cooker.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] bool fail(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
    return false;
}

[[nodiscard]] std::filesystem::path absolute_normalized(
    const std::filesystem::path& path,
    std::error_code& error
) {
    if (path.empty()) {
        error = std::make_error_code(std::errc::invalid_argument);
        return {};
    }
    auto result = std::filesystem::absolute(path, error);
    if (error) return {};
    return std::filesystem::weakly_canonical(result, error);
}

[[nodiscard]] bool contains_parent_traversal(const std::filesystem::path& path) {
    for (const auto& component : path) {
        if (component == "..") return true;
    }
    return false;
}

[[nodiscard]] bool path_within(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return candidate == root;
    for (const auto& component : relative) {
        if (component == "..") return false;
    }
    return true;
}

} // namespace

std::optional<std::filesystem::path> resolve_project_authored_world_path(
    const ProjectSession& session,
    const std::filesystem::path& requested,
    const bool require_existing,
    std::string* const diagnostics
) {
    const auto fail_path = [&](std::string message) -> std::optional<std::filesystem::path> {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return std::nullopt;
    };

    if (requested.empty()) return fail_path("Authored World path is empty.");
    if (requested.extension() != ".vworld") {
        return fail_path("Authored World path must use the .vworld extension.");
    }
    if (contains_parent_traversal(requested)) {
        return fail_path("Authored World path cannot contain parent traversal (`..`).");
    }

    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(session.asset_root(), error);
    if (error) {
        return fail_path("Could not resolve the project asset root: " + error.message());
    }

    const auto candidate_input = requested.is_absolute() ? requested : root / requested;
    const auto candidate = std::filesystem::weakly_canonical(candidate_input, error);
    if (error) {
        return fail_path("Could not resolve authored World path: " + error.message());
    }
    if (!path_within(root, candidate) || candidate == root) {
        return fail_path("Authored World path must stay inside the project asset root.");
    }

    if (require_existing) {
        const bool regular = std::filesystem::is_regular_file(candidate, error);
        if (error) {
            return fail_path("Could not inspect authored World path: " + error.message());
        }
        if (!regular) {
            return fail_path("Authored World does not exist: " + candidate.generic_string());
        }
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return candidate;
}

bool WorldDocument::open_authored(
    const std::filesystem::path& path,
    const DirtyReplacePolicy policy,
    std::string* const diagnostics
) {
    if (!replacement_allowed(policy, diagnostics)) return false;
    if (path.extension() != ".vworld") {
        return fail(diagnostics, "Authored World path must use the .vworld extension.");
    }

    std::error_code error;
    const auto resolved = absolute_normalized(path, error);
    if (error) {
        return fail(diagnostics, "Could not resolve authored World path: " + error.message());
    }

    const auto source = platform::read_text_file(resolved);
    if (!source) return fail(diagnostics, source.diagnostics);

    const auto parsed = tools::world_cooker::parse_source_manifest(source.contents);
    if (!parsed.snapshot.has_value()) {
        return fail(
            diagnostics,
            "Could not open authored World `" + resolved.generic_string() + "`: " + parsed.diagnostics
        );
    }

    world::World replacement;
    if (!replacement.restore(*parsed.snapshot)) {
        return fail(diagnostics, "Could not restore authored World `" + resolved.generic_string() + "`.");
    }

    *world_ = std::move(replacement);
    path_ = resolved;
    authored_source_ = source.contents;
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool WorldDocument::save_authored(std::string* const diagnostics) {
    if (!authored()) {
        return fail(diagnostics, "World has no authored .vworld save path. Use Save As first.");
    }
    return save_authored_as(path_, diagnostics);
}

bool WorldDocument::save_authored_as(
    const std::filesystem::path& path,
    std::string* const diagnostics
) {
    if (path.empty()) return fail(diagnostics, "Authored World save path is empty.");
    if (path.extension() != ".vworld") {
        return fail(diagnostics, "Authored World save path must use the .vworld extension.");
    }

    std::error_code error;
    const auto resolved = absolute_normalized(path, error);
    if (error) {
        return fail(diagnostics, "Could not resolve authored World save path: " + error.message());
    }
    const auto parent = resolved.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return fail(diagnostics, "Could not create authored World directory: " + error.message());
        }
    }

    std::string serialize_diagnostics;
    std::string text = tools::world_cooker::serialize_source_manifest(
        world_->snapshot(),
        &serialize_diagnostics
    );
    if (text.empty()) {
        return fail(
            diagnostics,
            serialize_diagnostics.empty()
                ? "Could not serialize authored World."
                : std::move(serialize_diagnostics)
        );
    }

    std::optional<std::string> expected;
    if (resolved == path_ && authored_source_) {
        expected = authored_source_;
    } else if (std::filesystem::exists(resolved, error)) {
        const auto current = platform::read_text_file(resolved);
        if (!current) return fail(diagnostics, current.diagnostics);
        expected = current.contents;
    } else if (error) {
        return fail(diagnostics, "Could not inspect authored World save target: " + error.message());
    }
    // A semantic no-op preserves source comments/whitespace and its revision.
    if (expected) {
        const auto parsed = tools::world_cooker::parse_source_manifest(*expected);
        if (parsed.snapshot && tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == text) text = *expected;
    }
    const auto published = platform::replace_text_file_if_unchanged(resolved,
        expected ? std::optional<std::string_view>{*expected} : std::nullopt, text);
    if (published.state != platform::FileReplaceState::updated &&
        published.state != platform::FileReplaceState::unchanged) {
        return fail(diagnostics, "Could not save authored World. Reload external changes before retrying: " + published.diagnostics);
    }

    path_ = resolved;
    authored_source_ = std::move(text);
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool WorldDocument::apply_authoring_transaction(
    const tools::world_authoring::Transaction& transaction,
    std::string* const diagnostics
) {
    const auto result = tools::world_authoring::apply(world_->snapshot(), transaction);
    if (!result) return fail(diagnostics, result.diagnostics);
    if (result.changed) {
        if (!world_->restore(result.snapshot)) return fail(diagnostics, "Could not restore validated World edit.");
        dirty_ = true;
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor

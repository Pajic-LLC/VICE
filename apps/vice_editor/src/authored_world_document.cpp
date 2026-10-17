#include <vice/editor/project_session.hpp>

#include <vice/tools/world_cooker.hpp>

#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
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
    return result.lexically_normal();
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not open authored World `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not read authored World `" + path.generic_string() + "`.";
        return std::nullopt;
    }
    return contents.str();
}

} // namespace

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

    std::string read_diagnostics;
    const auto source = read_text(resolved, read_diagnostics);
    if (!source.has_value()) return fail(diagnostics, std::move(read_diagnostics));

    const auto parsed = tools::world_cooker::parse_source_manifest(*source);
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
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool WorldDocument::save_authored(std::string* const diagnostics) {
    if (path_.empty()) return fail(diagnostics, "Authored World has no save path. Use Save As first.");
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
    const std::string text = tools::world_cooker::serialize_source_manifest(
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

    const auto temporary = std::filesystem::path{resolved.string() + ".tmp"};
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) return fail(diagnostics, "Could not create temporary authored World file.");
        output << text;
        if (!output) {
            output.close();
            std::filesystem::remove(temporary, error);
            return fail(diagnostics, "Could not write temporary authored World file.");
        }
    }

    std::filesystem::copy_file(
        temporary,
        resolved,
        std::filesystem::copy_options::overwrite_existing,
        error
    );
    std::error_code cleanup_error;
    std::filesystem::remove(temporary, cleanup_error);
    if (error) {
        return fail(diagnostics, "Could not replace authored World file: " + error.message());
    }

    path_ = resolved;
    dirty_ = false;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::editor

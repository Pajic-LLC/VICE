#include <vice/project/project.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace vice::project {
namespace {

[[nodiscard]] int hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] bool read_quoted_value(
    std::istringstream& stream,
    std::string& output
) {
    stream >> std::ws;
    if (!(stream >> std::quoted(output))) return false;
    stream >> std::ws;
    return stream.eof();
}

[[nodiscard]] bool safe_component(const std::filesystem::path& component) noexcept {
    const auto text = component.generic_string();
    return !text.empty() && text != "." && text != "..";
}

[[nodiscard]] bool portable_drive_prefix(const std::string_view text) noexcept {
    return text.size() >= 2U && std::isalpha(static_cast<unsigned char>(text[0])) != 0 && text[1] == ':';
}

[[nodiscard]] std::string portable_path(const std::filesystem::path& path) {
    return path.generic_string();
}

[[nodiscard]] bool write_text_file(
    const std::filesystem::path& path,
    const std::string& text,
    std::string& diagnostics
) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    if (!output) {
        diagnostics = "Could not open project manifest for writing: " + path.generic_string();
        return false;
    }
    output << text;
    output.flush();
    if (!output) {
        diagnostics = "Could not write project manifest: " + path.generic_string();
        return false;
    }
    output.close();
    if (!output) {
        diagnostics = "Could not finish writing project manifest: " + path.generic_string();
        return false;
    }
    return true;
}

} // namespace

std::optional<ProjectId> ProjectId::parse(const std::string_view text) noexcept {
    if (text.size() != 36U || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }

    ProjectId result;
    std::size_t byte_index = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        if (index + 1U >= text.size() || byte_index >= result.bytes.size()) {
            return std::nullopt;
        }
        const int high = hex_value(text[index]);
        const int low = hex_value(text[index + 1U]);
        if (high < 0 || low < 0) return std::nullopt;
        result.bytes[byte_index++] = static_cast<std::uint8_t>((high << 4) | low);
        index += 2U;
    }
    if (byte_index != result.bytes.size() || !result) return std::nullopt;
    return result;
}

std::string ProjectId::to_string() const {
    static constexpr char hex[] = "0123456789abcdef";
    std::string result;
    result.reserve(36U);
    for (std::size_t index = 0; index < bytes.size(); ++index) {
        if (index == 4U || index == 6U || index == 8U || index == 10U) {
            result.push_back('-');
        }
        const auto byte = bytes[index];
        result.push_back(hex[(byte >> 4U) & 0x0FU]);
        result.push_back(hex[byte & 0x0FU]);
    }
    return result;
}

bool valid_relative_path(const std::filesystem::path& path) noexcept {
    const std::string portable = path.generic_string();
    if (path.empty() || portable.find('\\') != std::string::npos || portable_drive_prefix(portable) ||
        path.is_absolute() || path.has_root_name() || path.has_root_directory()) {
        return false;
    }
    for (const auto& component : path) {
        if (!safe_component(component)) return false;
    }
    return true;
}

bool validate(const Manifest& manifest, std::string* const diagnostics) {
    const auto fail = [&](const std::string& message) {
        if (diagnostics != nullptr) *diagnostics = message;
        return false;
    };

    if (manifest.version != project_manifest_version) {
        return fail("Project manifest version is not supported.");
    }
    if (!manifest.id) return fail("Project manifest has no stable project ID.");
    if (manifest.name.empty()) return fail("Project manifest has no display name.");
    if (manifest.game_name.empty()) return fail("Project manifest has no game display name.");
    if (!valid_relative_path(manifest.asset_root)) {
        return fail("Project asset root must be a portable relative path without traversal.");
    }
    if (!valid_relative_path(manifest.startup_world)) {
        return fail("Project startup World must be a portable relative path without traversal.");
    }
    if (manifest.startup_world.extension() != ".vworld") {
        return fail("Project startup World must reference an authored .vworld asset.");
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

LoadResult parse_manifest(const std::string_view text) {
    LoadResult result;
    std::istringstream input{std::string{text}};
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;
    bool id_seen = false;
    bool name_seen = false;
    bool game_name_seen = false;
    bool assets_seen = false;
    bool startup_seen = false;
    Manifest manifest;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Project manifest line " + std::to_string(line_number) + ": " + message;
        return result;
    };

    while (std::getline(input, line)) {
        ++line_number;
        const auto first = std::find_if_not(
            line.begin(), line.end(), [](const unsigned char value) { return std::isspace(value) != 0; }
        );
        if (first == line.end() || *first == '#') continue;

        std::istringstream stream{std::string{first, line.end()}};
        std::string key;
        stream >> key;

        if (!header_seen) {
            if (key != "vice-project") return fail("expected `vice-project <version>` header.");
            std::uint32_t version = 0;
            if (!(stream >> version)) return fail("project version is missing.");
            stream >> std::ws;
            if (!stream.eof()) return fail("project header contains trailing data.");
            manifest.version = version;
            header_seen = true;
            continue;
        }

        if (key == "id") {
            if (id_seen) return fail("duplicate `id` field.");
            std::string value;
            if (!(stream >> value)) return fail("project ID is missing.");
            stream >> std::ws;
            if (!stream.eof()) return fail("project ID contains trailing data.");
            const auto parsed = ProjectId::parse(value);
            if (!parsed.has_value()) return fail("project ID is not a valid non-zero UUID.");
            manifest.id = *parsed;
            id_seen = true;
        } else if (key == "name") {
            if (name_seen) return fail("duplicate `name` field.");
            if (!read_quoted_value(stream, manifest.name)) return fail("project name must be quoted.");
            name_seen = true;
        } else if (key == "game_name") {
            if (game_name_seen) return fail("duplicate `game_name` field.");
            if (!read_quoted_value(stream, manifest.game_name)) return fail("game display name must be quoted.");
            if (manifest.game_name.empty()) return fail("game display name must not be empty.");
            game_name_seen = true;
        } else if (key == "assets") {
            if (assets_seen) return fail("duplicate `assets` field.");
            std::string value;
            if (!read_quoted_value(stream, value)) return fail("asset root must be quoted.");
            if (value.find('\\') != std::string::npos) {
                return fail("asset root must use `/` separators.");
            }
            manifest.asset_root = std::filesystem::path{value};
            assets_seen = true;
        } else if (key == "startup_world") {
            if (startup_seen) return fail("duplicate `startup_world` field.");
            std::string value;
            if (!read_quoted_value(stream, value)) return fail("startup World must be quoted.");
            if (value.find('\\') != std::string::npos) {
                return fail("startup World must use `/` separators.");
            }
            manifest.startup_world = std::filesystem::path{value};
            startup_seen = true;
        } else {
            return fail("unknown field `" + key + "`.");
        }
    }

    if (!header_seen) {
        result.diagnostics = "Project manifest is empty.";
        return result;
    }
    if (!id_seen || !name_seen || !assets_seen || !startup_seen) {
        result.diagnostics = "Project manifest is missing one or more required fields.";
        return result;
    }
    if (!game_name_seen) manifest.game_name = manifest.name;
    if (!validate(manifest, &result.diagnostics)) return result;
    result.manifest = std::move(manifest);
    return result;
}

LoadResult load_manifest(const std::filesystem::path& path) {
    LoadResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open project manifest: " + path.generic_string();
        return result;
    }
    std::ostringstream text;
    text << input.rdbuf();
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read project manifest: " + path.generic_string();
        return result;
    }
    return parse_manifest(text.str());
}

std::string serialize_manifest(const Manifest& manifest) {
    Manifest normalized = manifest;
    if (normalized.game_name.empty() && !normalized.name.empty()) normalized.game_name = normalized.name;

    std::string diagnostics;
    if (!validate(normalized, &diagnostics)) return {};

    std::ostringstream output;
    output << "vice-project " << normalized.version << '\n';
    output << "id " << normalized.id.to_string() << '\n';
    output << "name " << std::quoted(normalized.name) << '\n';
    output << "game_name " << std::quoted(normalized.game_name) << '\n';
    output << "assets " << std::quoted(portable_path(normalized.asset_root)) << '\n';
    output << "startup_world " << std::quoted(portable_path(normalized.startup_world)) << '\n';
    return output.str();
}

bool save_manifest(
    const std::filesystem::path& path,
    const Manifest& manifest,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };

    if (path.empty()) return fail("Project manifest save path is empty.");
    std::string validation;
    if (!validate(manifest, &validation)) return fail(validation);
    const std::string text = serialize_manifest(manifest);
    if (text.empty()) return fail("Project manifest could not be serialized.");

    std::error_code error;
    const auto parent = path.parent_path();
    if (!parent.empty() && !std::filesystem::is_directory(parent, error)) {
        if (error) return fail("Could not inspect project manifest directory: " + error.message());
        return fail("Project manifest directory does not exist: " + parent.generic_string());
    }

    auto temporary = path;
    temporary += ".tmp";
    auto backup = path;
    backup += ".bak";
    std::filesystem::remove(temporary, error);
    error.clear();
    std::filesystem::remove(backup, error);
    error.clear();

    std::string write_diagnostics;
    if (!write_text_file(temporary, text, write_diagnostics)) {
        std::filesystem::remove(temporary, error);
        return fail(std::move(write_diagnostics));
    }

    const bool had_original = std::filesystem::exists(path, error);
    if (error) {
        const std::string inspect_error = error.message();
        error.clear();
        std::filesystem::remove(temporary, error);
        return fail("Could not inspect existing project manifest: " + inspect_error);
    }

    if (had_original) {
        std::filesystem::rename(path, backup, error);
        if (error) {
            const std::string stage_error = error.message();
            error.clear();
            std::filesystem::remove(temporary, error);
            return fail("Could not stage existing project manifest for replacement: " + stage_error);
        }
    }

    std::filesystem::rename(temporary, path, error);
    if (error) {
        const std::string rename_error = error.message();
        if (had_original) {
            std::error_code rollback_error;
            std::filesystem::rename(backup, path, rollback_error);
            if (rollback_error) {
                return fail(
                    "Could not replace project manifest: " + rename_error +
                    ". Rollback also failed: " + rollback_error.message()
                );
            }
        }
        error.clear();
        std::filesystem::remove(temporary, error);
        return fail("Could not replace project manifest: " + rename_error);
    }

    if (had_original) {
        error.clear();
        std::filesystem::remove(backup, error);
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

std::filesystem::path resolve_asset_root(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
) {
    if (!valid_relative_path(manifest.asset_root)) return {};
    return (manifest_path.parent_path() / manifest.asset_root).lexically_normal();
}

std::filesystem::path resolve_startup_world(
    const std::filesystem::path& manifest_path,
    const Manifest& manifest
) {
    if (!valid_relative_path(manifest.startup_world)) return {};
    const auto assets = resolve_asset_root(manifest_path, manifest);
    if (assets.empty()) return {};
    return (assets / manifest.startup_world).lexically_normal();
}

std::filesystem::path resolve_cooked_root(
    const std::filesystem::path& cooked_cache_base,
    const Manifest& manifest
) {
    if (cooked_cache_base.empty() || !manifest.id) return {};
    return (cooked_cache_base / manifest.id.to_string()).lexically_normal();
}

std::filesystem::path resolve_cooked_startup_world(
    const std::filesystem::path& cooked_cache_base,
    const Manifest& manifest
) {
    if (!valid_relative_path(manifest.startup_world)) return {};
    const auto cooked_root = resolve_cooked_root(cooked_cache_base, manifest);
    if (cooked_root.empty()) return {};
    auto cooked_startup = manifest.startup_world;
    cooked_startup.replace_extension(".vworldc");
    return (cooked_root / cooked_startup).lexically_normal();
}

} // namespace vice::project

#include <vice/project/project.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iomanip>
#include <sstream>

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
    std::string diagnostics;
    if (!validate(manifest, &diagnostics)) return {};

    std::ostringstream output;
    output << "vice-project " << manifest.version << '\n';
    output << "id " << manifest.id.to_string() << '\n';
    output << "name " << std::quoted(manifest.name) << '\n';
    output << "assets " << std::quoted(portable_path(manifest.asset_root)) << '\n';
    output << "startup_world " << std::quoted(portable_path(manifest.startup_world)) << '\n';
    return output.str();
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

} // namespace vice::project

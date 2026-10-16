#include <vice/tools/rigid_material_cooker.hpp>

#include <vice/rigid_body/material_asset.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace vice::tools::rigid_material_cooker {
namespace {

constexpr std::array<std::byte, 8> material_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'R'}, std::byte{'I'}, std::byte{'G'}, std::byte{'M'},
};
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

enum class PropertyTag : std::uint8_t {
    boolean = 1,
    integer = 2,
    real = 3,
    string = 4,
};

struct SourceMaterial {
    rigid_body::RigidMaterialStableId stable_id{};
    simulation::MaterialId runtime_id{};
    std::string name;
    simulation::Material material;
};

struct SourceInteraction {
    rigid_body::RigidMaterialStableId first{};
    rigid_body::RigidMaterialStableId second{};
    rigid_body::MaterialInteraction interaction;
};

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] int hex_value(const char value) noexcept {
    if (value >= '0' && value <= '9') return value - '0';
    if (value >= 'a' && value <= 'f') return value - 'a' + 10;
    if (value >= 'A' && value <= 'F') return value - 'A' + 10;
    return -1;
}

[[nodiscard]] std::optional<rigid_body::RigidMaterialStableId> parse_stable_id(
    const std::string_view text
) noexcept {
    if (text.size() != 36U || text[8] != '-' || text[13] != '-' ||
        text[18] != '-' || text[23] != '-') {
        return std::nullopt;
    }

    rigid_body::RigidMaterialStableId id{};
    std::size_t output = 0;
    for (std::size_t index = 0; index < text.size();) {
        if (text[index] == '-') {
            ++index;
            continue;
        }
        if (index + 1U >= text.size() || output >= id.size()) return std::nullopt;
        const int high = hex_value(text[index]);
        const int low = hex_value(text[index + 1U]);
        if (high < 0 || low < 0) return std::nullopt;
        id[output++] = static_cast<std::uint8_t>((high << 4) | low);
        index += 2U;
    }
    if (output != id.size() || std::all_of(id.begin(), id.end(), [](const std::uint8_t value) {
            return value == 0U;
        })) {
        return std::nullopt;
    }
    return id;
}

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

void append_u8(std::vector<std::byte>& bytes, const std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

void append_u32(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

void append_u64(std::vector<std::byte>& bytes, const std::uint64_t value) {
    for (std::size_t index = 0; index < sizeof(value); ++index) {
        bytes.push_back(static_cast<std::byte>((value >> (index * 8U)) & 0xffU));
    }
}

void append_i64(std::vector<std::byte>& bytes, const std::int64_t value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_f64(std::vector<std::byte>& bytes, const double value) {
    append_u64(bytes, std::bit_cast<std::uint64_t>(value));
}

void append_f32(std::vector<std::byte>& bytes, const float value) {
    append_u32(bytes, std::bit_cast<std::uint32_t>(value));
}

void append_string(std::vector<std::byte>& bytes, const std::string_view value) {
    append_u32(bytes, static_cast<std::uint32_t>(value.size()));
    for (const char character : value) {
        bytes.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
    }
}

void append_stable_id(
    std::vector<std::byte>& bytes,
    const rigid_body::RigidMaterialStableId& id
) {
    for (const std::uint8_t value : id) bytes.push_back(static_cast<std::byte>(value));
}

[[nodiscard]] SourceMaterial* find_material(
    std::vector<SourceMaterial>& materials,
    const rigid_body::RigidMaterialStableId& id
) noexcept {
    const auto found = std::find_if(
        materials.begin(),
        materials.end(),
        [&id](const SourceMaterial& material) { return material.stable_id == id; }
    );
    return found != materials.end() ? &*found : nullptr;
}

[[nodiscard]] const SourceMaterial* find_material(
    const std::vector<SourceMaterial>& materials,
    const rigid_body::RigidMaterialStableId& id
) noexcept {
    const auto found = std::find_if(
        materials.begin(),
        materials.end(),
        [&id](const SourceMaterial& material) { return material.stable_id == id; }
    );
    return found != materials.end() ? &*found : nullptr;
}

[[nodiscard]] bool parse_optional_float(
    const std::string& text,
    std::optional<float>& value
) noexcept {
    if (text == "none") {
        value.reset();
        return true;
    }
    float parsed = 0.0F;
    const auto* first = text.data();
    const auto* last = first + text.size();
    const auto conversion = std::from_chars(first, last, parsed);
    if (conversion.ec != std::errc{} || conversion.ptr != last || !std::isfinite(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

[[nodiscard]] std::vector<std::byte> encode(
    std::vector<SourceMaterial> materials,
    std::vector<SourceInteraction> interactions
) {
    std::sort(
        materials.begin(),
        materials.end(),
        [](const SourceMaterial& left, const SourceMaterial& right) {
            return left.stable_id < right.stable_id;
        }
    );
    std::sort(
        interactions.begin(),
        interactions.end(),
        [](const SourceInteraction& left, const SourceInteraction& right) {
            if (left.first != right.first) return left.first < right.first;
            return left.second < right.second;
        }
    );

    std::vector<std::byte> payload;
    for (const auto& material : materials) {
        append_stable_id(payload, material.stable_id);
        append_u64(payload, material.runtime_id.value);
        append_string(payload, material.name);

        std::vector<const simulation::MaterialProperty*> properties;
        properties.reserve(material.material.properties().size());
        for (const auto& property : material.material.properties()) properties.push_back(&property);
        std::sort(
            properties.begin(),
            properties.end(),
            [](const auto* left, const auto* right) {
                if (left->owner != right->owner) return left->owner < right->owner;
                return left->name < right->name;
            }
        );
        append_u32(payload, static_cast<std::uint32_t>(properties.size()));
        for (const auto* property : properties) {
            append_string(payload, property->owner);
            append_string(payload, property->name);
            if (std::holds_alternative<bool>(property->value)) {
                append_u8(payload, static_cast<std::uint8_t>(PropertyTag::boolean));
                append_u8(payload, std::get<bool>(property->value) ? 1U : 0U);
            } else if (std::holds_alternative<std::int64_t>(property->value)) {
                append_u8(payload, static_cast<std::uint8_t>(PropertyTag::integer));
                append_i64(payload, std::get<std::int64_t>(property->value));
            } else if (std::holds_alternative<double>(property->value)) {
                append_u8(payload, static_cast<std::uint8_t>(PropertyTag::real));
                append_f64(payload, std::get<double>(property->value));
            } else {
                append_u8(payload, static_cast<std::uint8_t>(PropertyTag::string));
                append_string(payload, std::get<std::string>(property->value));
            }
        }
    }

    for (const auto& interaction : interactions) {
        const auto* first = find_material(materials, interaction.first);
        const auto* second = find_material(materials, interaction.second);
        if (first == nullptr || second == nullptr) return {};
        append_u64(payload, first->runtime_id.value);
        append_u64(payload, second->runtime_id.value);
        std::uint8_t flags = 0;
        if (interaction.interaction.friction.has_value()) flags |= 1U;
        if (interaction.interaction.restitution.has_value()) flags |= 2U;
        append_u8(payload, flags);
        if (interaction.interaction.friction.has_value()) {
            append_f32(payload, *interaction.interaction.friction);
        }
        if (interaction.interaction.restitution.has_value()) {
            append_f32(payload, *interaction.interaction.restitution);
        }
        append_string(payload, interaction.interaction.label);
    }

    std::vector<std::byte> bytes;
    bytes.insert(bytes.end(), material_magic.begin(), material_magic.end());
    append_u32(bytes, rigid_body::rigid_material_asset_format_version);
    append_u32(bytes, static_cast<std::uint32_t>(materials.size()));
    append_u32(bytes, static_cast<std::uint32_t>(interactions.size()));
    append_u64(bytes, fingerprint(payload));
    bytes.insert(bytes.end(), payload.begin(), payload.end());
    return bytes;
}

} // namespace

CookResult cook_source_manifest(const std::string_view source) {
    CookResult result;
    std::vector<SourceMaterial> materials;
    std::vector<SourceInteraction> interactions;
    std::istringstream stream{std::string{source}};
    std::string line;
    std::size_t line_number = 0;
    bool header_seen = false;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Rigid material source line " + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(stream, line)) {
        ++line_number;
        if (const auto comment = line.find('#'); comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream values{line};
        std::string keyword;
        values >> keyword;
        if (!header_seen) {
            std::uint32_t version = 0;
            if (keyword != "vice-rigid-material-source" || !(values >> version) || version != 1U) {
                fail("expected `vice-rigid-material-source 1`.");
                return result;
            }
            std::string trailing;
            if (values >> trailing) {
                fail("source header has trailing data.");
                return result;
            }
            header_seen = true;
            continue;
        }

        if (keyword == "material") {
            std::string id_text;
            std::uint64_t runtime_id = 0;
            std::string name;
            if (!(values >> id_text >> runtime_id >> std::quoted(name))) {
                fail("invalid material declaration.");
                return result;
            }
            const auto id = parse_stable_id(id_text);
            if (!id.has_value() || runtime_id == 0U || name.empty() ||
                find_material(materials, *id) != nullptr ||
                std::any_of(materials.begin(), materials.end(), [runtime_id](const SourceMaterial& item) {
                    return item.runtime_id.value == runtime_id;
                })) {
                fail("material identity is invalid or duplicated.");
                return result;
            }
            materials.push_back(SourceMaterial{*id, simulation::MaterialId{runtime_id}, std::move(name), {}});
        } else if (keyword == "property") {
            std::string id_text;
            std::string owner;
            std::string name;
            std::string type;
            if (!(values >> id_text >> std::quoted(owner) >> std::quoted(name) >> type)) {
                fail("invalid property declaration.");
                return result;
            }
            const auto id = parse_stable_id(id_text);
            auto* material = id.has_value() ? find_material(materials, *id) : nullptr;
            if (material == nullptr || owner.empty() || name.empty() ||
                material->material.find_property(owner, name) != nullptr) {
                fail("property material, owner, or name is invalid.");
                return result;
            }

            simulation::MaterialPropertyValue property_value;
            if (type == "bool") {
                std::string raw;
                if (!(values >> raw) || (raw != "true" && raw != "false")) {
                    fail("boolean property must be `true` or `false`.");
                    return result;
                }
                property_value = raw == "true";
            } else if (type == "int") {
                std::int64_t raw = 0;
                if (!(values >> raw)) {
                    fail("integer property is invalid.");
                    return result;
                }
                property_value = raw;
            } else if (type == "real") {
                double raw = 0.0;
                if (!(values >> raw) || !std::isfinite(raw)) {
                    fail("real property is invalid.");
                    return result;
                }
                property_value = raw;
            } else if (type == "string") {
                std::string raw;
                if (!(values >> std::quoted(raw))) {
                    fail("string property is invalid.");
                    return result;
                }
                property_value = std::move(raw);
            } else {
                fail("property type must be `bool`, `int`, `real`, or `string`.");
                return result;
            }
            if (!material->material.set_property(
                    std::move(owner),
                    std::move(name),
                    std::move(property_value)
                )) {
                fail("property could not be stored.");
                return result;
            }
        } else if (keyword == "interaction") {
            std::string first_text;
            std::string second_text;
            std::string friction_text;
            std::string restitution_text;
            std::string label;
            if (!(values >> first_text >> second_text >> friction_text >> restitution_text >>
                    std::quoted(label))) {
                fail("invalid interaction declaration.");
                return result;
            }
            const auto first = parse_stable_id(first_text);
            const auto second = parse_stable_id(second_text);
            if (!first.has_value() || !second.has_value() ||
                find_material(materials, *first) == nullptr ||
                find_material(materials, *second) == nullptr) {
                fail("interaction references an unknown material.");
                return result;
            }
            const auto pair_exists = std::any_of(
                interactions.begin(),
                interactions.end(),
                [&](const SourceInteraction& item) {
                    return (item.first == *first && item.second == *second) ||
                        (item.first == *second && item.second == *first);
                }
            );
            if (pair_exists) {
                fail("interaction pair is duplicated.");
                return result;
            }
            SourceInteraction interaction;
            interaction.first = *first;
            interaction.second = *second;
            if (!parse_optional_float(friction_text, interaction.interaction.friction) ||
                !parse_optional_float(restitution_text, interaction.interaction.restitution) ||
                (interaction.interaction.friction.has_value() && *interaction.interaction.friction < 0.0F) ||
                (interaction.interaction.restitution.has_value() &&
                    (*interaction.interaction.restitution < 0.0F ||
                     *interaction.interaction.restitution > 1.0F))) {
                fail("interaction friction or restitution is invalid.");
                return result;
            }
            interaction.interaction.label = std::move(label);
            interactions.push_back(std::move(interaction));
        } else {
            fail("unknown keyword `" + keyword + "`.");
            return result;
        }

        std::string trailing;
        if (values >> trailing) {
            fail("statement has trailing data.");
            return result;
        }
    }

    if (!header_seen) {
        result.diagnostics = "Rigid material source is missing its header.";
        return result;
    }
    if (materials.empty()) {
        result.diagnostics = "Rigid material source contains no materials.";
        return result;
    }

    result.bytes = encode(std::move(materials), std::move(interactions));
    if (result.bytes.empty()) {
        result.diagnostics = "Rigid material source could not be encoded.";
        return result;
    }
    const auto decoded = rigid_body::decode_rigid_material_asset(result.bytes);
    if (!decoded.asset.has_value()) {
        result.bytes.clear();
        result.diagnostics = "Cooked rigid material validation failed: " + decoded.diagnostics;
    }
    return result;
}

} // namespace vice::tools::rigid_material_cooker

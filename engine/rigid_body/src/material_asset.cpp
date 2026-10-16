#include <vice/rigid_body/material_asset.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::rigid_body {
namespace {

constexpr std::array<std::byte, 8> material_magic{
    std::byte{'V'}, std::byte{'I'}, std::byte{'C'}, std::byte{'E'},
    std::byte{'R'}, std::byte{'I'}, std::byte{'G'}, std::byte{'M'},
};
constexpr std::size_t header_size = material_magic.size() + sizeof(std::uint32_t) * 3U +
    sizeof(std::uint64_t);
constexpr std::uint32_t max_materials = 4096U;
constexpr std::uint32_t max_interactions = 16384U;
constexpr std::uint32_t max_properties_per_material = 256U;
constexpr std::uint32_t max_string_bytes = 4096U;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;

enum class PropertyTag : std::uint8_t {
    boolean = 1,
    integer = 2,
    real = 3,
    string = 4,
};

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> bytes) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const std::byte value : bytes) {
        hash ^= static_cast<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] bool stable_id_is_nil(const RigidMaterialStableId& id) noexcept {
    return std::all_of(id.begin(), id.end(), [](const std::uint8_t value) {
        return value == 0U;
    });
}

class Reader final {
public:
    explicit Reader(const std::span<const std::byte> bytes) noexcept : bytes_{bytes} {}

    bool read_u8(std::uint8_t& value) noexcept {
        if (remaining() < 1U) return false;
        value = std::to_integer<std::uint8_t>(bytes_[position_]);
        ++position_;
        return true;
    }

    bool read_u32(std::uint32_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint32_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_u64(std::uint64_t& value) noexcept {
        if (remaining() < sizeof(value)) return false;
        value = 0;
        for (std::size_t index = 0; index < sizeof(value); ++index) {
            value |= static_cast<std::uint64_t>(
                std::to_integer<std::uint8_t>(bytes_[position_ + index])
            ) << (index * 8U);
        }
        position_ += sizeof(value);
        return true;
    }

    bool read_i64(std::int64_t& value) noexcept {
        std::uint64_t bits = 0;
        if (!read_u64(bits)) return false;
        value = std::bit_cast<std::int64_t>(bits);
        return true;
    }

    bool read_f64(double& value) noexcept {
        std::uint64_t bits = 0;
        if (!read_u64(bits)) return false;
        value = std::bit_cast<double>(bits);
        return std::isfinite(value);
    }

    bool read_f32(float& value) noexcept {
        std::uint32_t bits = 0;
        if (!read_u32(bits)) return false;
        value = std::bit_cast<float>(bits);
        return std::isfinite(value);
    }

    bool read_stable_id(RigidMaterialStableId& id) noexcept {
        if (remaining() < id.size()) return false;
        for (std::size_t index = 0; index < id.size(); ++index) {
            id[index] = std::to_integer<std::uint8_t>(bytes_[position_ + index]);
        }
        position_ += id.size();
        return true;
    }

    bool read_string(std::string& value) {
        std::uint32_t size = 0;
        if (!read_u32(size) || size > max_string_bytes || remaining() < size) return false;
        value.assign(
            reinterpret_cast<const char*>(bytes_.data() + position_),
            static_cast<std::size_t>(size)
        );
        position_ += size;
        return true;
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

private:
    std::span<const std::byte> bytes_;
    std::size_t position_ = 0;
};

struct InteractionKey {
    std::uint64_t first = 0;
    std::uint64_t second = 0;

    constexpr auto operator<=>(const InteractionKey&) const noexcept = default;
};

[[nodiscard]] InteractionKey interaction_key(
    const simulation::MaterialId first,
    const simulation::MaterialId second
) noexcept {
    return first.value <= second.value
        ? InteractionKey{first.value, second.value}
        : InteractionKey{second.value, first.value};
}

} // namespace

RigidMaterialAssetDecodeResult decode_rigid_material_asset(
    const std::span<const std::byte> bytes
) {
    RigidMaterialAssetDecodeResult result;
    if (bytes.size() < header_size) {
        result.diagnostics = "Rigid material asset is smaller than the VICERIGM header.";
        return result;
    }
    if (!std::equal(material_magic.begin(), material_magic.end(), bytes.begin())) {
        result.diagnostics = "Rigid material asset magic is invalid.";
        return result;
    }

    Reader reader{bytes.subspan(material_magic.size())};
    std::uint32_t version = 0;
    std::uint32_t material_count = 0;
    std::uint32_t interaction_count = 0;
    std::uint64_t stored_fingerprint = 0;
    if (!reader.read_u32(version) || !reader.read_u32(material_count) ||
        !reader.read_u32(interaction_count) || !reader.read_u64(stored_fingerprint)) {
        result.diagnostics = "Rigid material asset header is incomplete.";
        return result;
    }
    if (version != rigid_material_asset_format_version) {
        result.diagnostics = "Rigid material asset format version is not supported.";
        return result;
    }
    if (material_count == 0U || material_count > max_materials ||
        interaction_count > max_interactions) {
        result.diagnostics = "Rigid material asset has invalid material or interaction counts.";
        return result;
    }
    if (fingerprint(bytes.subspan(header_size)) != stored_fingerprint) {
        result.diagnostics = "Rigid material asset payload fingerprint does not match.";
        return result;
    }

    RigidMaterialAsset asset;
    asset.version = version;
    asset.source_fingerprint = stored_fingerprint;
    asset.materials.reserve(material_count);
    asset.interactions.reserve(interaction_count);

    std::set<RigidMaterialStableId> stable_ids;
    std::set<std::uint64_t> runtime_ids;
    for (std::uint32_t index = 0; index < material_count; ++index) {
        RigidMaterialDefinition definition;
        std::uint64_t runtime_id = 0;
        std::uint32_t property_count = 0;
        if (!reader.read_stable_id(definition.stable_id) ||
            !reader.read_u64(runtime_id) ||
            !reader.read_string(definition.name) ||
            !reader.read_u32(property_count)) {
            result.diagnostics = "Rigid material definition is incomplete.";
            return result;
        }
        if (stable_id_is_nil(definition.stable_id) || runtime_id == 0U ||
            definition.name.empty() || property_count > max_properties_per_material ||
            !stable_ids.insert(definition.stable_id).second ||
            !runtime_ids.insert(runtime_id).second) {
            result.diagnostics = "Rigid material definition has invalid or duplicate identity.";
            return result;
        }
        definition.runtime_id = simulation::MaterialId{runtime_id};

        for (std::uint32_t property_index = 0; property_index < property_count; ++property_index) {
            std::string owner;
            std::string name;
            std::uint8_t raw_tag = 0;
            if (!reader.read_string(owner) || !reader.read_string(name) ||
                !reader.read_u8(raw_tag) || owner.empty() || name.empty() ||
                definition.material.find_property(owner, name) != nullptr) {
                result.diagnostics = "Rigid material property is invalid or duplicated.";
                return result;
            }

            simulation::MaterialPropertyValue value;
            const auto tag = static_cast<PropertyTag>(raw_tag);
            if (tag == PropertyTag::boolean) {
                std::uint8_t raw = 0;
                if (!reader.read_u8(raw) || raw > 1U) {
                    result.diagnostics = "Rigid material boolean property is invalid.";
                    return result;
                }
                value = raw != 0U;
            } else if (tag == PropertyTag::integer) {
                std::int64_t raw = 0;
                if (!reader.read_i64(raw)) {
                    result.diagnostics = "Rigid material integer property is incomplete.";
                    return result;
                }
                value = raw;
            } else if (tag == PropertyTag::real) {
                double raw = 0.0;
                if (!reader.read_f64(raw)) {
                    result.diagnostics = "Rigid material real property is invalid.";
                    return result;
                }
                value = raw;
            } else if (tag == PropertyTag::string) {
                std::string raw;
                if (!reader.read_string(raw)) {
                    result.diagnostics = "Rigid material string property is invalid.";
                    return result;
                }
                value = std::move(raw);
            } else {
                result.diagnostics = "Rigid material property type is not supported.";
                return result;
            }

            if (!definition.material.set_property(
                    std::move(owner),
                    std::move(name),
                    std::move(value)
                )) {
                result.diagnostics = "Rigid material property could not be registered.";
                return result;
            }
        }

        asset.materials.push_back(std::move(definition));
    }

    std::set<InteractionKey> interaction_keys;
    for (std::uint32_t index = 0; index < interaction_count; ++index) {
        RigidMaterialInteractionDefinition definition;
        std::uint64_t first = 0;
        std::uint64_t second = 0;
        std::uint8_t flags = 0;
        if (!reader.read_u64(first) || !reader.read_u64(second) || !reader.read_u8(flags) ||
            first == 0U || second == 0U || flags > 3U ||
            !runtime_ids.contains(first) || !runtime_ids.contains(second)) {
            result.diagnostics = "Rigid material interaction has invalid material identity.";
            return result;
        }
        definition.first = simulation::MaterialId{first};
        definition.second = simulation::MaterialId{second};
        if (!interaction_keys.insert(interaction_key(definition.first, definition.second)).second) {
            result.diagnostics = "Rigid material interaction pair is duplicated.";
            return result;
        }

        if ((flags & 1U) != 0U) {
            float friction = 0.0F;
            if (!reader.read_f32(friction) || friction < 0.0F) {
                result.diagnostics = "Rigid material interaction friction is invalid.";
                return result;
            }
            definition.interaction.friction = friction;
        }
        if ((flags & 2U) != 0U) {
            float restitution = 0.0F;
            if (!reader.read_f32(restitution) || restitution < 0.0F || restitution > 1.0F) {
                result.diagnostics = "Rigid material interaction restitution is invalid.";
                return result;
            }
            definition.interaction.restitution = restitution;
        }
        if (!reader.read_string(definition.interaction.label)) {
            result.diagnostics = "Rigid material interaction label is invalid.";
            return result;
        }
        asset.interactions.push_back(std::move(definition));
    }

    if (reader.remaining() != 0U) {
        result.diagnostics = "Rigid material asset has trailing payload data.";
        return result;
    }

    result.asset = std::move(asset);
    return result;
}

RigidMaterialAssetDecodeResult load_rigid_material_asset(const std::filesystem::path& path) {
    RigidMaterialAssetDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked rigid material asset: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked rigid material asset: " + path.string();
        return result;
    }
    return decode_rigid_material_asset(std::as_bytes(std::span<const char>{raw}));
}

const RigidMaterialDefinition* find_rigid_material(
    const RigidMaterialAsset& asset,
    const RigidMaterialStableId& stable_id
) noexcept {
    const auto found = std::find_if(
        asset.materials.begin(),
        asset.materials.end(),
        [&stable_id](const RigidMaterialDefinition& definition) {
            return definition.stable_id == stable_id;
        }
    );
    return found != asset.materials.end() ? &*found : nullptr;
}

bool register_rigid_material_asset(
    Domain& domain,
    const RigidMaterialAsset& asset,
    std::string* diagnostics
) {
    std::vector<simulation::MaterialId> registered_materials;
    std::vector<std::pair<simulation::MaterialId, simulation::MaterialId>> registered_interactions;
    registered_materials.reserve(asset.materials.size());
    registered_interactions.reserve(asset.interactions.size());

    const auto fail = [&](const std::string& message) {
        for (auto it = registered_interactions.rbegin(); it != registered_interactions.rend(); ++it) {
            (void)domain.remove_material_interaction(it->first, it->second);
        }
        for (auto it = registered_materials.rbegin(); it != registered_materials.rend(); ++it) {
            (void)domain.unregister_material(*it);
        }
        if (diagnostics != nullptr) *diagnostics = message;
        return false;
    };

    for (const auto& definition : asset.materials) {
        if (!domain.register_material(definition.runtime_id, definition.material)) {
            return fail("Could not register rigid material `" + definition.name + "`.");
        }
        registered_materials.push_back(definition.runtime_id);
    }

    for (const auto& definition : asset.interactions) {
        if (!domain.set_material_interaction(
                definition.first,
                definition.second,
                definition.interaction
            )) {
            return fail("Could not register rigid material interaction `" +
                definition.interaction.label + "`.");
        }
        registered_interactions.emplace_back(definition.first, definition.second);
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::rigid_body

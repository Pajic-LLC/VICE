#include <vice/editor/asset_index.hpp>

#include <vice/render/material_asset.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/render/texture_asset.hpp>
#include <vice/rigid_body/collision_asset.hpp>
#include <vice/rigid_body/material_asset.hpp>
#include <vice/world/snapshot_file.hpp>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <sstream>
#include <string>
#include <unordered_set>

namespace vice::editor {
namespace {

[[nodiscard]] AssetKind classify_source(const std::filesystem::path& path) noexcept {
    const auto extension = path.extension().string();
    if (extension == ".vworld") return AssetKind::world;
    if (extension == ".vmesh") return AssetKind::mesh;
    if (extension == ".vmat") return AssetKind::render_material;
    if (extension == ".vtex") return AssetKind::texture;
    if (extension == ".vcollision") return AssetKind::collision;
    if (extension == ".vrigmat") return AssetKind::rigid_material;
    if (extension == ".hlsl") return AssetKind::shader_source;
    return AssetKind::unknown;
}

[[nodiscard]] AssetKind classify_cooked(const std::filesystem::path& path) noexcept {
    const auto extension = path.extension().string();
    if (extension == ".vworldc") return AssetKind::world;
    if (extension == ".vmeshc") return AssetKind::mesh;
    if (extension == ".vmatc") return AssetKind::render_material;
    if (extension == ".vtexc") return AssetKind::texture;
    if (extension == ".vcollisionc") return AssetKind::collision;
    if (extension == ".vrigmatc") return AssetKind::rigid_material;
    if (path.filename().string().ends_with(".vice.json")) return AssetKind::shader_manifest;
    return AssetKind::unknown;
}

[[nodiscard]] std::filesystem::path cooked_relative_path(
    std::filesystem::path relative,
    const AssetKind kind
) {
    switch (kind) {
    case AssetKind::world: relative.replace_extension(".vworldc"); break;
    case AssetKind::mesh: relative.replace_extension(".vmeshc"); break;
    case AssetKind::render_material: relative.replace_extension(".vmatc"); break;
    case AssetKind::texture: relative.replace_extension(".vtexc"); break;
    case AssetKind::collision: relative.replace_extension(".vcollisionc"); break;
    case AssetKind::rigid_material: relative.replace_extension(".vrigmatc"); break;
    default: return {};
    }
    return relative;
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream input{path, std::ios::binary};
    if (!input) return std::nullopt;
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) return std::nullopt;
    return contents.str();
}

[[nodiscard]] std::optional<MeshBounds> mesh_bounds(const render::MeshAsset& asset) noexcept {
    if (asset.vertices.empty()) return std::nullopt;

    const auto& first = asset.vertices.front().position;
    MeshBounds bounds{
        .minimum = {first[0], first[1], first[2]},
        .maximum = {first[0], first[1], first[2]},
    };
    for (const auto& vertex : asset.vertices) {
        bounds.minimum.x = std::min(bounds.minimum.x, vertex.position[0]);
        bounds.minimum.y = std::min(bounds.minimum.y, vertex.position[1]);
        bounds.minimum.z = std::min(bounds.minimum.z, vertex.position[2]);
        bounds.maximum.x = std::max(bounds.maximum.x, vertex.position[0]);
        bounds.maximum.y = std::max(bounds.maximum.y, vertex.position[1]);
        bounds.maximum.z = std::max(bounds.maximum.z, vertex.position[2]);
    }
    return bounds;
}

void inspect_cooked_asset(AssetRecord& record) {
    record.cooked_available = !record.cooked_path.empty() && std::filesystem::is_regular_file(record.cooked_path);
    if (!record.cooked_available) {
        if (record.kind != AssetKind::shader_source) {
            record.diagnostics = "Cooked asset is not available.";
        }
        return;
    }

    switch (record.kind) {
    case AssetKind::world: {
        const auto loaded = world::load_world_snapshot(record.cooked_path);
        record.cooked_valid = loaded.snapshot.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.snapshot.has_value()) {
            record.summary = std::to_string(loaded.snapshot->entities.size()) + " entities, " +
                std::to_string(loaded.snapshot->regions.size()) + " regions";
        }
        break;
    }
    case AssetKind::mesh: {
        const auto loaded = render::load_mesh_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.mesh_bounds = mesh_bounds(*loaded.asset);
            record.summary = std::to_string(loaded.asset->vertices.size()) + " vertices, " +
                std::to_string(loaded.asset->indices.size() / 3U) + " triangles";
            if (render::valid(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
    case AssetKind::render_material: {
        const auto loaded = render::load_render_material_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = loaded.asset->version >= render::render_material_asset_format_version_v2
                ? "Textured PBR surface"
                : "Base color + project vertex/fragment shaders";
            if (render::valid_render_material_id(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
    case AssetKind::texture: {
        const auto loaded = render::load_texture_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = std::to_string(loaded.asset->width) + " x " +
                std::to_string(loaded.asset->height) +
                (loaded.asset->color_space == render::TextureColorSpace::srgb ? " sRGB" : " linear");
            if (render::valid_texture_id(loaded.asset->stable_id)) {
                record.resource_id = world::ResourceId{
                    world::PersistentId{loaded.asset->stable_id},
                };
            }
        }
        break;
    }
    case AssetKind::collision: {
        const auto loaded = rigid_body::load_collision_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = std::to_string(loaded.asset->shapes.size()) + " shapes";
        }
        break;
    }
    case AssetKind::rigid_material: {
        const auto loaded = rigid_body::load_rigid_material_asset(record.cooked_path);
        record.cooked_valid = loaded.asset.has_value();
        record.diagnostics = loaded.diagnostics;
        if (loaded.asset.has_value()) {
            record.format_version = loaded.asset->version;
            record.source_fingerprint = loaded.asset->source_fingerprint;
            record.summary = std::to_string(loaded.asset->materials.size()) + " materials, " +
                std::to_string(loaded.asset->interactions.size()) + " interactions";
        }
        break;
    }
    case AssetKind::shader_manifest: {
        const auto text = read_text(record.cooked_path);
        if (!text.has_value()) {
            record.diagnostics = "Could not read cooked shader manifest.";
            break;
        }
        const auto manifest = render::parse_cooked_shader_manifest(*text);
        record.cooked_valid = manifest.valid;
        record.diagnostics = manifest.diagnostics;
        record.format_version = manifest.version;
        if (manifest.valid) {
            record.summary = manifest.entry_point + ", " +
                std::to_string(manifest.resources.size()) + " resources";
        }
        break;
    }
    case AssetKind::shader_source:
    case AssetKind::unknown:
        break;
    }
}

[[nodiscard]] std::string normalized_key(const std::filesystem::path& path) {
    return path.lexically_normal().generic_string();
}

} // namespace

const char* asset_kind_name(const AssetKind kind) noexcept {
    switch (kind) {
    case AssetKind::world: return "World";
    case AssetKind::mesh: return "Mesh";
    case AssetKind::render_material: return "Render Material";
    case AssetKind::texture: return "Texture";
    case AssetKind::collision: return "Collision";
    case AssetKind::rigid_material: return "Rigid Material";
    case AssetKind::shader_source: return "Shader";
    case AssetKind::shader_manifest: return "Shader Manifest";
    case AssetKind::unknown: return "Unknown";
    }
    return "Unknown";
}

bool AssetIndex::refresh(
    const std::filesystem::path& source_root,
    const std::filesystem::path& cooked_root
) {
    source_root_ = source_root;
    cooked_root_ = cooked_root;
    assets_.clear();
    diagnostics_.clear();

    std::error_code error;
    if (source_root_.empty() || !std::filesystem::is_directory(source_root_, error)) {
        diagnostics_ = error
            ? "Could not inspect authored asset root: " + error.message()
            : "Authored asset root is not available.";
        return false;
    }

    std::unordered_set<std::string> paired_cooked;
    std::filesystem::recursive_directory_iterator iterator{
        source_root_,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics_ = "Could not enumerate authored assets: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate authored assets: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file()) continue;
        const AssetKind kind = classify_source(iterator->path());
        if (kind == AssetKind::unknown) continue;

        const auto relative = std::filesystem::relative(iterator->path(), source_root_, error);
        if (error) {
            diagnostics_ = "Could not resolve authored asset path: " + error.message();
            return false;
        }

        AssetRecord record;
        record.kind = kind;
        record.name = iterator->path().stem().string();
        record.relative_path = relative;
        record.source_path = iterator->path();
        if (const auto cooked_relative = cooked_relative_path(relative, kind); !cooked_relative.empty()) {
            record.cooked_path = cooked_root_ / cooked_relative;
            if (std::filesystem::is_regular_file(record.cooked_path, error) && !error) {
                paired_cooked.insert(normalized_key(record.cooked_path));
            }
            error.clear();
        }
        inspect_cooked_asset(record);
        assets_.push_back(std::move(record));
    }

    error.clear();
    if (!cooked_root_.empty() && std::filesystem::is_directory(cooked_root_, error)) {
        std::filesystem::recursive_directory_iterator cooked_iterator{
            cooked_root_,
            std::filesystem::directory_options::skip_permission_denied,
            error,
        };
        if (!error) {
            for (; cooked_iterator != end; cooked_iterator.increment(error)) {
                if (error) break;
                if (!cooked_iterator->is_regular_file()) continue;
                if (paired_cooked.contains(normalized_key(cooked_iterator->path()))) continue;
                const AssetKind kind = classify_cooked(cooked_iterator->path());
                if (kind == AssetKind::unknown) continue;

                AssetRecord record;
                record.kind = kind;
                record.name = cooked_iterator->path().stem().string();
                record.relative_path = std::filesystem::relative(cooked_iterator->path(), cooked_root_, error);
                if (error) break;
                record.cooked_path = cooked_iterator->path();
                inspect_cooked_asset(record);
                assets_.push_back(std::move(record));
            }
        }
        if (error) {
            diagnostics_ = "Could not enumerate cooked assets: " + error.message();
        }
    }

    std::sort(assets_.begin(), assets_.end(), [](const AssetRecord& left, const AssetRecord& right) {
        if (left.kind != right.kind) {
            return static_cast<std::uint8_t>(left.kind) < static_cast<std::uint8_t>(right.kind);
        }
        return left.relative_path.generic_string() < right.relative_path.generic_string();
    });
    return diagnostics_.empty();
}

std::span<const AssetRecord> AssetIndex::assets() const noexcept {
    return assets_;
}

const AssetRecord* AssetIndex::find_resource(const world::ResourceId resource_id) const noexcept {
    const auto found = std::find_if(
        assets_.begin(), assets_.end(),
        [resource_id](const AssetRecord& record) {
            return record.resource_id.has_value() && *record.resource_id == resource_id;
        }
    );
    return found != assets_.end() ? &*found : nullptr;
}

const std::filesystem::path& AssetIndex::source_root() const noexcept {
    return source_root_;
}

const std::filesystem::path& AssetIndex::cooked_root() const noexcept {
    return cooked_root_;
}

const std::string& AssetIndex::diagnostics() const noexcept {
    return diagnostics_;
}

} // namespace vice::editor

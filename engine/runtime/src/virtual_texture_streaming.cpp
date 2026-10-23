#include <vice/runtime/virtual_texture_streaming.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <span>
#include <tuple>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

[[nodiscard]] bool key_less(
    const RuntimeVirtualTextureTileKey& left,
    const RuntimeVirtualTextureTileKey& right
) noexcept {
    const auto left_bytes = left.texture.value.bytes();
    const auto right_bytes = right.texture.value.bytes();
    if (std::lexicographical_compare(left_bytes.begin(), left_bytes.end(), right_bytes.begin(), right_bytes.end())) {
        return true;
    }
    if (std::lexicographical_compare(right_bytes.begin(), right_bytes.end(), left_bytes.begin(), left_bytes.end())) {
        return false;
    }
    return std::tie(left.mip_level, left.tile_y, left.tile_x) <
        std::tie(right.mip_level, right.tile_y, right.tile_x);
}

[[nodiscard]] bool contains_key(
    const std::span<const RuntimeVirtualTextureTileKey> keys,
    const RuntimeVirtualTextureTileKey& key
) noexcept {
    return std::binary_search(keys.begin(), keys.end(), key, key_less);
}

[[nodiscard]] std::filesystem::path archive_path_for(const CookedTextureRecord& record) {
    auto path = record.path;
    path.replace_extension(".vtilec");
    return path;
}

[[nodiscard]] const render::VirtualTextureArchiveTile* find_descriptor(
    const render::VirtualTextureArchiveDirectory& directory,
    const RuntimeVirtualTextureTileKey& key
) noexcept {
    const render::VirtualTextureArchiveTile lookup{
        .mip_level = key.mip_level,
        .tile_x = key.tile_x,
        .tile_y = key.tile_y,
    };
    const auto less = [](const auto& left, const auto& right) {
        return std::tie(left.mip_level, left.tile_y, left.tile_x) <
            std::tie(right.mip_level, right.tile_y, right.tile_x);
    };
    const auto found = std::lower_bound(directory.tiles.begin(), directory.tiles.end(), lookup, less);
    if (found == directory.tiles.end() || found->mip_level != key.mip_level ||
        found->tile_x != key.tile_x || found->tile_y != key.tile_y) {
        return nullptr;
    }
    return &*found;
}

} // namespace

struct RuntimeVirtualTextureResidentPool::State {
    struct DirectoryCache {
        world::ResourceId texture{};
        std::filesystem::path path;
        render::VirtualTextureArchiveDirectory directory;
    };

    std::uint64_t budget = 0U;
    std::uint64_t tick = 0U;
    std::vector<RuntimeVirtualTextureResidentTile> resident_tiles;
    std::vector<DirectoryCache> directories;

    [[nodiscard]] const DirectoryCache* directory_for(
        const CookedTextureRecord& record,
        std::string& diagnostics
    ) {
        const auto existing = std::find_if(directories.begin(), directories.end(), [&](const auto& cached) {
            return cached.texture == record.resource_id;
        });
        if (existing != directories.end()) return &*existing;

        const auto path = archive_path_for(record);
        const auto loaded = render::load_virtual_texture_archive_directory(path);
        if (!loaded.directory.has_value()) {
            diagnostics = loaded.diagnostics;
            return nullptr;
        }
        if (!render::virtual_texture_archive_matches_asset(*loaded.directory, record.asset)) {
            diagnostics = "Virtual texture tile archive does not match its cooked texture asset.";
            return nullptr;
        }
        directories.push_back(DirectoryCache{
            .texture = record.resource_id,
            .path = path,
            .directory = *loaded.directory,
        });
        return &directories.back();
    }
};

RuntimeVirtualTextureResidentPool::RuntimeVirtualTextureResidentPool(const std::uint64_t budget_bytes)
    : state_{std::make_unique<State>()} {
    state_->budget = budget_bytes;
}

RuntimeVirtualTextureResidentPool::~RuntimeVirtualTextureResidentPool() = default;
RuntimeVirtualTextureResidentPool::RuntimeVirtualTextureResidentPool(
    RuntimeVirtualTextureResidentPool&&
) noexcept = default;
RuntimeVirtualTextureResidentPool& RuntimeVirtualTextureResidentPool::operator=(
    RuntimeVirtualTextureResidentPool&&
) noexcept = default;

RuntimeVirtualTextureResidentPoolUpdateResult RuntimeVirtualTextureResidentPool::update(
    render::Device& device,
    const CookedTextureCatalog& catalog,
    const std::span<const RuntimeVirtualTextureTileKey> requests
) {
    RuntimeVirtualTextureResidentPoolUpdateResult result;
    if (state_ == nullptr) {
        result.diagnostics = "Runtime virtual texture resident pool state is unavailable.";
        return result;
    }

    std::vector<RuntimeVirtualTextureTileKey> canonical_requests(requests.begin(), requests.end());
    for (const auto& request : canonical_requests) {
        if (!request.texture.valid()) {
            result.diagnostics = "Runtime virtual texture request contains an invalid texture identity.";
            return result;
        }
    }
    std::sort(canonical_requests.begin(), canonical_requests.end(), key_less);
    canonical_requests.erase(
        std::unique(canonical_requests.begin(), canonical_requests.end()),
        canonical_requests.end()
    );
    if (canonical_requests.size() > std::numeric_limits<std::uint32_t>::max()) {
        result.diagnostics = "Runtime virtual texture request set is too large.";
        return result;
    }
    result.requested = static_cast<std::uint32_t>(canonical_requests.size());

    struct ResolvedRequest {
        RuntimeVirtualTextureTileKey key{};
        const CookedTextureRecord* record = nullptr;
        std::filesystem::path archive_path;
        render::VirtualTextureArchiveTile descriptor{};
        bool hit = false;
    };
    std::vector<ResolvedRequest> resolved;
    resolved.reserve(canonical_requests.size());
    std::uint64_t requested_bytes = 0U;
    std::uint64_t missing_bytes = 0U;

    for (const auto& key : canonical_requests) {
        const auto* const record = catalog.find(key.texture);
        if (record == nullptr || record->asset.version != render::texture_asset_format_version_v2 ||
            record->path.empty()) {
            result.diagnostics = "Runtime virtual texture request references a non-streamable texture asset.";
            return result;
        }
        const auto* const cached = state_->directory_for(*record, result.diagnostics);
        if (cached == nullptr) return result;
        const auto* const descriptor = find_descriptor(cached->directory, key);
        if (descriptor == nullptr) {
            result.diagnostics = "Runtime virtual texture request references an unknown canonical tile.";
            return result;
        }
        if (requested_bytes > std::numeric_limits<std::uint64_t>::max() - descriptor->payload_size) {
            result.diagnostics = "Runtime virtual texture requested byte count overflowed.";
            return result;
        }
        requested_bytes += descriptor->payload_size;
        const bool hit = resident(key);
        if (!hit) {
            if (missing_bytes > std::numeric_limits<std::uint64_t>::max() - descriptor->payload_size) {
                result.diagnostics = "Runtime virtual texture missing byte count overflowed.";
                return result;
            }
            missing_bytes += descriptor->payload_size;
        }
        resolved.push_back(ResolvedRequest{
            .key = key,
            .record = record,
            .archive_path = cached->path,
            .descriptor = *descriptor,
            .hit = hit,
        });
    }

    if (requested_bytes > state_->budget) {
        result.diagnostics = "Runtime virtual texture requested working set exceeds the tile residency budget.";
        return result;
    }

    const std::uint64_t current_bytes = resident_bytes();
    if (current_bytes > std::numeric_limits<std::uint64_t>::max() - missing_bytes) {
        result.diagnostics = "Runtime virtual texture resident byte count overflowed.";
        return result;
    }
    std::uint64_t projected_bytes = current_bytes + missing_bytes;

    std::vector<const RuntimeVirtualTextureResidentTile*> eviction_candidates;
    for (const auto& resident_tile : state_->resident_tiles) {
        if (!contains_key(canonical_requests, resident_tile.key)) {
            eviction_candidates.push_back(&resident_tile);
        }
    }
    std::sort(eviction_candidates.begin(), eviction_candidates.end(), [](const auto* left, const auto* right) {
        if (left->last_used != right->last_used) return left->last_used < right->last_used;
        return key_less(left->key, right->key);
    });

    std::vector<RuntimeVirtualTextureTileKey> evict_keys;
    for (const auto* candidate : eviction_candidates) {
        if (projected_bytes <= state_->budget) break;
        projected_bytes -= candidate->descriptor.payload_size;
        evict_keys.push_back(candidate->key);
    }
    if (projected_bytes > state_->budget) {
        result.diagnostics = "Runtime virtual texture could not satisfy the requested working set within budget.";
        return result;
    }

    std::vector<RuntimeVirtualTextureResidentTile> staged;
    staged.reserve(resolved.size());
    for (const auto& request : resolved) {
        if (request.hit) {
            ++result.resident_hits;
            continue;
        }
        const auto loaded = render::load_virtual_texture_archive_tile(
            request.archive_path,
            state_->directories[static_cast<std::size_t>(std::distance(
                state_->directories.begin(),
                std::find_if(state_->directories.begin(), state_->directories.end(), [&](const auto& cached) {
                    return cached.texture == request.key.texture;
                })
            ))].directory,
            request.key.mip_level,
            request.key.tile_x,
            request.key.tile_y
        );
        if (!loaded.tile.has_value()) {
            result.diagnostics = loaded.diagnostics;
            return result;
        }

        render::TextureDesc desc;
        desc.dimension = render::TextureDimension::d2;
        desc.width = request.descriptor.width;
        desc.height = request.descriptor.height;
        desc.depth = 1U;
        desc.mip_levels = 1U;
        desc.array_layers = 1U;
        desc.sample_count = 1U;
        desc.format = request.record->asset.color_space == render::TextureColorSpace::srgb
            ? render::TextureFormat::rgba8_unorm_srgb
            : render::TextureFormat::rgba8_unorm;
        desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
        desc.debug_name = "VICE Runtime virtual texture physical tile";
        auto texture = device.create_texture(desc);
        const std::uint64_t row_bytes = static_cast<std::uint64_t>(request.descriptor.width) * 4U;
        if (!texture || !texture->write_2d(loaded.tile->rgba8, row_bytes, 0U)) {
            result.diagnostics = "Runtime virtual texture could not upload a requested physical tile.";
            return result;
        }
        staged.push_back(RuntimeVirtualTextureResidentTile{
            .key = request.key,
            .descriptor = request.descriptor,
            .texture = std::move(texture),
            .last_used = 0U,
        });
    }

    if (state_->tick == std::numeric_limits<std::uint64_t>::max()) {
        state_->tick = 0U;
        for (auto& tile : state_->resident_tiles) tile.last_used = 0U;
    }
    const std::uint64_t use_tick = ++state_->tick;

    std::sort(evict_keys.begin(), evict_keys.end(), key_less);
    const auto should_evict = [&](const RuntimeVirtualTextureResidentTile& tile) {
        return std::binary_search(evict_keys.begin(), evict_keys.end(), tile.key, key_less);
    };
    const auto before_evict = state_->resident_tiles.size();
    std::erase_if(state_->resident_tiles, should_evict);
    result.evicted = static_cast<std::uint32_t>(before_evict - state_->resident_tiles.size());

    for (auto& tile : state_->resident_tiles) {
        if (contains_key(canonical_requests, tile.key)) tile.last_used = use_tick;
    }
    for (auto& tile : staged) {
        tile.last_used = use_tick;
        state_->resident_tiles.push_back(std::move(tile));
        ++result.loaded;
    }
    std::sort(state_->resident_tiles.begin(), state_->resident_tiles.end(), [](const auto& left, const auto& right) {
        return key_less(left.key, right.key);
    });
    return result;
}

render::Texture* RuntimeVirtualTextureResidentPool::find(
    const RuntimeVirtualTextureTileKey& key
) const noexcept {
    if (state_ == nullptr) return nullptr;
    const auto found = std::lower_bound(
        state_->resident_tiles.begin(),
        state_->resident_tiles.end(),
        key,
        [](const auto& resident_tile, const auto& lookup) {
            return key_less(resident_tile.key, lookup);
        }
    );
    return found != state_->resident_tiles.end() && found->key == key
        ? found->texture.get()
        : nullptr;
}

bool RuntimeVirtualTextureResidentPool::resident(
    const RuntimeVirtualTextureTileKey& key
) const noexcept {
    return find(key) != nullptr;
}

std::uint64_t RuntimeVirtualTextureResidentPool::resident_bytes() const noexcept {
    if (state_ == nullptr) return 0U;
    std::uint64_t bytes = 0U;
    for (const auto& tile : state_->resident_tiles) {
        if (bytes > std::numeric_limits<std::uint64_t>::max() - tile.descriptor.payload_size) {
            return std::numeric_limits<std::uint64_t>::max();
        }
        bytes += tile.descriptor.payload_size;
    }
    return bytes;
}

std::uint64_t RuntimeVirtualTextureResidentPool::budget_bytes() const noexcept {
    return state_ != nullptr ? state_->budget : 0U;
}

std::span<const RuntimeVirtualTextureResidentTile> RuntimeVirtualTextureResidentPool::tiles() const noexcept {
    return state_ != nullptr
        ? std::span<const RuntimeVirtualTextureResidentTile>{state_->resident_tiles}
        : std::span<const RuntimeVirtualTextureResidentTile>{};
}

} // namespace vice::runtime

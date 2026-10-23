#include "texture_pipeline.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace vice::runtime {

bool CookedTextureCatalog::load(const std::filesystem::path& content_root) {
    records_.clear();
    diagnostics_.clear();

    std::error_code error;
    if (content_root.empty() || !std::filesystem::is_directory(content_root, error)) {
        diagnostics_ = error
            ? "Could not inspect runtime content root: " + error.message()
            : "Runtime content root is not available.";
        return false;
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> seen;
    std::filesystem::recursive_directory_iterator iterator{
        content_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics_ = "Could not enumerate runtime textures: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate runtime textures: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vtexc") continue;

        auto loaded = render::load_texture_asset(iterator->path());
        if (!loaded.asset.has_value()) {
            diagnostics_ = "Could not load cooked runtime texture `" +
                iterator->path().generic_string() + "`: " + loaded.diagnostics;
            return false;
        }
        const world::ResourceId resource{world::PersistentId{loaded.asset->stable_id}};
        if (!resource.valid()) {
            diagnostics_ = "Cooked runtime texture has no stable ID: " +
                iterator->path().generic_string();
            return false;
        }
        if (!seen.insert(resource).second) {
            diagnostics_ = "Duplicate cooked runtime texture stable ID `" +
                resource.value.to_string() + "`.";
            return false;
        }
        records_.push_back(CookedTextureRecord{
            .resource_id = resource,
            .path = iterator->path(),
            .asset = std::move(*loaded.asset),
        });
    }
    return true;
}

const CookedTextureRecord* CookedTextureCatalog::find(const world::ResourceId resource_id) const noexcept {
    const auto found = std::find_if(records_.begin(), records_.end(), [resource_id](const auto& record) {
        return record.resource_id == resource_id;
    });
    return found == records_.end() ? nullptr : &*found;
}

std::span<const CookedTextureRecord> CookedTextureCatalog::records() const noexcept {
    return records_;
}

const std::string& CookedTextureCatalog::diagnostics() const noexcept {
    return diagnostics_;
}

namespace detail {
namespace {

[[nodiscard]] std::unique_ptr<render::Texture> create_texture(
    render::Device& device,
    const CookedTextureRecord& record
) {
    const std::uint32_t mip_count = render::texture_mip_count(record.asset);
    if (mip_count == 0U) return nullptr;

    render::TextureDesc desc;
    desc.dimension = render::TextureDimension::d2;
    desc.width = record.asset.width;
    desc.height = record.asset.height;
    desc.depth = 1U;
    desc.mip_levels = mip_count;
    desc.array_layers = 1U;
    desc.sample_count = 1U;
    desc.format = record.asset.color_space == render::TextureColorSpace::srgb
        ? render::TextureFormat::rgba8_unorm_srgb
        : render::TextureFormat::rgba8_unorm;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
    desc.debug_name = "VICE Runtime project texture";
    auto texture = device.create_texture(desc);
    if (!texture) return nullptr;

    for (std::uint32_t mip_level = 0U; mip_level < mip_count; ++mip_level) {
        const auto pixels = render::texture_mip_rgba8(record.asset, mip_level);
        const std::uint32_t width = record.asset.version == render::texture_asset_format_version_v2
            ? record.asset.mip_levels[mip_level].width
            : record.asset.width;
        const std::uint32_t height = record.asset.version == render::texture_asset_format_version_v2
            ? record.asset.mip_levels[mip_level].height
            : record.asset.height;
        const std::uint64_t expected_size = static_cast<std::uint64_t>(width) * height * 4U;
        if (pixels.empty() || pixels.size() != expected_size ||
            !texture->write_2d(pixels, static_cast<std::uint64_t>(width) * 4U, mip_level)) {
            return nullptr;
        }
    }
    return texture;
}

[[nodiscard]] std::unique_ptr<render::Texture> create_fallback(
    render::Device& device,
    const RuntimeMaterialTextureChannel channel
) {
    render::TextureDesc desc;
    desc.dimension = render::TextureDimension::d2;
    desc.width = 1U;
    desc.height = 1U;
    desc.depth = 1U;
    desc.mip_levels = 1U;
    desc.array_layers = 1U;
    desc.sample_count = 1U;
    desc.format = channel == RuntimeMaterialTextureChannel::normal ||
            channel == RuntimeMaterialTextureChannel::orm
        ? render::TextureFormat::rgba8_unorm
        : render::TextureFormat::rgba8_unorm_srgb;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
    desc.debug_name = "VICE Runtime material residency fallback";
    auto texture = device.create_texture(desc);
    if (!texture) return nullptr;

    std::array<std::byte, 4U> texel{};
    switch (channel) {
    case RuntimeMaterialTextureChannel::base_color:
        texel = {std::byte{255U}, std::byte{255U}, std::byte{255U}, std::byte{255U}};
        break;
    case RuntimeMaterialTextureChannel::normal:
        texel = {std::byte{128U}, std::byte{128U}, std::byte{255U}, std::byte{255U}};
        break;
    case RuntimeMaterialTextureChannel::orm:
        texel = {std::byte{255U}, std::byte{255U}, std::byte{0U}, std::byte{255U}};
        break;
    case RuntimeMaterialTextureChannel::emissive:
        texel = {std::byte{0U}, std::byte{0U}, std::byte{0U}, std::byte{255U}};
        break;
    }
    if (!texture->write_2d(texel, 4U)) return nullptr;
    return texture;
}

[[nodiscard]] std::size_t channel_index(const RuntimeMaterialTextureChannel channel) noexcept {
    return static_cast<std::size_t>(channel);
}

} // namespace

struct RuntimeTextureLibrary::State {
    render::Device* device = nullptr;
    CookedTextureCatalog catalog;
    std::unordered_map<world::ResourceId, std::unique_ptr<render::Texture>, world::ResourceIdHash> textures;
    std::array<std::unique_ptr<render::Texture>, 4U> fallbacks;
    std::uint64_t generation = 0U;
};

RuntimeTextureLibrary::RuntimeTextureLibrary() : state_{std::make_unique<State>()} {}
RuntimeTextureLibrary::~RuntimeTextureLibrary() = default;
RuntimeTextureLibrary::RuntimeTextureLibrary(RuntimeTextureLibrary&&) noexcept = default;
RuntimeTextureLibrary& RuntimeTextureLibrary::operator=(RuntimeTextureLibrary&&) noexcept = default;

bool RuntimeTextureLibrary::initialize(
    render::Device& device,
    const std::filesystem::path& content_root,
    std::string* const diagnostics,
    const bool eager_upload
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (state_ == nullptr) return fail("Runtime texture library state is unavailable.");
    state_->device = &device;
    state_->textures.clear();
    state_->fallbacks = {};
    state_->generation = 0U;
    if (!state_->catalog.load(content_root)) return fail(state_->catalog.diagnostics());

    for (std::size_t index = 0U; index < state_->fallbacks.size(); ++index) {
        state_->fallbacks[index] = create_fallback(
            device,
            static_cast<RuntimeMaterialTextureChannel>(index)
        );
        if (!state_->fallbacks[index]) {
            return fail("Could not create Runtime material residency fallback texture.");
        }
    }

    if (eager_upload) {
        std::vector<world::ResourceId> resources;
        resources.reserve(state_->catalog.records().size());
        for (const auto& record : state_->catalog.records()) resources.push_back(record.resource_id);
        if (!set_residency(resources, diagnostics)) return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeTextureLibrary::set_residency(
    const std::span<const world::ResourceId> resources,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (state_ == nullptr || state_->device == nullptr) {
        return fail("Runtime texture library is not initialized.");
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> requested;
    requested.reserve(resources.size());
    for (const auto resource : resources) {
        if (!resource.valid() || state_->catalog.find(resource) == nullptr) {
            return fail("Runtime texture residency references missing cooked texture `" +
                resource.value.to_string() + "`.");
        }
        requested.insert(resource);
    }

    bool changed = requested.size() != state_->textures.size();
    if (!changed) {
        for (const auto& entry : state_->textures) {
            if (!requested.contains(entry.first)) {
                changed = true;
                break;
            }
        }
    }
    if (!changed) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    std::erase_if(state_->textures, [&](const auto& entry) {
        return !requested.contains(entry.first);
    });

    for (const auto resource : resources) {
        if (state_->textures.contains(resource)) continue;
        const auto* const record = state_->catalog.find(resource);
        auto texture = create_texture(*state_->device, *record);
        if (!texture) {
            return fail("Could not upload resident GPU texture `" + resource.value.to_string() + "`." );
        }
        state_->textures.emplace(resource, std::move(texture));
    }
    ++state_->generation;

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

render::Texture* RuntimeTextureLibrary::find(const world::ResourceId resource_id) const noexcept {
    if (state_ == nullptr) return nullptr;
    const auto found = state_->textures.find(resource_id);
    return found == state_->textures.end() ? nullptr : found->second.get();
}

render::Texture* RuntimeTextureLibrary::resolve(
    const world::ResourceId resource_id,
    const RuntimeMaterialTextureChannel channel
) const noexcept {
    if (state_ == nullptr || state_->catalog.find(resource_id) == nullptr) return nullptr;
    if (auto* const texture = find(resource_id); texture != nullptr) return texture;
    return state_->fallbacks[channel_index(channel)].get();
}

bool RuntimeTextureLibrary::resident(const world::ResourceId resource_id) const noexcept {
    return find(resource_id) != nullptr;
}

std::uint64_t RuntimeTextureLibrary::resident_bytes() const noexcept {
    if (state_ == nullptr) return 0U;
    std::uint64_t bytes = 0U;
    for (const auto& entry : state_->textures) {
        const auto* const record = state_->catalog.find(entry.first);
        if (record != nullptr) bytes += static_cast<std::uint64_t>(record->asset.rgba8.size());
    }
    return bytes;
}

std::uint64_t RuntimeTextureLibrary::residency_generation() const noexcept {
    return state_ != nullptr ? state_->generation : 0U;
}

const CookedTextureCatalog& RuntimeTextureLibrary::catalog() const noexcept {
    return state_->catalog;
}

} // namespace detail
} // namespace vice::runtime

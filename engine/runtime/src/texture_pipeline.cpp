#include "texture_pipeline.hpp"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

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

struct RuntimeTextureLibrary::State {
    CookedTextureCatalog catalog;
    std::unordered_map<world::ResourceId, std::unique_ptr<render::Texture>, world::ResourceIdHash> textures;
};

RuntimeTextureLibrary::RuntimeTextureLibrary() : state_{std::make_unique<State>()} {}
RuntimeTextureLibrary::~RuntimeTextureLibrary() = default;
RuntimeTextureLibrary::RuntimeTextureLibrary(RuntimeTextureLibrary&&) noexcept = default;
RuntimeTextureLibrary& RuntimeTextureLibrary::operator=(RuntimeTextureLibrary&&) noexcept = default;

bool RuntimeTextureLibrary::initialize(
    render::Device& device,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (state_ == nullptr) return fail("Runtime texture library state is unavailable.");
    state_->textures.clear();
    if (!state_->catalog.load(content_root)) return fail(state_->catalog.diagnostics());

    for (const auto& record : state_->catalog.records()) {
        render::TextureDesc desc;
        desc.dimension = render::TextureDimension::d2;
        desc.width = record.asset.width;
        desc.height = record.asset.height;
        desc.depth = 1U;
        desc.mip_levels = 1U;
        desc.array_layers = 1U;
        desc.sample_count = 1U;
        desc.format = record.asset.color_space == render::TextureColorSpace::srgb
            ? render::TextureFormat::rgba8_unorm_srgb
            : render::TextureFormat::rgba8_unorm;
        desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
        desc.debug_name = "VICE Runtime project texture";
        auto texture = device.create_texture(desc);
        if (!texture) {
            return fail("Could not create GPU texture for `" + record.resource_id.value.to_string() + "`.");
        }
        const std::uint64_t row_bytes = static_cast<std::uint64_t>(record.asset.width) * 4U;
        if (!texture->write_2d(record.asset.rgba8, row_bytes)) {
            return fail("Could not upload GPU texture for `" + record.resource_id.value.to_string() + "`.");
        }
        state_->textures.emplace(record.resource_id, std::move(texture));
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

render::Texture* RuntimeTextureLibrary::find(const world::ResourceId resource_id) const noexcept {
    if (state_ == nullptr) return nullptr;
    const auto found = state_->textures.find(resource_id);
    return found == state_->textures.end() ? nullptr : found->second.get();
}

const CookedTextureCatalog& RuntimeTextureLibrary::catalog() const noexcept {
    return state_->catalog;
}

} // namespace detail
} // namespace vice::runtime

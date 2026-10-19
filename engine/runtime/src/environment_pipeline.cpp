#include "environment_pipeline.hpp"

#include <vice/render/environment_asset.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::runtime::detail {
namespace {

[[nodiscard]] constexpr std::uint32_t mip_extent(
    std::uint32_t extent,
    std::uint32_t level
) noexcept {
    while (level-- > 0U && extent > 1U) extent /= 2U;
    return extent == 0U ? 1U : extent;
}

[[nodiscard]] std::unique_ptr<render::Texture> create_rgba16f_texture(
    render::Device& device,
    const std::uint32_t width,
    const std::uint32_t height,
    const std::uint32_t mip_levels,
    const std::string_view debug_name
) {
    render::TextureDesc desc;
    desc.dimension = render::TextureDimension::d2;
    desc.width = width;
    desc.height = height;
    desc.depth = 1U;
    desc.mip_levels = mip_levels;
    desc.array_layers = 1U;
    desc.sample_count = 1U;
    desc.format = render::TextureFormat::rgba16_float;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::copy_destination;
    desc.debug_name = debug_name;
    return device.create_texture(desc);
}

[[nodiscard]] bool upload_single_mip(
    render::Texture& texture,
    const std::span<const std::byte> bytes,
    const std::uint32_t width,
    const std::uint32_t mip_level
) {
    return texture.write_2d(bytes, static_cast<std::uint64_t>(width) * 8U, mip_level);
}

} // namespace

struct RuntimeEnvironment::State {
    std::unique_ptr<render::Texture> irradiance;
    std::unique_ptr<render::Texture> specular;
    std::unique_ptr<render::Texture> brdf_lut;
    std::unique_ptr<render::Sampler> sampler;
    float intensity = 0.0F;
    float rotation_radians = 0.0F;
    float max_specular_lod = 0.0F;
    bool available = false;
};

RuntimeEnvironment::RuntimeEnvironment() : state_{std::make_unique<State>()} {}
RuntimeEnvironment::~RuntimeEnvironment() = default;
RuntimeEnvironment::RuntimeEnvironment(RuntimeEnvironment&&) noexcept = default;
RuntimeEnvironment& RuntimeEnvironment::operator=(RuntimeEnvironment&&) noexcept = default;

bool RuntimeEnvironment::initialize(
    render::Device& device,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (state_ == nullptr) return fail("Runtime environment state is unavailable.");

    *state_ = State{};
    std::error_code error;
    if (content_root.empty() || !std::filesystem::is_directory(content_root, error)) {
        return fail(error
            ? "Could not inspect Runtime content root for environments: " + error.message()
            : "Runtime content root is unavailable for environment discovery.");
    }

    std::vector<std::filesystem::path> environment_paths;
    std::filesystem::recursive_directory_iterator iterator{
        content_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) return fail("Could not enumerate Runtime environments: " + error.message());
    for (; iterator != end; iterator.increment(error)) {
        if (error) return fail("Could not enumerate Runtime environments: " + error.message());
        if (iterator->is_regular_file() && iterator->path().extension() == ".venvc") {
            environment_paths.push_back(iterator->path());
        }
    }
    std::sort(environment_paths.begin(), environment_paths.end());
    if (environment_paths.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (environment_paths.size() != 1U) {
        return fail("VICE Runtime currently supports exactly one global environment asset per project.");
    }

    auto loaded = render::load_environment_asset(environment_paths.front());
    if (!loaded.asset.has_value()) {
        return fail("Could not load Runtime environment `" + environment_paths.front().generic_string() +
            "`: " + loaded.diagnostics);
    }
    const auto& asset = *loaded.asset;

    state_->irradiance = create_rgba16f_texture(
        device,
        asset.irradiance_width,
        asset.irradiance_height,
        1U,
        "VICE Runtime environment irradiance"
    );
    state_->specular = create_rgba16f_texture(
        device,
        asset.specular_width,
        asset.specular_height,
        asset.specular_mip_levels,
        "VICE Runtime environment specular"
    );
    state_->brdf_lut = create_rgba16f_texture(
        device,
        asset.brdf_lut_size,
        asset.brdf_lut_size,
        1U,
        "VICE Runtime environment BRDF LUT"
    );
    if (!state_->irradiance || !state_->specular || !state_->brdf_lut) {
        return fail("Could not create Runtime environment textures.");
    }

    if (!upload_single_mip(
            *state_->irradiance,
            asset.irradiance_rgba16f,
            asset.irradiance_width,
            0U
        ) || !upload_single_mip(
            *state_->brdf_lut,
            asset.brdf_lut_rgba16f,
            asset.brdf_lut_size,
            0U
        )) {
        return fail("Could not upload Runtime environment irradiance or BRDF data.");
    }

    std::size_t specular_offset = 0U;
    for (std::uint32_t mip = 0U; mip < asset.specular_mip_levels; ++mip) {
        const std::uint32_t width = mip_extent(asset.specular_width, mip);
        const std::uint32_t height = mip_extent(asset.specular_height, mip);
        const std::size_t level_bytes = static_cast<std::size_t>(width) * height * 8U;
        if (specular_offset > asset.specular_rgba16f.size() ||
            level_bytes > asset.specular_rgba16f.size() - specular_offset ||
            !upload_single_mip(
                *state_->specular,
                std::span<const std::byte>{asset.specular_rgba16f}.subspan(specular_offset, level_bytes),
                width,
                mip
            )) {
            return fail("Could not upload Runtime environment specular mip " + std::to_string(mip) + ".");
        }
        specular_offset += level_bytes;
    }
    if (specular_offset != asset.specular_rgba16f.size()) {
        return fail("Runtime environment specular mip payload has trailing data.");
    }

    render::SamplerDesc sampler_desc;
    sampler_desc.min_filter = render::Filter::linear;
    sampler_desc.mag_filter = render::Filter::linear;
    sampler_desc.mip_filter = render::Filter::linear;
    sampler_desc.address_u = render::AddressMode::repeat;
    sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    sampler_desc.max_anisotropy = 1U;
    sampler_desc.debug_name = "VICE Runtime environment sampler";
    state_->sampler = device.create_sampler(sampler_desc);
    if (!state_->sampler) return fail("Could not create Runtime environment sampler.");

    state_->intensity = asset.intensity;
    state_->rotation_radians = asset.rotation_radians;
    state_->max_specular_lod = static_cast<float>(asset.specular_mip_levels - 1U);
    state_->available = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeEnvironmentBinding RuntimeEnvironment::binding() const noexcept {
    if (state_ == nullptr || !state_->available) return {};
    return RuntimeEnvironmentBinding{
        .irradiance = state_->irradiance.get(),
        .specular = state_->specular.get(),
        .brdf_lut = state_->brdf_lut.get(),
        .sampler = state_->sampler.get(),
        .intensity = state_->intensity,
        .rotation_radians = state_->rotation_radians,
        .max_specular_lod = state_->max_specular_lod,
        .available = true,
    };
}

} // namespace vice::runtime::detail

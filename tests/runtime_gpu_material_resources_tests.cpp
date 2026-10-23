#include "gpu_material_resources.hpp"
#include "material_pipeline.hpp"

#include <vice/render/material_asset.hpp>
#include <vice/render/texture_asset.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/resource.hpp>

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

void write_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    assert(output);
    output.write(
        reinterpret_cast<const char*>(bytes.data()),
        static_cast<std::streamsize>(bytes.size())
    );
    assert(output);
}

void write_texture(
    const std::filesystem::path& path,
    const vice::world::PersistentId id,
    const std::byte red,
    const std::byte green,
    const std::byte blue
) {
    vice::render::TextureAsset asset;
    asset.stable_id = id.bytes();
    asset.source_fingerprint = 1U;
    asset.width = 1U;
    asset.height = 1U;
    asset.color_space = vice::render::TextureColorSpace::srgb;
    asset.rgba8 = {red, green, blue, std::byte{255U}};
    const auto encoded = vice::render::encode_texture_asset(asset);
    assert(encoded);
    write_bytes(path, encoded.bytes);
}

void write_gpu_scene_material(
    const std::filesystem::path& path,
    const vice::world::PersistentId material_id,
    const vice::world::PersistentId texture_a_id,
    const vice::world::PersistentId texture_b_id
) {
    vice::render::RenderMaterialAsset asset;
    asset.version = vice::render::render_material_asset_format_version_v4;
    asset.stable_id = material_id.bytes();
    asset.source_fingerprint = 1U;
    asset.base_color_texture = texture_a_id.bytes();
    asset.normal_texture = texture_b_id.bytes();
    asset.orm_texture = texture_a_id.bytes();
    asset.emissive_texture = texture_b_id.bytes();
    asset.vertex_shader_manifest = "unused/vertex/shader.vice.json";
    asset.fragment_shader_manifest = "unused/fragment/shader.vice.json";
    const auto encoded = vice::render::encode_render_material_asset(asset);
    assert(encoded);
    write_bytes(path, encoded.bytes);
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    const auto root = std::filesystem::temp_directory_path() /
        "vice_runtime_gpu_material_resources_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    assert(!error);

    const auto texture_a_id = required_id("71000000-0000-4000-8000-000000000001");
    const auto texture_b_id = required_id("71000000-0000-4000-8000-000000000002");
    const auto missing_id = required_id("71000000-0000-4000-8000-000000000099");
    const auto material_id = required_id("70000000-0000-4000-8000-000000000001");
    write_texture(
        root / "a.vtexc", texture_a_id,
        std::byte{255U}, std::byte{0U}, std::byte{0U}
    );
    write_texture(
        root / "b.vtexc", texture_b_id,
        std::byte{0U}, std::byte{128U}, std::byte{255U}
    );
    write_gpu_scene_material(
        root / "gpu_scene.vmatc", material_id, texture_a_id, texture_b_id
    );

    auto device = render::Device::create();
    assert(device);

    detail::RuntimeTextureLibrary textures;
    std::string diagnostics;
    assert(textures.initialize(*device, root, &diagnostics));
    assert(diagnostics.empty());
    const world::ResourceId texture_a_resource{texture_a_id};
    const world::ResourceId texture_b_resource{texture_b_id};
    const world::ResourceId missing_resource{missing_id};
    assert(textures.resident(texture_a_resource));
    assert(textures.resident(texture_b_resource));
    assert(textures.resident_bytes() == 8U);
    const auto initial_generation = textures.residency_generation();
    assert(textures.set_residency({}, &diagnostics));
    assert(textures.residency_generation() == initial_generation + 1U);
    assert(textures.find(texture_a_resource) == nullptr);
    assert(textures.find(texture_b_resource) == nullptr);
    assert(textures.resident_bytes() == 0U);
    auto* const fallback_base = textures.resolve(
        texture_a_resource, detail::RuntimeMaterialTextureChannel::base_color
    );
    auto* const fallback_normal = textures.resolve(
        texture_b_resource, detail::RuntimeMaterialTextureChannel::normal
    );
    assert(fallback_base != nullptr);
    assert(fallback_normal != nullptr);
    assert(fallback_base != fallback_normal);
    assert(textures.resolve(
        missing_resource, detail::RuntimeMaterialTextureChannel::base_color
    ) == nullptr);
    const std::array resident_a{texture_a_resource};
    assert(textures.set_residency(resident_a, &diagnostics));
    assert(textures.find(texture_a_resource) != nullptr);
    assert(textures.find(texture_b_resource) == nullptr);
    assert(textures.resident_bytes() == 4U);
    assert(textures.resolve(
        texture_b_resource, detail::RuntimeMaterialTextureChannel::normal
    ) == fallback_normal);
    const std::array resident_all{texture_a_resource, texture_b_resource};
    assert(textures.set_residency(resident_all, &diagnostics));
    assert(textures.find(texture_a_resource) != nullptr);
    assert(textures.find(texture_b_resource) != nullptr);
    assert(textures.resident_bytes() == 8U);

    RuntimeGpuMaterialSampler linear_repeat;
    RuntimeGpuMaterialSampler nearest_clamp;
    nearest_clamp.min_filter = render::Filter::nearest;
    nearest_clamp.mag_filter = render::Filter::nearest;
    nearest_clamp.mip_filter = render::Filter::nearest;
    nearest_clamp.address_u = render::AddressMode::clamp_to_edge;
    nearest_clamp.address_v = render::AddressMode::clamp_to_edge;
    nearest_clamp.address_w = render::AddressMode::clamp_to_edge;

    RuntimeGpuMaterialTablePlan plan;
    plan.texture_ids = {
        world::ResourceId{texture_b_id},
        world::ResourceId{texture_a_id},
    };
    plan.samplers = {nearest_clamp, linear_repeat};
    plan.records.resize(2U);
    plan.records[0].base_color_texture = 0U;
    plan.records[0].normal_texture = 1U;
    plan.records[0].sampler = 1U;
    plan.records[1].base_color_texture = 0U;
    plan.records[1].sampler = 0U;

    detail::RuntimeGpuMaterialResourceTable resources;
    assert(resources.initialize(*device, plan, textures, &diagnostics));
    assert(diagnostics.empty());

    auto view = resources.view();
    assert(view.textures.size() == 2U);
    assert(view.samplers.size() == 2U);
    assert(view.textures[0] == textures.find(world::ResourceId{texture_b_id}));
    assert(view.textures[1] == textures.find(world::ResourceId{texture_a_id}));
    assert(view.textures[0] != nullptr);
    assert(view.textures[1] != nullptr);
    assert(view.samplers[0] != nullptr);
    assert(view.samplers[1] != nullptr);
    assert(view.samplers[0] != view.samplers[1]);
    assert(plan.records[0].base_color_texture == plan.records[1].base_color_texture);
    assert(plan.records[0].sampler != plan.records[1].sampler);

    RuntimeGpuMaterialTablePlan missing_plan = plan;
    missing_plan.texture_ids.push_back(world::ResourceId{missing_id});
    assert(!resources.initialize(*device, missing_plan, textures, &diagnostics));
    assert(diagnostics.find(missing_id.to_string()) != std::string::npos);
    view = resources.view();
    assert(view.textures.empty());
    assert(view.samplers.empty());

    RuntimeGpuMaterialTablePlan smaller_plan;
    smaller_plan.texture_ids = {world::ResourceId{texture_a_id}};
    smaller_plan.samplers = {linear_repeat};
    assert(resources.initialize(*device, smaller_plan, textures, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures.size() == 1U);
    assert(view.samplers.size() == 1U);
    assert(view.textures.front() == textures.find(world::ResourceId{texture_a_id}));
    assert(view.samplers.front() != nullptr);

    detail::RuntimeMaterialLibrary materials;
    assert(materials.initialize(
        *device,
        render::TextureFormat::rgba16_float,
        {},
        root,
        &diagnostics
    ));
    assert(diagnostics.empty());

    const detail::RuntimeMaterialBinding gpu_scene_binding = materials.find(world::ResourceId{material_id});
    assert(gpu_scene_binding.pipeline == nullptr);
    assert(gpu_scene_binding.constants == nullptr);
    assert(gpu_scene_binding.constants_size == 0U);
    assert(gpu_scene_binding.sampler == nullptr);
    assert(gpu_scene_binding.base_color_texture != nullptr);
    assert(gpu_scene_binding.normal_texture != nullptr);
    assert(gpu_scene_binding.orm_texture != nullptr);
    assert(gpu_scene_binding.emissive_texture != nullptr);

    RuntimeGpuMaterialTablePlan resident_plan;
    resident_plan.texture_ids = {
        world::ResourceId{texture_b_id},
        world::ResourceId{texture_a_id},
    };
    resident_plan.texture_channels = {
        RuntimeGpuMaterialTextureChannel::normal,
        RuntimeGpuMaterialTextureChannel::base_color,
    };
    resident_plan.samplers = {linear_repeat};
    assert(resources.initialize(*device, resident_plan, materials, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures.size() == 2U);
    assert(view.textures[0] == gpu_scene_binding.normal_texture);
    assert(view.textures[1] == gpu_scene_binding.base_color_texture);

    assert(materials.set_texture_residency(resident_all, &diagnostics));
    const detail::RuntimeMaterialBinding resident_binding = materials.find(world::ResourceId{material_id});
    assert(resident_binding.base_color_texture != nullptr);
    assert(resident_binding.normal_texture != nullptr);
    assert(resident_binding.base_color_texture != gpu_scene_binding.base_color_texture);
    assert(resident_binding.normal_texture != gpu_scene_binding.normal_texture);
    assert(materials.resident_texture_bytes() == 8U);
    assert(resources.initialize(*device, resident_plan, materials, &diagnostics));
    assert(diagnostics.empty());
    view = resources.view();
    assert(view.textures[0] == resident_binding.normal_texture);
    assert(view.textures[1] == resident_binding.base_color_texture);
    assert(view.samplers.size() == 1U);
    assert(view.samplers.front() != nullptr);

    std::filesystem::remove_all(root, error);
    assert(!error);
    return 0;
}

#include "gpu_pbr_batches.hpp"

#include <vice/runtime/gpu_instance_materials.hpp>

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

[[nodiscard]] vice::runtime::CookedRenderMaterialRecord material_record(
    const char* const id,
    const std::uint32_t version
) {
    vice::runtime::CookedRenderMaterialRecord record;
    record.resource_id = vice::world::ResourceId{required_id(id)};
    record.asset.version = version;
    record.asset.stable_id = record.resource_id.value.bytes();
    return record;
}

struct Color3 final {
    float r{};
    float g{};
    float b{};
};

struct EnvironmentLobes final {
    Color3 diffuse{};
    Color3 specular{};
};

[[noreturn]] void fail(const std::string_view message) {
    std::cerr << "runtime GPU PBR regression: " << message << '\n';
    std::abort();
}

void require(const bool condition, const std::string_view message) {
    if (!condition) {
        fail(message);
    }
}

[[nodiscard]] bool close(const float lhs, const float rhs, const float epsilon = 1.0e-5F) {
    return std::fabs(lhs - rhs) <= epsilon;
}

void require_close(const Color3 actual, const Color3 expected, const std::string_view message) {
    if (!close(actual.r, expected.r) || !close(actual.g, expected.g) ||
        !close(actual.b, expected.b)) {
        std::ostringstream stream;
        stream << message << ": expected (" << expected.r << ", " << expected.g << ", "
               << expected.b << "), got (" << actual.r << ", " << actual.g << ", "
               << actual.b << ')';
        fail(stream.str());
    }
}

[[nodiscard]] Color3 multiply(const Color3 lhs, const Color3 rhs) {
    return Color3{lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b};
}

[[nodiscard]] Color3 multiply(const Color3 value, const float scalar) {
    return Color3{value.r * scalar, value.g * scalar, value.b * scalar};
}

[[nodiscard]] EnvironmentLobes reference_environment_lobes(
    const Color3 diffuse_irradiance,
    const Color3 specular_radiance,
    const Color3 f0,
    const Color3 albedo,
    const float metallic,
    const float brdf_x,
    const float brdf_y,
    const float occlusion
) {
    const float clamped_metallic = std::clamp(metallic, 0.0F, 1.0F);
    const Color3 fresnel{
        f0.r * brdf_x + brdf_y,
        f0.g * brdf_x + brdf_y,
        f0.b * brdf_x + brdf_y,
    };
    const Color3 kd{
        (1.0F - f0.r) * (1.0F - clamped_metallic),
        (1.0F - f0.g) * (1.0F - clamped_metallic),
        (1.0F - f0.b) * (1.0F - clamped_metallic),
    };
    return EnvironmentLobes{
        .diffuse = multiply(multiply(multiply(diffuse_irradiance, kd), albedo), occlusion),
        .specular = multiply(specular_radiance, fresnel),
    };
}

void validate_environment_energy_reference_cases() {
    constexpr Color3 diffuse_irradiance{1.2F, 0.7F, 0.4F};
    constexpr Color3 specular_radiance{0.9F, 0.9F, 0.9F};
    constexpr float brdf_x = 0.65F;
    constexpr float brdf_y = 0.03F;
    constexpr float occlusion = 0.75F;

    constexpr Color3 dielectric_f0{0.04F, 0.04F, 0.04F};
    constexpr Color3 dielectric_albedo{0.8F, 0.2F, 0.1F};
    const auto dielectric = reference_environment_lobes(
        diffuse_irradiance,
        specular_radiance,
        dielectric_f0,
        dielectric_albedo,
        0.0F,
        brdf_x,
        brdf_y,
        occlusion
    );
    require_close(
        dielectric.diffuse,
        Color3{0.6912F, 0.1008F, 0.0288F},
        "dielectric diffuse must apply albedo and the non-metal energy split exactly once"
    );
    require_close(
        dielectric.specular,
        Color3{0.0504F, 0.0504F, 0.0504F},
        "dielectric specular must remain independent of post-BRDF base-color tinting"
    );

    const auto dielectric_other_albedo = reference_environment_lobes(
        diffuse_irradiance,
        specular_radiance,
        dielectric_f0,
        Color3{0.1F, 0.8F, 0.2F},
        0.0F,
        brdf_x,
        brdf_y,
        occlusion
    );
    require_close(
        dielectric_other_albedo.specular,
        dielectric.specular,
        "changing dielectric albedo must not tint the environment specular lobe after Fresnel"
    );

    constexpr Color3 metallic_albedo{0.8F, 0.2F, 0.1F};
    const auto metallic = reference_environment_lobes(
        diffuse_irradiance,
        specular_radiance,
        metallic_albedo,
        metallic_albedo,
        1.0F,
        brdf_x,
        brdf_y,
        occlusion
    );
    require_close(
        metallic.diffuse,
        Color3{0.0F, 0.0F, 0.0F},
        "fully metallic environment lighting must contribute no diffuse IBL"
    );
    require_close(
        metallic.specular,
        Color3{0.495F, 0.144F, 0.0855F},
        "metallic environment specular must carry base color only through f0/Fresnel"
    );

    const Color3 broken_double_tint = multiply(metallic.specular, metallic_albedo);
    require(
        !close(broken_double_tint.r, metallic.specular.r) &&
            !close(broken_double_tint.g, metallic.specular.g) &&
            !close(broken_double_tint.b, metallic.specular.b),
        "metallic reference case must detect the historical post-Fresnel base-color multiply"
    );
}

[[nodiscard]] std::optional<std::filesystem::path> find_gpu_pbr_shader_from(
    std::filesystem::path root
) {
    std::error_code error;
    if (root.empty()) {
        return std::nullopt;
    }
    root = std::filesystem::absolute(root, error);
    if (error) {
        error.clear();
    }

    for (int depth = 0; depth < 12; ++depth) {
        const auto candidate = root / "assets" / "shaders" / "vice_gpu_pbr_surface.hlsl";
        if (std::filesystem::is_regular_file(candidate, error)) {
            return candidate;
        }
        error.clear();
        const auto parent = root.parent_path();
        if (parent.empty() || parent == root) {
            break;
        }
        root = parent;
    }
    return std::nullopt;
}

[[nodiscard]] std::filesystem::path gpu_pbr_shader_path() {
    if (const auto from_source = find_gpu_pbr_shader_from(std::filesystem::path{__FILE__}.parent_path())) {
        return *from_source;
    }
    if (const auto from_working_directory = find_gpu_pbr_shader_from(std::filesystem::current_path())) {
        return *from_working_directory;
    }
    fail("could not locate assets/shaders/vice_gpu_pbr_surface.hlsl");
}

[[nodiscard]] std::string read_text_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(input.good(), "could not open production GPU PBR shader");
    std::ostringstream stream;
    stream << input.rdbuf();
    require(input.good() || input.eof(), "could not read production GPU PBR shader");
    return stream.str();
}

[[nodiscard]] std::size_t count_occurrences(
    const std::string_view text,
    const std::string_view needle
) {
    require(!needle.empty(), "source-contract needle must not be empty");
    std::size_t count = 0U;
    std::size_t position = 0U;
    while ((position = text.find(needle, position)) != std::string_view::npos) {
        ++count;
        position += needle.size();
    }
    return count;
}

[[nodiscard]] std::string_view source_block(
    const std::string_view shader,
    const std::string_view begin_marker,
    const std::string_view end_marker
) {
    const auto begin = shader.find(begin_marker);
    require(begin != std::string_view::npos, "production shader source block start was not found");
    const auto end = shader.find(end_marker, begin + begin_marker.size());
    require(end != std::string_view::npos, "production shader source block end was not found");
    return shader.substr(begin, end - begin);
}

void validate_environment_shader_contract() {
    const std::string shader_storage = read_text_file(gpu_pbr_shader_path());
    const std::string_view shader = shader_storage;

    constexpr std::string_view fresnel_contract =
        "const float3 fresnel = f0 * brdf.x + brdf.y;";
    constexpr std::string_view kd_contract =
        "const float3 kd = (1.0 - f0) * (1.0 - saturate(metallic_value));";
    constexpr std::string_view diffuse_contract =
        "const float3 diffuse = diffuse_irradiance * kd * albedo * occlusion;";
    constexpr std::string_view composition_contract =
        "return (diffuse + specular * fresnel) * metadata.x * metadata.w;";

    require(
        count_occurrences(shader, fresnel_contract) == 3U,
        "global environment and both local probes must use the same Fresnel/BRDF contract"
    );
    require(
        count_occurrences(shader, kd_contract) == 3U,
        "global environment and both local probes must use the non-metal diffuse energy split"
    );
    require(
        count_occurrences(shader, diffuse_contract) == 3U,
        "global environment and both local probes must apply albedo exactly once in diffuse IBL"
    );
    require(
        count_occurrences(shader, composition_contract) == 3U,
        "all environment sources must compose diffuse and specular before source weighting"
    );
    require(
        shader.find("environment * base_sample.rgb") == std::string_view::npos &&
            shader.find("environment*base_sample.rgb") == std::string_view::npos,
        "fragment composition must not multiply accumulated environment specular by base color"
    );
    require(
        shader.find("ambient + direct + environment + emissive") != std::string_view::npos,
        "fragment composition must add the already material-aware environment term directly"
    );

    const auto source_0 = source_block(
        shader,
        "float3 vice_environment_source_0(",
        "float3 vice_environment_source_1("
    );
    const auto source_1 = source_block(
        shader,
        "float3 vice_environment_source_1(",
        "float3 vice_environment_source_2("
    );
    const auto source_2 = source_block(
        shader,
        "float3 vice_environment_source_2(",
        "float vice_ray_ambient_occlusion_visibility("
    );
    require(
        source_0.find("vice_runtime_reflection_specular(pixel_position, raster_specular)") !=
            std::string_view::npos,
        "RT reflection replacement must remain on the global environment specular source"
    );
    require(
        source_1.find("vice_runtime_reflection_specular") == std::string_view::npos &&
            source_2.find("vice_runtime_reflection_specular") == std::string_view::npos,
        "RT reflection replacement must not replace local-probe specular sources"
    );
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    validate_environment_energy_reference_cases();
    validate_environment_shader_contract();

    const world::ResourceId mesh_a{required_id("60000000-0000-4000-8000-000000000001")};
    const world::ResourceId mesh_b{required_id("60000000-0000-4000-8000-000000000002")};
    const world::ResourceId material_a{required_id("70000000-0000-4000-8000-000000000001")};
    const world::ResourceId material_b{required_id("70000000-0000-4000-8000-000000000002")};
    const world::ResourceId legacy_material{required_id("70000000-0000-4000-8000-000000000003")};
    const auto texture_a = required_id("71000000-0000-4000-8000-000000000001");
    const auto texture_b = required_id("71000000-0000-4000-8000-000000000002");

    std::vector<CookedMeshRecord> meshes(2U);
    meshes[0].resource_id = mesh_a;
    meshes[0].asset.indices = {0U, 1U, 2U};
    meshes[1].resource_id = mesh_b;
    meshes[1].asset.indices = {0U, 1U, 2U, 2U, 3U, 0U};

    std::vector<CookedRenderMaterialRecord> materials;
    auto gpu_material_a = material_record(
        "70000000-0000-4000-8000-000000000001",
        render::render_material_asset_format_version_v4
    );
    gpu_material_a.asset.base_color_texture = texture_a.bytes();
    gpu_material_a.asset.normal_texture = texture_a.bytes();
    gpu_material_a.asset.orm_texture = texture_a.bytes();
    gpu_material_a.asset.emissive_texture = texture_a.bytes();
    materials.push_back(std::move(gpu_material_a));

    auto gpu_material_b = material_record(
        "70000000-0000-4000-8000-000000000002",
        render::render_material_asset_format_version_v4
    );
    gpu_material_b.asset.base_color_texture = texture_b.bytes();
    gpu_material_b.asset.normal_texture = texture_b.bytes();
    gpu_material_b.asset.orm_texture = texture_b.bytes();
    gpu_material_b.asset.emissive_texture = texture_b.bytes();
    materials.push_back(std::move(gpu_material_b));

    materials.push_back(material_record(
        "70000000-0000-4000-8000-000000000003",
        render::render_material_asset_format_version_v3
    ));

    std::vector<RenderMeshInstance> instances(5U);
    instances[0].mesh = mesh_a;
    instances[0].material = material_a;
    instances[1].mesh = mesh_a;
    instances[1].material = material_b;
    instances[2].mesh = mesh_a;
    instances[2].material = legacy_material;
    instances[3].mesh = mesh_a;
    instances[3].material.reset();
    instances[4].mesh = mesh_b;
    instances[4].material = material_b;

    const auto batches = detail::build_runtime_gpu_pbr_batch_plan(instances, meshes, materials);
    assert(batches);
    assert(batches.plan.instance_batches.size() == instances.size());
    assert(batches.plan.batches.size() == 2U);
    assert(batches.plan.batch_meshes.size() == 2U);
    assert(batches.plan.batch_meshes[0] == mesh_a);
    assert(batches.plan.batch_meshes[1] == mesh_b);
    assert(batches.plan.batches[0].index_count == 3U);
    assert(batches.plan.batches[1].index_count == 6U);
    assert(batches.plan.instance_batches[0] == 0U);
    assert(batches.plan.instance_batches[1] == 0U);
    assert(batches.plan.instance_batches[2] == runtime_gpu_draw_batch_unassigned);
    assert(batches.plan.instance_batches[3] == runtime_gpu_draw_batch_unassigned);
    assert(batches.plan.instance_batches[4] == 1U);

    const auto material_table = build_runtime_gpu_material_table_plan(
        RuntimeGpuMaterialTableBuildInput{
            .materials = materials,
            .validate_texture_references = false,
        }
    );
    assert(material_table);
    const auto material_a_index = material_table.plan.material_index(material_a);
    const auto material_b_index = material_table.plan.material_index(material_b);
    const auto legacy_material_index = material_table.plan.material_index(legacy_material);
    assert(material_a_index.has_value());
    assert(material_b_index.has_value());
    assert(legacy_material_index.has_value());
    assert(material_table.plan.records[*material_a_index].base_color_texture !=
        material_table.plan.records[*material_b_index].base_color_texture);
    assert(material_table.plan.texture_ids.size() == 8U);
    assert(material_table.plan.texture_channels.size() == 8U);
    for (std::size_t channel = 0U; channel < 4U; ++channel) {
        assert(material_table.plan.texture_ids[channel] == world::ResourceId{texture_a});
        assert(material_table.plan.texture_channels[channel] ==
            static_cast<RuntimeGpuMaterialTextureChannel>(channel));
        assert(material_table.plan.texture_ids[4U + channel] == world::ResourceId{texture_b});
        assert(material_table.plan.texture_channels[4U + channel] ==
            static_cast<RuntimeGpuMaterialTextureChannel>(channel));
    }

    const auto instance_materials = build_runtime_gpu_instance_material_records(
        instances,
        material_table.plan
    );
    assert(instance_materials);
    assert(instance_materials.records.size() == instances.size());
    assert(instance_materials.records[0].material_index == *material_a_index);
    assert(instance_materials.records[1].material_index == *material_b_index);
    assert(instance_materials.records[2].material_index == *legacy_material_index);
    assert(instance_materials.records[3].material_index == runtime_gpu_resource_unassigned);
    assert(instance_materials.records[4].material_index == *material_b_index);
    assert(batches.plan.instance_batches[0] == batches.plan.instance_batches[1]);
    assert(instance_materials.records[0].material_index != instance_materials.records[1].material_index);
    assert(batches.plan.instance_batches[2] == runtime_gpu_draw_batch_unassigned);

    auto missing_material_instances = instances;
    missing_material_instances[0].material = world::ResourceId{
        required_id("70000000-0000-4000-8000-000000000099")
    };
    const auto missing_material = detail::build_runtime_gpu_pbr_batch_plan(
        missing_material_instances, meshes, materials
    );
    assert(!missing_material);

    auto missing_mesh_instances = instances;
    missing_mesh_instances[0].mesh = world::ResourceId{
        required_id("60000000-0000-4000-8000-000000000099")
    };
    const auto missing_mesh = detail::build_runtime_gpu_pbr_batch_plan(
        missing_mesh_instances, meshes, materials
    );
    assert(!missing_mesh);

    return 0;
}

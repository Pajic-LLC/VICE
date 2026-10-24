#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {

[[nodiscard]] std::string read_text_file(const char* const path) {
    std::ifstream input{path, std::ios::binary};
    if (!input.good()) return {};
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
}

} // namespace

int main() {
#ifndef VICE_RAY_AMBIENT_OCCLUSION_MANIFEST
    return 0;
#else
    using namespace vice::render;

    const std::string json = read_text_file(VICE_RAY_AMBIENT_OCCLUSION_MANIFEST);
    CHECK(!json.empty());
    const CookedShaderManifest manifest = parse_cooked_shader_manifest(json);
    CHECK(manifest.valid);
    CHECK(manifest.stage == ShaderStage::compute);
    CHECK(manifest.entry_point == "vice_ray_ambient_occlusion_cs");
    CHECK(manifest.compute_thread_group_size.has_value());
    CHECK(manifest.compute_thread_group_size->x == 8U);
    CHECK(manifest.compute_thread_group_size->y == 8U);
    CHECK(manifest.compute_thread_group_size->z == 1U);

    const auto find_resource = [&](const ShaderRegisterClass register_class, const unsigned binding) {
        return std::find_if(
            manifest.resources.begin(),
            manifest.resources.end(),
            [&](const ShaderResourceBindingDesc& resource) {
                return resource.register_class == register_class &&
                    resource.binding == binding && resource.space == 0U;
            }
        );
    };

    const auto constants = find_resource(ShaderRegisterClass::constant_buffer, 0U);
    CHECK(constants != manifest.resources.end());
    CHECK(constants->kind == ShaderResourceKind::generic);

    const auto tlas = find_resource(ShaderRegisterClass::shader_resource, 0U);
    CHECK(tlas != manifest.resources.end());
    CHECK(tlas->kind == ShaderResourceKind::acceleration_structure);

    const auto depth = find_resource(ShaderRegisterClass::shader_resource, 1U);
    CHECK(depth != manifest.resources.end());
    CHECK(depth->kind == ShaderResourceKind::generic);

    const auto output = find_resource(ShaderRegisterClass::unordered_access, 0U);
    CHECK(output != manifest.resources.end());
    CHECK(output->kind == ShaderResourceKind::generic);

#ifdef VICE_RAY_AMBIENT_OCCLUSION_SOURCE
    const std::string source = read_text_file(VICE_RAY_AMBIENT_OCCLUSION_SOURCE);
    CHECK(!source.empty());
    CHECK(source.find("vice_ao_preferred_neighbor(") != std::string::npos);
    CHECK(source.find("abs(negative_depth - center_depth)") != std::string::npos);
    CHECK(source.find("cross(tangent_y, tangent_x)") != std::string::npos);
    CHECK(source.find("vice_ao_hemisphere_direction(normal, pixel)") != std::string::npos);
    CHECK(source.find("ray.TMax = radius") != std::string::npos);
    CHECK(source.find("COMMITTED_TRIANGLE_HIT ? 0.0 : 1.0") != std::string::npos);
#endif

#ifdef VICE_GPU_PBR_SURFACE_SOURCE
    const std::string gpu_pbr_source = read_text_file(VICE_GPU_PBR_SURFACE_SOURCE);
    CHECK(!gpu_pbr_source.empty());
    CHECK(gpu_pbr_source.find("register(t25, space2)") != std::string::npos);
    CHECK(gpu_pbr_source.find("ambient_occlusion_metadata.x < 0.5") != std::string::npos);
    CHECK(gpu_pbr_source.find("environment *= ray_ambient_occlusion") != std::string::npos);
    CHECK(gpu_pbr_source.find("0.035 * occlusion * ray_ambient_occlusion") != std::string::npos);
    CHECK(gpu_pbr_source.find("ray_ambient_occlusion * evaluate_light") == std::string::npos);
#endif

#ifdef VICE_PROJECT_SURFACE_SOURCE
    const std::string project_source = read_text_file(VICE_PROJECT_SURFACE_SOURCE);
    CHECK(!project_source.empty());
    CHECK(project_source.find("register(t17, space0)") != std::string::npos);
    CHECK(project_source.find("ambient_occlusion_metadata.x < 0.5") != std::string::npos);
    CHECK(project_source.find("albedo * 0.035 * ray_ambient_occlusion") != std::string::npos);
    CHECK(project_source.find("ray_ambient_occlusion * evaluate_light") == std::string::npos);
#endif

#ifdef VICE_RUNTIME_SCENE_LEGACY_SOURCE
    const std::string legacy_source = read_text_file(VICE_RUNTIME_SCENE_LEGACY_SOURCE);
    CHECK(!legacy_source.empty());
    CHECK(legacy_source.find("float ambient_occlusion_metadata[4]{}") != std::string::npos);
    CHECK(legacy_source.find("sizeof(SceneConstants) == 2272U") != std::string::npos);
    CHECK(legacy_source.find("sizeof(GpuPbrSceneConstants) == 2208U") != std::string::npos);
#endif

    return 0;
#endif
}

#undef CHECK

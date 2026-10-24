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
#ifndef VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_MANIFEST
    return 0;
#else
    using namespace vice::render;

    const std::string json = read_text_file(VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_MANIFEST);
    CHECK(!json.empty());
    const CookedShaderManifest manifest = parse_cooked_shader_manifest(json);
    CHECK(manifest.valid);
    CHECK(manifest.stage == ShaderStage::compute);
    CHECK(manifest.entry_point == "vice_ray_ambient_occlusion_temporal_cs");
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

    CHECK(find_resource(ShaderRegisterClass::constant_buffer, 0U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::shader_resource, 0U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::shader_resource, 1U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::shader_resource, 2U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::shader_resource, 3U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::shader_resource, 4U) != manifest.resources.end());
    CHECK(find_resource(ShaderRegisterClass::unordered_access, 0U) != manifest.resources.end());

#ifdef VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_SOURCE
    const std::string source = read_text_file(VICE_RAY_AMBIENT_OCCLUSION_TEMPORAL_SOURCE);
    CHECK(!source.empty());
    CHECK(source.find("history_uv = uv - velocity") != std::string::npos);
    CHECK(source.find("previous_z >= VICE_AO_BACKGROUND_DEPTH") != std::string::npos);
    CHECK(source.find("neighborhood_min") != std::string::npos);
    CHECK(source.find("history = clamp(history, neighborhood_min, neighborhood_max)") !=
        std::string::npos);
    CHECK(source.find("length(velocity) * rejection_parameters.y") != std::string::npos);
    CHECK(source.find("resolved_visibility[pixel] = 1.0") != std::string::npos);
#endif

    return 0;
#endif
}

#undef CHECK

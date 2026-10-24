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

int main() {
#ifndef VICE_DIRECTIONAL_RAY_SHADOW_MANIFEST
    return 0;
#else
    using namespace vice::render;

    std::ifstream input{VICE_DIRECTIONAL_RAY_SHADOW_MANIFEST, std::ios::binary};
    CHECK(input.good());
    const std::string json{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
    const CookedShaderManifest manifest = parse_cooked_shader_manifest(json);
    CHECK(manifest.valid);
    CHECK(manifest.stage == ShaderStage::compute);
    CHECK(manifest.entry_point == "vice_directional_ray_shadow_cs");
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

    return 0;
#endif
}

#undef CHECK
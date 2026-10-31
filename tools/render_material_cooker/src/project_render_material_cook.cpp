#include <vice/tools/project_render_material_cook.hpp>

#include <vice/tools/render_material_cooker.hpp>
#include <vice/tools/shader_cooker.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace vice::tools {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{reinterpret_cast<const char*>(value.data()), value.size()};
}

[[nodiscard]] std::optional<std::string> read_text(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        diagnostics = "Could not read render material source `" + path_text(path) + "`.";
        return std::nullopt;
    }
    std::ostringstream contents;
    contents << input.rdbuf();
    if (!input.eof() && input.fail()) {
        diagnostics = "Could not finish reading render material source `" + path_text(path) + "`.";
        return std::nullopt;
    }
    return contents.str();
}

[[nodiscard]] bool within_root(
    const std::filesystem::path& root,
    const std::filesystem::path& candidate
) {
    std::error_code error;
    const auto relative = std::filesystem::relative(candidate, root, error);
    if (error || relative.empty()) return false;
    for (const auto& part : relative) {
        if (part == "..") return false;
    }
    return true;
}

[[nodiscard]] shader_cooker::Toolchain configured_toolchain() {
    shader_cooker::Toolchain toolchain;
#if defined(VICE_PROJECT_DXC_EXECUTABLE)
    toolchain.dxc = std::filesystem::path{VICE_PROJECT_DXC_EXECUTABLE};
#endif
#if defined(VICE_PROJECT_METAL_SHADER_CONVERTER_EXECUTABLE)
    toolchain.metal_shader_converter = std::filesystem::path{
        VICE_PROJECT_METAL_SHADER_CONVERTER_EXECUTABLE
    };
#endif
    return toolchain;
}

[[nodiscard]] constexpr shader_cooker::CookTarget platform_target() noexcept {
#if defined(_WIN32)
    return shader_cooker::CookTarget::direct3d12;
#elif defined(__APPLE__)
    return shader_cooker::CookTarget::metal;
#else
#error VICE project shader cooking requires a supported render platform.
#endif
}

[[nodiscard]] bool has_binding(
    const shader_cooker::ShaderReflection& reflection,
    const shader_cooker::ShaderRegisterClass register_class,
    const std::uint32_t binding,
    const std::uint32_t space = 0U
) noexcept {
    return std::any_of(
        reflection.resources.begin(),
        reflection.resources.end(),
        [=](const shader_cooker::ShaderResourceReflection& resource) {
            return resource.register_class == register_class && resource.binding == binding &&
                resource.space == space && resource.count == 1U && !resource.unbounded;
        }
    );
}

[[nodiscard]] bool has_unbounded_binding(
    const shader_cooker::ShaderReflection& reflection,
    const shader_cooker::ShaderRegisterClass register_class,
    const std::uint32_t binding,
    const std::uint32_t space
) noexcept {
    return std::any_of(
        reflection.resources.begin(),
        reflection.resources.end(),
        [=](const shader_cooker::ShaderResourceReflection& resource) {
            return resource.register_class == register_class && resource.binding == binding &&
                resource.space == space && resource.count == 0U && resource.unbounded;
        }
    );
}

[[nodiscard]] bool supported_v1_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    return reflection.valid && std::all_of(
        reflection.resources.begin(),
        reflection.resources.end(),
        [](const shader_cooker::ShaderResourceReflection& resource) {
            return resource.register_class == shader_cooker::ShaderRegisterClass::constant_buffer &&
                resource.space == 0U && (resource.binding == 0U || resource.binding == 1U) &&
                resource.count == 1U && !resource.unbounded;
        }
    );
}

[[nodiscard]] bool supported_v2_vertex_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    return supported_v1_reflection(reflection);
}

[[nodiscard]] bool supported_textured_fragment_reflection(
    const shader_cooker::ShaderReflection& reflection,
    const std::uint32_t material_texture_count,
    const std::uint32_t shadow_texture_base,
    const bool allow_environment = false,
    const bool allow_point_shadows = false
) noexcept {
    if (!reflection.valid) return false;
    std::array<bool, 4U> shadows{};
    std::array<bool, 12U> point_shadows{};
    bool shadow_sampler = false;
    std::array<bool, 9U> environment{};
    bool environment_sampler = false;
    for (const auto& resource : reflection.resources) {
        if (resource.space != 0U || resource.count != 1U || resource.unbounded) return false;
        if (resource.register_class == shader_cooker::ShaderRegisterClass::constant_buffer) {
            if (resource.binding != 0U && resource.binding != 1U) return false;
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::shader_resource) {
            if (resource.binding < material_texture_count || resource.binding == 17U) continue;
            if (resource.binding >= shadow_texture_base &&
                resource.binding < shadow_texture_base + static_cast<std::uint32_t>(shadows.size())) {
                shadows[resource.binding - shadow_texture_base] = true;
            } else if (allow_point_shadows && resource.binding >= 5U && resource.binding <= 16U) {
                point_shadows[resource.binding - 5U] = true;
            } else if (allow_environment && resource.binding >= 8U && resource.binding <= 16U) {
                environment[resource.binding - 8U] = true;
            } else {
                return false;
            }
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::sampler) {
            if (resource.binding == 0U) continue;
            if (resource.binding == 1U) shadow_sampler = true;
            else if (allow_environment && resource.binding == 2U) environment_sampler = true;
            else return false;
        } else {
            return false;
        }
    }
    const bool any_shadow = std::any_of(shadows.begin(), shadows.end(), [](const bool value) { return value; });
    const bool all_shadows = std::all_of(shadows.begin(), shadows.end(), [](const bool value) { return value; });
    const bool legacy_shadow = shadows[0] &&
        std::none_of(shadows.begin() + 1, shadows.end(), [](const bool value) { return value; });
    const bool directional_shadow_valid = !any_shadow || legacy_shadow || all_shadows;
    const bool any_point_shadow = std::any_of(
        point_shadows.begin(), point_shadows.end(), [](const bool value) { return value; }
    );
    const bool all_point_shadows = std::all_of(
        point_shadows.begin(), point_shadows.end(), [](const bool value) { return value; }
    );
    const bool point_shadow_valid = !any_point_shadow || all_point_shadows;
    const bool shadow_sampler_required = any_shadow || any_point_shadow;
    const bool shadow_valid = directional_shadow_valid && point_shadow_valid &&
        shadow_sampler == shadow_sampler_required;
    const bool any_environment = std::any_of(
        environment.begin(), environment.end(), [](const bool value) { return value; }
    );
    const bool primary_environment = std::all_of(
        environment.begin(), environment.begin() + 3, [](const bool value) { return value; }
    );
    const bool all_additional_environment = std::all_of(
        environment.begin() + 3, environment.end(), [](const bool value) { return value; }
    );
    const bool no_additional_environment = std::none_of(
        environment.begin() + 3, environment.end(), [](const bool value) { return value; }
    );
    const bool environment_valid = (!any_environment && !environment_sampler) ||
        (primary_environment && no_additional_environment && environment_sampler) ||
        (primary_environment && all_additional_environment && environment_sampler);
    return shadow_valid && environment_valid;
}

[[nodiscard]] bool supported_v2_fragment_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    return supported_textured_fragment_reflection(reflection, 1U, 1U, false, true);
}

[[nodiscard]] bool supported_v3_fragment_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    return supported_textured_fragment_reflection(reflection, 4U, 4U, true, false);
}

[[nodiscard]] bool supported_v4_vertex_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    if (!reflection.valid) return false;
    for (const auto& resource : reflection.resources) {
        if (resource.register_class == shader_cooker::ShaderRegisterClass::constant_buffer) {
            if (resource.space != 0U || resource.binding != 0U || resource.count != 1U || resource.unbounded) {
                return false;
            }
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::shader_resource) {
            if (resource.space != 0U || resource.binding > 1U || resource.count != 1U || resource.unbounded) {
                return false;
            }
        } else {
            return false;
        }
    }
    return has_binding(
        reflection, shader_cooker::ShaderRegisterClass::constant_buffer, 0U, 0U
    ) && has_binding(
        reflection, shader_cooker::ShaderRegisterClass::shader_resource, 0U, 0U
    ) && has_binding(
        reflection, shader_cooker::ShaderRegisterClass::shader_resource, 1U, 0U
    );
}

[[nodiscard]] bool supported_v4_fragment_reflection(
    const shader_cooker::ShaderReflection& reflection
) noexcept {
    if (!reflection.valid) return false;
    for (const auto& resource : reflection.resources) {
        if (resource.register_class == shader_cooker::ShaderRegisterClass::constant_buffer) {
            if (resource.space != 0U || resource.binding != 0U || resource.count != 1U || resource.unbounded) {
                return false;
            }
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::shader_resource) {
            const bool scene_table = resource.space == 0U &&
                (resource.binding == 2U || resource.binding == 3U) &&
                resource.count == 1U && !resource.unbounded;
            const bool material_textures = resource.space == 1U && resource.binding == 0U &&
                resource.count == 0U && resource.unbounded;
            const bool lighting = resource.space == 2U && resource.binding <= 27U &&
                resource.count == 1U && !resource.unbounded;
            if (!scene_table && !material_textures && !lighting) return false;
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::sampler) {
            const bool material_samplers = resource.space == 1U && resource.binding == 0U &&
                resource.count == 0U && resource.unbounded;
            const bool lighting_samplers = resource.space == 2U && resource.binding <= 1U &&
                resource.count == 1U && !resource.unbounded;
            if (!material_samplers && !lighting_samplers) return false;
        } else if (resource.register_class == shader_cooker::ShaderRegisterClass::unordered_access) {
            // The production shader records virtual-texture requests through this
            // optional feedback UAV. The runtime always supplies its bounded buffer.
            if (resource.space != 3U || resource.binding != 0U || resource.count != 1U || resource.unbounded) {
                return false;
            }
        } else {
            return false;
        }
    }

    bool required = has_binding(
        reflection, shader_cooker::ShaderRegisterClass::constant_buffer, 0U, 0U
    ) && has_binding(
        reflection, shader_cooker::ShaderRegisterClass::shader_resource, 2U, 0U
    ) && has_binding(
        reflection, shader_cooker::ShaderRegisterClass::shader_resource, 3U, 0U
    ) && has_unbounded_binding(
        reflection, shader_cooker::ShaderRegisterClass::shader_resource, 0U, 1U
    ) && has_unbounded_binding(
        reflection, shader_cooker::ShaderRegisterClass::sampler, 0U, 1U
    );
    for (std::uint32_t binding = 0U; binding <= 24U; ++binding) {
        required = required && has_binding(
            reflection, shader_cooker::ShaderRegisterClass::shader_resource, binding, 2U
        );
    }
    for (std::uint32_t binding = 0U; binding <= 1U; ++binding) {
        required = required && has_binding(
            reflection, shader_cooker::ShaderRegisterClass::sampler, binding, 2U
        );
    }
    return required;
}

[[nodiscard]] bool texture_available(
    const render::TextureStableId& texture,
    const std::span<const world::ResourceId> available
) noexcept {
    const world::ResourceId id{world::PersistentId{texture}};
    return std::find(available.begin(), available.end(), id) != available.end();
}

[[nodiscard]] std::string shader_failure(
    const std::filesystem::path& material,
    const std::string_view stage,
    const shader_cooker::CookResult& cooked
) {
    return "Could not cook " + std::string{stage} + " shader for render material `" +
        path_text(material) + "`: " + cooked.diagnostics;
}

} // namespace

ProjectRenderMaterialCookResult cook_project_render_materials(
    const project::Workspace& workspace,
    const std::span<const world::ResourceId> available_textures
) {
    ProjectRenderMaterialCookResult result;
    const auto& source_root = workspace.asset_root;
    const auto& cooked_root = workspace.cooked_root;

    std::error_code error;
    if (source_root.empty() || !std::filesystem::is_directory(source_root, error)) {
        result.diagnostics = error
            ? "Could not inspect project asset root: " + error.message()
            : "Project asset root is not available.";
        return result;
    }

    std::filesystem::recursive_directory_iterator iterator{
        source_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        result.diagnostics = "Could not enumerate project render materials: " + error.message();
        return result;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            result.diagnostics = "Could not enumerate project render materials: " + error.message();
            return result;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmat") continue;

        const auto relative = std::filesystem::relative(iterator->path(), source_root, error);
        if (error) {
            result.diagnostics = "Could not resolve project render material path: " + error.message();
            return result;
        }

        std::string diagnostics;
        const auto source_text = read_text(iterator->path(), diagnostics);
        if (!source_text.has_value()) {
            result.diagnostics = std::move(diagnostics);
            return result;
        }
        const auto parsed = render_material_cooker::parse_source_manifest(*source_text);
        if (!parsed.source.has_value()) {
            result.diagnostics = "Could not parse project render material `" + path_text(iterator->path()) +
                "`: " + parsed.diagnostics;
            return result;
        }
        const bool textured = parsed.source->version >=
            render_material_cooker::render_material_source_format_version_v2;
        const bool extended_pbr = parsed.source->version >=
            render_material_cooker::render_material_source_format_version_v3;
        const bool gpu_scene_pbr = parsed.source->version ==
            render_material_cooker::render_material_source_format_version_v4;
        if (textured && !texture_available(parsed.source->base_color_texture, available_textures)) {
            result.diagnostics = "Render material `" + path_text(iterator->path()) +
                "` references a base-color texture that this project did not cook.";
            return result;
        }
        if (extended_pbr &&
            (!texture_available(parsed.source->normal_texture, available_textures) ||
                !texture_available(parsed.source->orm_texture, available_textures) ||
                !texture_available(parsed.source->emissive_texture, available_textures))) {
            result.diagnostics = "Render material `" + path_text(iterator->path()) +
                "` references a normal, ORM, or emissive texture that this project did not cook.";
            return result;
        }

        const auto shader_source = (source_root / parsed.source->shader_source).lexically_normal();
        if (!within_root(source_root, shader_source) ||
            !std::filesystem::is_regular_file(shader_source, error) || error) {
            result.diagnostics = error
                ? "Could not inspect render material shader `" + path_text(shader_source) + "`: " + error.message()
                : "Render material shader is not a project asset: " + path_text(shader_source);
            return result;
        }

        auto cooked_relative = relative;
        cooked_relative.replace_extension(".vmatc");
        const auto cooked_path = (cooked_root / cooked_relative).lexically_normal();
        const auto shader_directory_name = iterator->path().stem().string() + ".shaders";
        const auto shader_root = cooked_path.parent_path() / shader_directory_name;
        const auto vertex_root = shader_root / "vertex";
        const auto fragment_root = shader_root / "fragment";

        shader_cooker::CookRequest vertex_request;
        vertex_request.input = shader_source;
        vertex_request.output_directory = vertex_root;
        vertex_request.entry_point = parsed.source->vertex_entry;
        vertex_request.stage = shader_cooker::ShaderStage::vertex;
        vertex_request.target = platform_target();
        if (gpu_scene_pbr) vertex_request.shader_model = "6.6";
        vertex_request.toolchain = configured_toolchain();
        const auto vertex = shader_cooker::cook(vertex_request);
        if (!vertex.success) {
            result.diagnostics = shader_failure(iterator->path(), "vertex", vertex);
            return result;
        }
        const bool vertex_supported = gpu_scene_pbr
            ? supported_v4_vertex_reflection(vertex.reflection)
            : textured ? supported_v2_vertex_reflection(vertex.reflection)
                       : supported_v1_reflection(vertex.reflection);
        if (!vertex_supported) {
            result.diagnostics = gpu_scene_pbr
                ? "Render material v4 vertex shader `" + path_text(shader_source) +
                    "` must use b0 SceneFrame plus t0 scene models and t1 visible-instance indices in space0."
                : "Render material vertex shader `" + path_text(shader_source) +
                    "` must use the VICE scene constant buffer at b0, space0 without unsupported resources.";
            return result;
        }

        shader_cooker::CookRequest fragment_request;
        fragment_request.input = shader_source;
        fragment_request.output_directory = fragment_root;
        fragment_request.entry_point = parsed.source->fragment_entry;
        fragment_request.stage = shader_cooker::ShaderStage::fragment;
        fragment_request.target = platform_target();
        if (gpu_scene_pbr) fragment_request.shader_model = "6.6";
        fragment_request.toolchain = configured_toolchain();
        const auto fragment = shader_cooker::cook(fragment_request);
        if (!fragment.success) {
            result.diagnostics = shader_failure(iterator->path(), "fragment", fragment);
            return result;
        }

        if (gpu_scene_pbr) {
            if (!supported_v4_fragment_reflection(fragment.reflection)) {
                result.diagnostics = "Render material v4 fragment shader `" + path_text(shader_source) +
                    "` must use t2 instance materials and t3 material records in space0, unbounded t0/s0 material arrays in space1, and the complete bounded t0-t24/s0-s1 lighting set in space2, optional t25-t27 ray-lighting inputs in space2, and optional u0 virtual-texture feedback in space3.";
                return result;
            }
        } else {
            const bool fragment_supported = extended_pbr
                ? supported_v3_fragment_reflection(fragment.reflection)
                : textured ? supported_v2_fragment_reflection(fragment.reflection)
                           : supported_v1_reflection(fragment.reflection);
            bool fragment_required = has_binding(
                fragment.reflection,
                shader_cooker::ShaderRegisterClass::constant_buffer,
                1U
            );
            if (textured) {
                fragment_required = fragment_required && has_binding(
                    fragment.reflection, shader_cooker::ShaderRegisterClass::shader_resource, 0U
                ) && has_binding(
                    fragment.reflection, shader_cooker::ShaderRegisterClass::sampler, 0U
                );
            }
            if (extended_pbr) {
                for (std::uint32_t binding = 1U; binding <= 3U; ++binding) {
                    fragment_required = fragment_required && has_binding(
                        fragment.reflection, shader_cooker::ShaderRegisterClass::shader_resource, binding
                    );
                }
            }
            if (!fragment_supported || !fragment_required) {
                result.diagnostics = extended_pbr
                    ? "Render material v3 fragment shader `" + path_text(shader_source) +
                        "` must use b1 material constants, t0-t3 PBR textures, and s0 sampler in space0; "
                        "optional IBL must provide the complete t8-t10/s2 set."
                    : textured
                        ? "Render material v2 fragment shader `" + path_text(shader_source) +
                            "` must use b1 material constants, t0 base-color texture, and s0 sampler in space0; "
                            "optional directional shadows use t1-t4/s1 and optional point shadows require the complete t5-t16/s1 set."
                        : "Render material fragment shader `" + path_text(shader_source) +
                        "` must use the VICE material constant buffer at b1, space0.";
                return result;
            }
        }

        const auto vertex_relative = std::filesystem::relative(vertex.manifest_path, cooked_path.parent_path(), error);
        if (error) {
            result.diagnostics = "Could not resolve cooked vertex shader manifest path: " + error.message();
            return result;
        }
        const auto fragment_relative = std::filesystem::relative(fragment.manifest_path, cooked_path.parent_path(), error);
        if (error) {
            result.diagnostics = "Could not resolve cooked fragment shader manifest path: " + error.message();
            return result;
        }

        const auto material = render_material_cooker::cook(
            *parsed.source,
            vertex_relative.generic_string(),
            fragment_relative.generic_string()
        );
        if (!material.success) {
            result.diagnostics = "Could not cook project render material `" + path_text(iterator->path()) +
                "`: " + material.diagnostics;
            return result;
        }
        if (!render_material_cooker::write_asset(cooked_path, material.bytes, &diagnostics)) {
            result.diagnostics = "Could not write cooked project render material `" + path_text(cooked_path) +
                "`: " + diagnostics;
            return result;
        }

        result.materials.push_back(ProjectRenderMaterialCookRecord{
            .relative_path = std::move(cooked_relative),
            .cooked_path = cooked_path,
            .shader_source = shader_source,
            .vertex_manifest = vertex.manifest_path,
            .fragment_manifest = fragment.manifest_path,
        });
    }

    result.success = true;
    return result;
}

} // namespace vice::tools

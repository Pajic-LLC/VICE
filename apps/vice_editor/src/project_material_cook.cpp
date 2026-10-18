#include <vice/editor/project_material_cook.hpp>

#include <vice/tools/render_material_cooker.hpp>
#include <vice/tools/shader_cooker.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

namespace vice::editor {
namespace {

[[nodiscard]] std::string path_text(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string{
        reinterpret_cast<const char*>(value.data()),
        value.size()
    };
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

[[nodiscard]] tools::shader_cooker::Toolchain configured_toolchain() {
    tools::shader_cooker::Toolchain toolchain;
#if defined(VICE_EDITOR_DXC_EXECUTABLE)
    toolchain.dxc = std::filesystem::path{VICE_EDITOR_DXC_EXECUTABLE};
#endif
#if defined(VICE_EDITOR_METAL_SHADER_CONVERTER_EXECUTABLE)
    toolchain.metal_shader_converter = std::filesystem::path{
        VICE_EDITOR_METAL_SHADER_CONVERTER_EXECUTABLE
    };
#endif
    return toolchain;
}

[[nodiscard]] constexpr tools::shader_cooker::CookTarget platform_target() noexcept {
#if defined(_WIN32)
    return tools::shader_cooker::CookTarget::direct3d12;
#elif defined(__APPLE__)
    return tools::shader_cooker::CookTarget::metal;
#else
#error VICE project shader cooking requires a supported render platform.
#endif
}

[[nodiscard]] bool has_binding(
    const tools::shader_cooker::ShaderReflection& reflection,
    const tools::shader_cooker::ShaderRegisterClass register_class,
    const std::uint32_t binding,
    const std::uint32_t space = 0U
) noexcept {
    return std::any_of(
        reflection.resources.begin(),
        reflection.resources.end(),
        [=](const tools::shader_cooker::ShaderResourceReflection& resource) {
            return resource.register_class == register_class && resource.binding == binding &&
                resource.space == space && resource.count == 1U && !resource.unbounded;
        }
    );
}

[[nodiscard]] bool supported_material_reflection(
    const tools::shader_cooker::ShaderReflection& reflection
) noexcept {
    return reflection.valid && std::all_of(
        reflection.resources.begin(),
        reflection.resources.end(),
        [](const tools::shader_cooker::ShaderResourceReflection& resource) {
            return resource.register_class == tools::shader_cooker::ShaderRegisterClass::constant_buffer &&
                resource.space == 0U && (resource.binding == 0U || resource.binding == 1U) &&
                resource.count == 1U && !resource.unbounded;
        }
    );
}

[[nodiscard]] std::string shader_failure(
    const std::filesystem::path& material,
    const std::string_view stage,
    const tools::shader_cooker::CookResult& cooked
) {
    return "Could not cook " + std::string{stage} + " shader for render material `" +
        path_text(material) + "`: " + cooked.diagnostics;
}

} // namespace

ProjectMaterialCookResult cook_project_render_materials(const ProjectSession& session) {
    ProjectMaterialCookResult result;
    const auto& source_root = session.asset_root();
    const auto& cooked_root = session.cooked_root();

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
        const auto parsed = tools::render_material_cooker::parse_source_manifest(*source_text);
        if (!parsed.source.has_value()) {
            result.diagnostics = "Could not parse project render material `" + path_text(iterator->path()) +
                "`: " + parsed.diagnostics;
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

        tools::shader_cooker::CookRequest vertex_request;
        vertex_request.input = shader_source;
        vertex_request.output_directory = vertex_root;
        vertex_request.entry_point = parsed.source->vertex_entry;
        vertex_request.stage = tools::shader_cooker::ShaderStage::vertex;
        vertex_request.target = platform_target();
        vertex_request.toolchain = configured_toolchain();
        const auto vertex = tools::shader_cooker::cook(vertex_request);
        if (!vertex.success) {
            result.diagnostics = shader_failure(iterator->path(), "vertex", vertex);
            return result;
        }
        if (!supported_material_reflection(vertex.reflection) ||
            !has_binding(
                vertex.reflection,
                tools::shader_cooker::ShaderRegisterClass::constant_buffer,
                0U
            )) {
            result.diagnostics = "Render material vertex shader `" + path_text(shader_source) +
                "` must use the VICE scene constant buffer at b0, space0 and may only use b0/b1 constant buffers.";
            return result;
        }

        tools::shader_cooker::CookRequest fragment_request;
        fragment_request.input = shader_source;
        fragment_request.output_directory = fragment_root;
        fragment_request.entry_point = parsed.source->fragment_entry;
        fragment_request.stage = tools::shader_cooker::ShaderStage::fragment;
        fragment_request.target = platform_target();
        fragment_request.toolchain = configured_toolchain();
        const auto fragment = tools::shader_cooker::cook(fragment_request);
        if (!fragment.success) {
            result.diagnostics = shader_failure(iterator->path(), "fragment", fragment);
            return result;
        }
        if (!supported_material_reflection(fragment.reflection) ||
            !has_binding(
                fragment.reflection,
                tools::shader_cooker::ShaderRegisterClass::constant_buffer,
                1U
            )) {
            result.diagnostics = "Render material fragment shader `" + path_text(shader_source) +
                "` must use the VICE material constant buffer at b1, space0 and may only use b0/b1 constant buffers.";
            return result;
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

        const auto material = tools::render_material_cooker::cook(
            *parsed.source,
            vertex_relative.generic_string(),
            fragment_relative.generic_string()
        );
        if (!material.success) {
            result.diagnostics = "Could not cook project render material `" + path_text(iterator->path()) +
                "`: " + material.diagnostics;
            return result;
        }
        if (!tools::render_material_cooker::write_asset(cooked_path, material.bytes, &diagnostics)) {
            result.diagnostics = "Could not write cooked project render material `" + path_text(cooked_path) +
                "`: " + diagnostics;
            return result;
        }

        result.materials.push_back(CookedProjectRenderMaterial{
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

} // namespace vice::editor

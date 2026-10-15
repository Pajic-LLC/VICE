#include <vice/render/pipeline_layout.hpp>

#include <algorithm>
#include <limits>
#include <sstream>

namespace vice::render {
namespace {

[[nodiscard]] const char* register_prefix(const ShaderRegisterClass register_class) noexcept {
    switch (register_class) {
    case ShaderRegisterClass::constant_buffer: return "b";
    case ShaderRegisterClass::shader_resource: return "t";
    case ShaderRegisterClass::unordered_access: return "u";
    case ShaderRegisterClass::sampler: return "s";
    }
    return "?";
}

[[nodiscard]] std::string binding_name(const PipelineResourceBinding& binding) {
    std::ostringstream name;
    name << register_prefix(binding.register_class) << binding.binding;
    if (binding.space != 0U) {
        name << ", space" << binding.space;
    }
    return name.str();
}

[[nodiscard]] bool same_identity(
    const PipelineResourceBinding& left,
    const ShaderResourceBindingDesc& right
) noexcept {
    return left.register_class == right.register_class &&
        left.binding == right.binding &&
        left.space == right.space;
}

[[nodiscard]] bool same_shape(
    const PipelineResourceBinding& left,
    const ShaderResourceBindingDesc& right
) noexcept {
    return left.count == right.count && left.unbounded == right.unbounded;
}

[[nodiscard]] bool add_resources(
    PipelineLayout& layout,
    const std::span<const ShaderResourceBindingDesc> resources,
    const ShaderStageMask stage
) {
    for (const auto& resource : resources) {
        if (!is_valid(resource)) {
            layout.diagnostics = "Shader reflection contains an invalid resource binding.";
            return false;
        }

        const auto existing = std::find_if(
            layout.resources.begin(),
            layout.resources.end(),
            [&](const PipelineResourceBinding& binding) {
                return same_identity(binding, resource);
            }
        );

        if (existing != layout.resources.end()) {
            if (!same_shape(*existing, resource)) {
                layout.diagnostics =
                    "Shader stages disagree about the shape of " + binding_name(*existing) + ".";
                return false;
            }
            existing->stages |= stage;
            continue;
        }

        layout.resources.push_back(PipelineResourceBinding{
            .register_class = resource.register_class,
            .binding = resource.binding,
            .space = resource.space,
            .count = resource.count,
            .unbounded = resource.unbounded,
            .stages = stage,
        });
    }
    return true;
}

[[nodiscard]] bool binding_less(
    const PipelineResourceBinding& left,
    const PipelineResourceBinding& right
) noexcept {
    if (left.register_class != right.register_class) {
        return left.register_class < right.register_class;
    }
    if (left.space != right.space) {
        return left.space < right.space;
    }
    return left.binding < right.binding;
}

[[nodiscard]] bool ranges_overlap(
    const PipelineResourceBinding& left,
    const PipelineResourceBinding& right
) noexcept {
    if (left.register_class != right.register_class || left.space != right.space) {
        return false;
    }
    if (left.unbounded) {
        return true;
    }
    const std::uint64_t left_end =
        static_cast<std::uint64_t>(left.binding) + static_cast<std::uint64_t>(left.count);
    return left_end > static_cast<std::uint64_t>(right.binding);
}

} // namespace

PipelineLayout compile_graphics_pipeline_layout(
    const std::span<const ShaderResourceBindingDesc> vertex_resources,
    const std::span<const ShaderResourceBindingDesc> fragment_resources
) {
    PipelineLayout layout;
    layout.resources.reserve(vertex_resources.size() + fragment_resources.size());

    if (!add_resources(layout, vertex_resources, ShaderStageMask::vertex) ||
        !add_resources(layout, fragment_resources, ShaderStageMask::fragment)) {
        return layout;
    }

    std::sort(layout.resources.begin(), layout.resources.end(), binding_less);

    for (std::size_t index = 1; index < layout.resources.size(); ++index) {
        const auto& previous = layout.resources[index - 1U];
        const auto& current = layout.resources[index];
        if (ranges_overlap(previous, current)) {
            layout.diagnostics =
                "Shader resource ranges overlap at " + binding_name(current) + ".";
            return layout;
        }
    }

    layout.valid = true;
    return layout;
}

} // namespace vice::render

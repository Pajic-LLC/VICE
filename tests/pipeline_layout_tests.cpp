#include <vice/render/pipeline_layout.hpp>

#include <array>
#include <cassert>

int main() {
    using namespace vice::render;

    constexpr ShaderResourceBindingDesc frame_data{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 2,
        .space = 1,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc texture_array{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 4,
        .space = 2,
        .count = 6,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc sampler{
        .register_class = ShaderRegisterClass::sampler,
        .binding = 3,
        .space = 1,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc bindless_textures{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 32,
        .space = 5,
        .count = 0,
        .unbounded = true,
    };

    constexpr std::array vertex_resources{
        texture_array,
        frame_data,
        bindless_textures,
    };
    constexpr std::array fragment_resources{
        sampler,
        frame_data,
        texture_array,
    };

    const PipelineLayout layout = compile_graphics_pipeline_layout(
        vertex_resources,
        fragment_resources
    );
    assert(layout.valid);
    assert(layout.diagnostics.empty());
    assert(layout.resources.size() == 4U);

    assert(layout.resources[0].register_class == ShaderRegisterClass::constant_buffer);
    assert(layout.resources[0].binding == 2U);
    assert(layout.resources[0].space == 1U);
    assert(has_stage(layout.resources[0].stages, ShaderStageMask::vertex));
    assert(has_stage(layout.resources[0].stages, ShaderStageMask::fragment));

    assert(layout.resources[1].register_class == ShaderRegisterClass::shader_resource);
    assert(layout.resources[1].binding == 4U);
    assert(layout.resources[1].space == 2U);
    assert(layout.resources[1].count == 6U);
    assert(has_stage(layout.resources[1].stages, ShaderStageMask::vertex));
    assert(has_stage(layout.resources[1].stages, ShaderStageMask::fragment));

    assert(layout.resources[2].register_class == ShaderRegisterClass::shader_resource);
    assert(layout.resources[2].binding == 32U);
    assert(layout.resources[2].space == 5U);
    assert(layout.resources[2].unbounded);
    assert(layout.resources[2].stages == ShaderStageMask::vertex);

    assert(layout.resources[3].register_class == ShaderRegisterClass::sampler);
    assert(layout.resources[3].binding == 3U);
    assert(layout.resources[3].space == 1U);
    assert(layout.resources[3].stages == ShaderStageMask::fragment);

    constexpr ShaderResourceBindingDesc conflicting_frame_data{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 2,
        .space = 1,
        .count = 2,
        .unbounded = false,
    };
    constexpr std::array vertex_frame{frame_data};
    constexpr std::array conflicting_fragment{conflicting_frame_data};
    const PipelineLayout shape_conflict = compile_graphics_pipeline_layout(
        vertex_frame,
        conflicting_fragment
    );
    assert(!shape_conflict.valid);
    assert(!shape_conflict.diagnostics.empty());

    constexpr ShaderResourceBindingDesc first_range{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 0,
        .count = 4,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc overlapping_range{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 3,
        .space = 0,
        .count = 2,
        .unbounded = false,
    };
    constexpr std::array overlapping_vertex{first_range, overlapping_range};
    const PipelineLayout overlap = compile_graphics_pipeline_layout(overlapping_vertex, {});
    assert(!overlap.valid);
    assert(!overlap.diagnostics.empty());

    constexpr ShaderResourceBindingDesc unbounded_range{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 7,
        .space = 3,
        .count = 0,
        .unbounded = true,
    };
    constexpr ShaderResourceBindingDesc after_unbounded{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 12,
        .space = 3,
        .count = 1,
        .unbounded = false,
    };
    constexpr std::array invalid_unbounded{unbounded_range, after_unbounded};
    const PipelineLayout unbounded_overlap = compile_graphics_pipeline_layout(invalid_unbounded, {});
    assert(!unbounded_overlap.valid);

    const PipelineLayout empty = compile_graphics_pipeline_layout({}, {});
    assert(empty.valid);
    assert(empty.resources.empty());

    constexpr ShaderResourceBindingDesc compute_constants{
        .register_class = ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_input{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_output{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    };
    constexpr std::array compute_resources{
        compute_output,
        compute_constants,
        compute_input,
    };

    const PipelineLayout compute = compile_compute_pipeline_layout(compute_resources);
    assert(compute.valid);
    assert(compute.diagnostics.empty());
    assert(compute.resources.size() == 3U);
    for (const auto& resource : compute.resources) {
        assert(resource.stages == ShaderStageMask::compute);
        assert(resource.kind == ShaderResourceKind::generic);
    }

    constexpr ShaderResourceBindingDesc compute_tlas{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 9,
        .space = 4,
        .count = 1,
        .unbounded = false,
        .kind = ShaderResourceKind::acceleration_structure,
    };
    constexpr std::array compute_rt_resources{compute_tlas};
    const PipelineLayout compute_rt = compile_compute_pipeline_layout(compute_rt_resources);
    assert(compute_rt.valid);
    assert(compute_rt.resources.size() == 1U);
    assert(compute_rt.resources[0].kind == ShaderResourceKind::acceleration_structure);
    assert(compute_rt.resources[0].stages == ShaderStageMask::compute);

    constexpr ShaderResourceBindingDesc generic_tlas_slot{
        .register_class = ShaderRegisterClass::shader_resource,
        .binding = 9,
        .space = 4,
        .count = 1,
        .unbounded = false,
        .kind = ShaderResourceKind::generic,
    };
    constexpr std::array rt_vertex{compute_tlas};
    constexpr std::array rt_fragment{generic_tlas_slot};
    const PipelineLayout kind_conflict = compile_graphics_pipeline_layout(rt_vertex, rt_fragment);
    assert(!kind_conflict.valid);
    assert(!kind_conflict.diagnostics.empty());

    constexpr ShaderResourceBindingDesc compute_overlap_a{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 4,
        .space = 2,
        .count = 4,
        .unbounded = false,
    };
    constexpr ShaderResourceBindingDesc compute_overlap_b{
        .register_class = ShaderRegisterClass::unordered_access,
        .binding = 7,
        .space = 2,
        .count = 2,
        .unbounded = false,
    };
    constexpr std::array compute_overlap_resources{
        compute_overlap_a,
        compute_overlap_b,
    };
    const PipelineLayout compute_overlap = compile_compute_pipeline_layout(
        compute_overlap_resources
    );
    assert(!compute_overlap.valid);
    assert(!compute_overlap.diagnostics.empty());

    const PipelineLayout empty_compute = compile_compute_pipeline_layout({});
    assert(empty_compute.valid);
    assert(empty_compute.resources.empty());

    return 0;
}

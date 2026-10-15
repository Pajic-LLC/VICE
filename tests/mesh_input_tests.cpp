#include <vice/render/mesh_input.hpp>

#include <array>
#include <cassert>

int main() {
    using namespace vice::render;

    static_assert(vertex_format_size(VertexFormat::float32) == 4U);
    static_assert(vertex_format_size(VertexFormat::float32x2) == 8U);
    static_assert(vertex_format_size(VertexFormat::float32x3) == 12U);
    static_assert(vertex_format_size(VertexFormat::float32x4) == 16U);
    static_assert(vertex_format_size(VertexFormat::float16x4) == 8U);
    static_assert(vertex_format_size(VertexFormat::unorm8x4) == 4U);
    static_assert(vertex_format_size(VertexFormat::uint16x4) == 8U);
    static_assert(index_format_size(IndexFormat::uint16) == 2U);
    static_assert(index_format_size(IndexFormat::uint32) == 4U);

    static constexpr std::array attributes{
        VertexAttributeDesc{
            .semantic = "POSITION",
            .semantic_index = 0,
            .shader_location = 0,
            .format = VertexFormat::float32x2,
            .offset = 0,
        },
        VertexAttributeDesc{
            .semantic = "COLOR",
            .semantic_index = 0,
            .shader_location = 1,
            .format = VertexFormat::float32x3,
            .offset = 8,
        },
    };
    static constexpr VertexBufferLayoutDesc interleaved{
        .stride = 20,
        .step_mode = VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = attributes,
    };
    static_assert(is_valid(interleaved));

    static constexpr std::array layouts{interleaved};
    static_assert(is_valid(layouts));

    static constexpr std::array duplicate_locations{
        VertexAttributeDesc{
            .semantic = "POSITION",
            .semantic_index = 0,
            .shader_location = 0,
            .format = VertexFormat::float32x2,
            .offset = 0,
        },
        VertexAttributeDesc{
            .semantic = "NORMAL",
            .semantic_index = 0,
            .shader_location = 0,
            .format = VertexFormat::float32x3,
            .offset = 8,
        },
    };
    static constexpr VertexBufferLayoutDesc duplicate_location_layout{
        .stride = 20,
        .step_mode = VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = duplicate_locations,
    };
    static constexpr std::array duplicate_layouts{duplicate_location_layout};
    static_assert(!is_valid(duplicate_layouts));

    static constexpr std::array overflowing_attribute{
        VertexAttributeDesc{
            .semantic = "POSITION",
            .semantic_index = 0,
            .shader_location = 0,
            .format = VertexFormat::float32x4,
            .offset = 8,
        },
    };
    static constexpr VertexBufferLayoutDesc invalid_stride{
        .stride = 20,
        .step_mode = VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = overflowing_attribute,
    };
    static_assert(!is_valid(invalid_stride));

    static constexpr VertexBufferLayoutDesc invalid_vertex_rate{
        .stride = 20,
        .step_mode = VertexStepMode::per_vertex,
        .step_rate = 2,
        .attributes = attributes,
    };
    static_assert(!is_valid(invalid_vertex_rate));

    static constexpr VertexBufferLayoutDesc valid_instance_rate{
        .stride = 20,
        .step_mode = VertexStepMode::per_instance,
        .step_rate = 3,
        .attributes = attributes,
    };
    static_assert(is_valid(valid_instance_rate));

    static constexpr std::array empty_semantic{
        VertexAttributeDesc{
            .semantic = "",
            .semantic_index = 0,
            .shader_location = 0,
            .format = VertexFormat::float32x2,
            .offset = 0,
        },
    };
    static constexpr VertexBufferLayoutDesc invalid_semantic{
        .stride = 8,
        .step_mode = VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = empty_semantic,
    };
    static_assert(!is_valid(invalid_semantic));

    return 0;
}

#include <vice/render/pipeline_layout.hpp>
#include <vice/render/rhi.hpp>

#include <array>
#include <cassert>
#include <span>

int main() {
    using namespace vice::render;

    constexpr Viewport viewport{
        .x = 24.0F,
        .y = 32.0F,
        .width = 1280.0F,
        .height = 720.0F,
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    };
    static_assert(is_valid(viewport));

    constexpr Viewport invalid_width{
        .width = 0.0F,
        .height = 720.0F,
    };
    static_assert(!is_valid(invalid_width));

    constexpr Viewport invalid_depth{
        .width = 640.0F,
        .height = 480.0F,
        .min_depth = 0.8F,
        .max_depth = 0.2F,
    };
    static_assert(!is_valid(invalid_depth));

    constexpr Viewport invalid_depth_range{
        .width = 640.0F,
        .height = 480.0F,
        .min_depth = -0.1F,
        .max_depth = 1.0F,
    };
    static_assert(!is_valid(invalid_depth_range));

    constexpr ScissorRect scissor{
        .x = 100,
        .y = 80,
        .width = 900,
        .height = 600,
    };
    static_assert(is_valid(scissor));

    constexpr ScissorRect invalid_scissor{
        .x = 0,
        .y = 0,
        .width = 0,
        .height = 10,
    };
    static_assert(!is_valid(invalid_scissor));

    constexpr GraphicsPipelineDesc default_pipeline{};
    static_assert(default_pipeline.blend == BlendMode::opaque);

    static constexpr std::array<ShaderResourceBindingDesc, 1> font_resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0,
            .space = 0,
            .count = 1,
            .unbounded = false,
        },
    };

    constexpr ShaderModuleDesc font_fragment{
        .stage = ShaderStage::fragment,
        .language = ShaderSourceLanguage::hlsl,
        .source = "float4 main() : SV_Target { return 1.0.xxxx; }",
        .resources = std::span<const ShaderResourceBindingDesc>{font_resources},
        .entry_point = "main",
        .debug_name = "VICE editor source resource test",
    };
    static_assert(is_valid(font_fragment));

    static constexpr std::array<ShaderResourceBindingDesc, 1> invalid_resources{
        ShaderResourceBindingDesc{
            .register_class = ShaderRegisterClass::shader_resource,
            .binding = 0,
            .space = 0,
            .count = 0,
            .unbounded = false,
        },
    };

    constexpr ShaderModuleDesc invalid_fragment{
        .stage = ShaderStage::fragment,
        .language = ShaderSourceLanguage::metal,
        .source = "fragment float4 main() { return float4(1); }",
        .resources = std::span<const ShaderResourceBindingDesc>{invalid_resources},
        .entry_point = "main",
        .debug_name = "invalid source resource test",
    };
    static_assert(!is_valid(invalid_fragment));

    const PipelineLayout layout = compile_graphics_pipeline_layout({}, font_resources);
    assert(layout.valid);
    assert(layout.resources.size() == 1U);
    assert(layout.resources.front().register_class == ShaderRegisterClass::shader_resource);
    assert(layout.resources.front().binding == 0U);
    assert(layout.resources.front().space == 0U);
    assert(layout.resources.front().count == 1U);
    assert(has_stage(layout.resources.front().stages, ShaderStageMask::fragment));
    assert(!has_stage(layout.resources.front().stages, ShaderStageMask::vertex));

    return 0;
}

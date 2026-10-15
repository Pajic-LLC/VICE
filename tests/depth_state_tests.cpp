#include <vice/render/rhi.hpp>

#include <cassert>

int main() {
    using namespace vice::render;

    constexpr DepthStateDesc disabled{};
    static_assert(is_valid(disabled));
    static_assert(disabled.format == TextureFormat::undefined);
    static_assert(!disabled.test_enabled);
    static_assert(!disabled.write_enabled);

    constexpr DepthStateDesc enabled{
        .format = TextureFormat::depth32_float,
        .test_enabled = true,
        .write_enabled = true,
        .compare = CompareOp::less,
    };
    static_assert(is_valid(enabled));

    constexpr DepthStateDesc invalid_disabled_write{
        .format = TextureFormat::undefined,
        .test_enabled = false,
        .write_enabled = true,
        .compare = CompareOp::always,
    };
    static_assert(!is_valid(invalid_disabled_write));

    constexpr DepthStateDesc invalid_color_depth{
        .format = TextureFormat::rgba8_unorm,
        .test_enabled = true,
        .write_enabled = true,
        .compare = CompareOp::less,
    };
    static_assert(!is_valid(invalid_color_depth));

    constexpr RenderPassDesc pass{};
    static_assert(pass.depth_attachment == nullptr);
    static_assert(pass.clear_depth == 1.0F);
    static_assert(pass.clear_depth_attachment);

    return 0;
}

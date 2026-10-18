from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    file = Path(path)
    text = file.read_text()
    count = text.count(old)
    if count != 1:
        raise RuntimeError(f"{path}: expected one anchor, found {count}")
    file.write_text(text.replace(old, new, 1))


# Math: add a reusable right-handed, zero-to-one orthographic projection.
replace_once(
    "engine/math/include/vice/math/math.hpp",
    "[[nodiscard]] inline Mat4 look_at_rh(\n",
    """[[nodiscard]] inline Mat4 orthographic_rh_zo(\n    const float left,\n    const float right,\n    const float bottom,\n    const float top,\n    const float near_plane,\n    const float far_plane\n) noexcept {\n    Mat4 result{};\n    if (right <= left || top <= bottom || near_plane < 0.0F || far_plane <= near_plane) {\n        return result;\n    }\n\n    result(0, 0) = 2.0F / (right - left);\n    result(1, 1) = 2.0F / (top - bottom);\n    result(2, 2) = 1.0F / (near_plane - far_plane);\n    result(0, 3) = -(right + left) / (right - left);\n    result(1, 3) = -(top + bottom) / (top - bottom);\n    result(2, 3) = near_plane / (near_plane - far_plane);\n    result(3, 3) = 1.0F;\n    return result;\n}\n\n[[nodiscard]] inline Mat4 look_at_rh(\n""",
)

# RHI contract: depth-only pipelines and offscreen depth passes.
replace_once(
    "engine/render/include/vice/render/rhi.hpp",
    """[[nodiscard]] constexpr bool is_valid(const GraphicsPipelineDesc& desc) noexcept {\n    return desc.vertex_shader != nullptr && desc.fragment_shader != nullptr &&\n        desc.color_format != TextureFormat::undefined &&\n        desc.color_format != TextureFormat::depth32_float &&\n        is_valid(desc.depth) && is_valid(desc.vertex_buffers);\n}\n""",
    """[[nodiscard]] constexpr bool is_valid(const GraphicsPipelineDesc& desc) noexcept {\n    if (desc.vertex_shader == nullptr || !is_valid(desc.depth) || !is_valid(desc.vertex_buffers)) {\n        return false;\n    }\n    const bool has_color_attachment = desc.color_format != TextureFormat::undefined;\n    const bool has_fragment_shader = desc.fragment_shader != nullptr;\n    if (has_color_attachment != has_fragment_shader) {\n        return false;\n    }\n    if (has_color_attachment) {\n        return desc.color_format != TextureFormat::depth32_float;\n    }\n    return desc.depth.format == TextureFormat::depth32_float &&\n        desc.depth.test_enabled && desc.depth.write_enabled;\n}\n""",
)
replace_once(
    "engine/render/include/vice/render/rhi.hpp",
    "    virtual bool begin_render_pass(Swapchain& swapchain, const RenderPassDesc& desc) = 0;\n",
    """    virtual bool begin_render_pass(Swapchain& swapchain, const RenderPassDesc& desc) = 0;\n    virtual bool begin_depth_render_pass(\n        Texture& depth_attachment,\n        float clear_depth = 1.0F,\n        bool clear_depth_attachment = true\n    ) = 0;\n""",
)

# Direct3D 12 offscreen depth pass.
replace_once(
    "engine/render/src/d3d12/rhi.cpp",
    """        active_swapchain_ = d3d_swapchain;\n        active_depth_texture_ = depth_texture;\n        render_pass_active_ = true;\n        reset_graphics_bindings();\n        return true;\n    }\n\n    bool set_viewport(const Viewport& viewport) override {\n""",
    """        active_swapchain_ = d3d_swapchain;\n        active_depth_texture_ = depth_texture;\n        render_pass_active_ = true;\n        reset_graphics_bindings();\n        return true;\n    }\n\n    bool begin_depth_render_pass(\n        Texture& depth_attachment,\n        const float clear_depth,\n        const bool clear_depth_attachment\n    ) override {\n        if (!valid() || render_pass_active_ || clear_depth < 0.0F || clear_depth > 1.0F) {\n            return false;\n        }\n        auto* depth_texture = dynamic_cast<D3D12Texture*>(&depth_attachment);\n        if (depth_texture == nullptr || depth_texture->format() != TextureFormat::depth32_float ||\n            !has_usage(depth_texture->usage(), TextureUsage::depth_attachment) ||\n            !depth_texture->has_dsv()) {\n            return false;\n        }\n\n        transition(*depth_texture, D3D12_RESOURCE_STATE_DEPTH_WRITE);\n        const auto depth_handle = depth_texture->dsv();\n        command_list_->OMSetRenderTargets(0, nullptr, FALSE, &depth_handle);\n        if (clear_depth_attachment) {\n            command_list_->ClearDepthStencilView(\n                depth_handle,\n                D3D12_CLEAR_FLAG_DEPTH,\n                clear_depth,\n                0,\n                0,\n                nullptr\n            );\n        }\n        active_swapchain_ = nullptr;\n        active_depth_texture_ = depth_texture;\n        render_pass_active_ = true;\n        reset_graphics_bindings();\n        return true;\n    }\n\n    bool set_viewport(const Viewport& viewport) override {\n""",
)

# Direct3D 12 depth-only pipeline creation.
replace_once(
    "engine/render/src/d3d12/rhi.cpp",
    """        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||\n            desc.fragment_shader->stage() != ShaderStage::fragment) {\n            core::log(core::LogLevel::error, \"VICE rejected an invalid graphics pipeline description.\");\n            return nullptr;\n        }\n        auto* vertex = dynamic_cast<D3D12ShaderModule*>(desc.vertex_shader);\n        auto* fragment = dynamic_cast<D3D12ShaderModule*>(desc.fragment_shader);\n        if (vertex == nullptr || fragment == nullptr) {\n            return nullptr;\n        }\n\n        const PipelineLayout layout = compile_graphics_pipeline_layout(\n            vertex->resources(),\n            fragment->resources()\n        );\n""",
    """        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||\n            (desc.fragment_shader != nullptr &&\n                desc.fragment_shader->stage() != ShaderStage::fragment)) {\n            core::log(core::LogLevel::error, \"VICE rejected an invalid graphics pipeline description.\");\n            return nullptr;\n        }\n        auto* vertex = dynamic_cast<D3D12ShaderModule*>(desc.vertex_shader);\n        auto* fragment = desc.fragment_shader != nullptr\n            ? dynamic_cast<D3D12ShaderModule*>(desc.fragment_shader)\n            : nullptr;\n        if (vertex == nullptr || (desc.fragment_shader != nullptr && fragment == nullptr)) {\n            return nullptr;\n        }\n        const std::span<const ShaderResourceBindingDesc> fragment_resources = fragment != nullptr\n            ? fragment->resources()\n            : std::span<const ShaderResourceBindingDesc>{};\n\n        const PipelineLayout layout = compile_graphics_pipeline_layout(\n            vertex->resources(),\n            fragment_resources\n        );\n""",
)
replace_once(
    "engine/render/src/d3d12/rhi.cpp",
    "        pipeline_desc.PS = fragment->native_bytecode();\n",
    """        if (fragment != nullptr) {\n            pipeline_desc.PS = fragment->native_bytecode();\n        }\n""",
)
replace_once(
    "engine/render/src/d3d12/rhi.cpp",
    """        pipeline_desc.NumRenderTargets = 1;\n        pipeline_desc.RTVFormats[0] = pixel_format(desc.color_format);\n        pipeline_desc.DSVFormat = pixel_format(desc.depth.format);\n""",
    """        const bool has_color_attachment = desc.color_format != TextureFormat::undefined;\n        pipeline_desc.NumRenderTargets = has_color_attachment ? 1U : 0U;\n        if (has_color_attachment) {\n            pipeline_desc.RTVFormats[0] = pixel_format(desc.color_format);\n        }\n        pipeline_desc.DSVFormat = pixel_format(desc.depth.format);\n""",
)

# Metal offscreen depth pass.
replace_once(
    "engine/render/src/metal/rhi.mm",
    """        active_swapchain_ = metal_swapchain;\n        active_depth_texture_ = depth_texture;\n        reset_mesh_state();\n        active_pipeline_ = nullptr;\n        bound_resources_.clear();\n        pipeline_bound_ = false;\n        return render_encoder_ != nil;\n    }\n\n    bool set_viewport(const Viewport& viewport) override {\n""",
    """        active_swapchain_ = metal_swapchain;\n        active_depth_texture_ = depth_texture;\n        reset_mesh_state();\n        active_pipeline_ = nullptr;\n        bound_resources_.clear();\n        pipeline_bound_ = false;\n        return render_encoder_ != nil;\n    }\n\n    bool begin_depth_render_pass(\n        Texture& depth_attachment,\n        const float clear_depth,\n        const bool clear_depth_attachment\n    ) override {\n        if (render_encoder_ != nil || drawable_ != nil || command_buffer_ == nil ||\n            clear_depth < 0.0F || clear_depth > 1.0F) {\n            return false;\n        }\n        auto* depth_texture = dynamic_cast<MetalTexture*>(&depth_attachment);\n        if (depth_texture == nullptr || depth_texture->native() == nil ||\n            depth_texture->format() != TextureFormat::depth32_float ||\n            !has_usage(depth_texture->usage(), TextureUsage::depth_attachment)) {\n            return false;\n        }\n\n        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];\n        pass.depthAttachment.texture = depth_texture->native();\n        pass.depthAttachment.loadAction = clear_depth_attachment\n            ? MTLLoadActionClear\n            : MTLLoadActionLoad;\n        pass.depthAttachment.storeAction = MTLStoreActionStore;\n        pass.depthAttachment.clearDepth = clear_depth;\n        render_encoder_ = [command_buffer_ renderCommandEncoderWithDescriptor:pass];\n        active_swapchain_ = nullptr;\n        active_depth_texture_ = depth_texture;\n        reset_mesh_state();\n        active_pipeline_ = nullptr;\n        bound_resources_.clear();\n        pipeline_bound_ = false;\n        return render_encoder_ != nil;\n    }\n\n    bool set_viewport(const Viewport& viewport) override {\n""",
)

# Metal depth-only pipeline creation.
replace_once(
    "engine/render/src/metal/rhi.mm",
    """        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||\n            desc.fragment_shader->stage() != ShaderStage::fragment) {\n            core::log(core::LogLevel::error, \"VICE rejected an invalid graphics pipeline description.\");\n            return nullptr;\n        }\n        auto* vertex = dynamic_cast<MetalShaderModule*>(desc.vertex_shader);\n        auto* fragment = dynamic_cast<MetalShaderModule*>(desc.fragment_shader);\n        if (vertex == nullptr || fragment == nullptr) {\n            return nullptr;\n        }\n\n        PipelineLayout layout = compile_graphics_pipeline_layout(\n            vertex->resources(),\n            fragment->resources()\n        );\n""",
    """        if (!is_valid(desc) || desc.vertex_shader->stage() != ShaderStage::vertex ||\n            (desc.fragment_shader != nullptr &&\n                desc.fragment_shader->stage() != ShaderStage::fragment)) {\n            core::log(core::LogLevel::error, \"VICE rejected an invalid graphics pipeline description.\");\n            return nullptr;\n        }\n        auto* vertex = dynamic_cast<MetalShaderModule*>(desc.vertex_shader);\n        auto* fragment = desc.fragment_shader != nullptr\n            ? dynamic_cast<MetalShaderModule*>(desc.fragment_shader)\n            : nullptr;\n        if (vertex == nullptr || (desc.fragment_shader != nullptr && fragment == nullptr)) {\n            return nullptr;\n        }\n        const std::span<const ShaderResourceBindingDesc> fragment_resources = fragment != nullptr\n            ? fragment->resources()\n            : std::span<const ShaderResourceBindingDesc>{};\n\n        PipelineLayout layout = compile_graphics_pipeline_layout(\n            vertex->resources(),\n            fragment_resources\n        );\n""",
)
replace_once(
    "engine/render/src/metal/rhi.mm",
    """        native_desc.vertexFunction = vertex->function();\n        native_desc.fragmentFunction = fragment->function();\n        native_desc.vertexDescriptor = vertex_descriptor;\n        auto* color_attachment = native_desc.colorAttachments[0];\n        color_attachment.pixelFormat = pixel_format(desc.color_format);\n        if (desc.blend == BlendMode::alpha) {\n            color_attachment.blendingEnabled = YES;\n            color_attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;\n            color_attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;\n            color_attachment.rgbBlendOperation = MTLBlendOperationAdd;\n            color_attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;\n            color_attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;\n            color_attachment.alphaBlendOperation = MTLBlendOperationAdd;\n        }\n""",
    """        native_desc.vertexFunction = vertex->function();\n        native_desc.fragmentFunction = fragment != nullptr ? fragment->function() : nil;\n        native_desc.vertexDescriptor = vertex_descriptor;\n        if (desc.color_format != TextureFormat::undefined) {\n            auto* color_attachment = native_desc.colorAttachments[0];\n            color_attachment.pixelFormat = pixel_format(desc.color_format);\n            if (desc.blend == BlendMode::alpha) {\n                color_attachment.blendingEnabled = YES;\n                color_attachment.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;\n                color_attachment.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceAlpha;\n                color_attachment.rgbBlendOperation = MTLBlendOperationAdd;\n                color_attachment.sourceAlphaBlendFactor = MTLBlendFactorOne;\n                color_attachment.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;\n                color_attachment.alphaBlendOperation = MTLBlendOperationAdd;\n            }\n        }\n""",
)
replace_once(
    "engine/render/src/metal/rhi.mm",
    """            std::vector<MetalShaderResourceLocationDesc>{\n                fragment->locations().begin(),\n                fragment->locations().end()\n            },\n""",
    """            fragment != nullptr\n                ? std::vector<MetalShaderResourceLocationDesc>{\n                    fragment->locations().begin(),\n                    fragment->locations().end()\n                }\n                : std::vector<MetalShaderResourceLocationDesc>{},\n""",
)

# Validation regressions.
replace_once(
    "tests/render_tests.cpp",
    "#include <cstddef>\n\nint main() {\n",
    """#include <cstddef>\n\nnamespace {\n\nclass TestShaderModule final : public vice::render::ShaderModule {\npublic:\n    explicit TestShaderModule(const vice::render::ShaderStage stage) : stage_{stage} {}\n    [[nodiscard]] vice::render::ShaderStage stage() const noexcept override { return stage_; }\n\nprivate:\n    vice::render::ShaderStage stage_;\n};\n\n} // namespace\n\nint main() {\n""",
)
replace_once(
    "tests/render_tests.cpp",
    """    constexpr GraphicsPipelineDesc invalid_pipeline{};\n    static_assert(!is_valid(invalid_pipeline));\n\n    constexpr RenderPassDesc render_pass{};\n""",
    """    constexpr GraphicsPipelineDesc invalid_pipeline{};\n    static_assert(!is_valid(invalid_pipeline));\n\n    TestShaderModule vertex_shader{ShaderStage::vertex};\n    TestShaderModule fragment_shader{ShaderStage::fragment};\n    GraphicsPipelineDesc depth_only_pipeline;\n    depth_only_pipeline.vertex_shader = &vertex_shader;\n    depth_only_pipeline.fragment_shader = nullptr;\n    depth_only_pipeline.color_format = TextureFormat::undefined;\n    depth_only_pipeline.depth.format = TextureFormat::depth32_float;\n    depth_only_pipeline.depth.test_enabled = true;\n    depth_only_pipeline.depth.write_enabled = true;\n    assert(is_valid(depth_only_pipeline));\n\n    auto invalid_depth_only = depth_only_pipeline;\n    invalid_depth_only.depth.write_enabled = false;\n    assert(!is_valid(invalid_depth_only));\n\n    auto invalid_color_without_fragment = depth_only_pipeline;\n    invalid_color_without_fragment.color_format = TextureFormat::bgra8_unorm;\n    assert(!is_valid(invalid_color_without_fragment));\n\n    GraphicsPipelineDesc color_pipeline;\n    color_pipeline.vertex_shader = &vertex_shader;\n    color_pipeline.fragment_shader = &fragment_shader;\n    color_pipeline.color_format = TextureFormat::bgra8_unorm;\n    assert(is_valid(color_pipeline));\n\n    constexpr RenderPassDesc render_pass{};\n""",
)

# Orthographic projection regression.
replace_once(
    "tests/math_tests.cpp",
    "    const auto perspective = perspective_rh_zo(1.0F, 16.0F / 9.0F, 0.1F, 1000.0F);\n",
    """    const auto orthographic = orthographic_rh_zo(-2.0F, 2.0F, -1.0F, 3.0F, 0.5F, 10.5F);\n    assert(near(orthographic(0, 0), 0.5F));\n    assert(near(orthographic(1, 1), 0.5F));\n    assert(near(orthographic(2, 2), -0.1F));\n    assert(near(orthographic(1, 3), -0.5F));\n    assert(near(orthographic(2, 3), -0.05F));\n    assert(near(orthographic(3, 3), 1.0F));\n\n    const auto perspective = perspective_rh_zo(1.0F, 16.0F / 9.0F, 0.1F, 1000.0F);\n""",
)

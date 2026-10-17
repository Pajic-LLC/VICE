#include <vice/editor/ui_renderer.hpp>

#include <vice/core/log.hpp>
#include <vice/render/mesh_input.hpp>

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>

namespace vice::editor {
namespace {

struct UiFrameConstants {
    float scale_x = 1.0F;
    float scale_y = 1.0F;
    float translate_x = 0.0F;
    float translate_y = 0.0F;
    float font_width = 1.0F;
    float font_height = 1.0F;
    float inverse_font_width = 1.0F;
    float inverse_font_height = 1.0F;
};

static_assert(sizeof(UiFrameConstants) == 32U);

constexpr std::string_view hlsl_ui_source = R"(
cbuffer UiFrame : register(b0) {
    float2 ui_scale;
    float2 ui_translate;
    float4 font_info;
};

struct VertexIn {
    float2 position : POSITION0;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float2 uv : TEXCOORD0;
    float4 color : COLOR0;
};

VertexOut vice_editor_ui_vs(VertexIn input) {
    VertexOut output;
    output.position = float4(input.position * ui_scale + ui_translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

StructuredBuffer<uint> font_pixels : register(t1);

float font_alpha(int2 pixel) {
    int width = max(1, (int)font_info.x);
    int height = max(1, (int)font_info.y);
    pixel = clamp(pixel, int2(0, 0), int2(width - 1, height - 1));
    uint packed = font_pixels[pixel.y * width + pixel.x];
    return ((packed >> 24) & 255u) / 255.0;
}

float4 vice_editor_ui_fs(VertexOut input) : SV_Target0 {
    float2 size = max(float2(1.0, 1.0), font_info.xy);
    float2 sample_position = input.uv * size - 0.5;
    int2 base = (int2)floor(sample_position);
    float2 fraction = frac(sample_position);
    float a00 = font_alpha(base);
    float a10 = font_alpha(base + int2(1, 0));
    float a01 = font_alpha(base + int2(0, 1));
    float a11 = font_alpha(base + int2(1, 1));
    float top = lerp(a00, a10, fraction.x);
    float bottom = lerp(a01, a11, fraction.x);
    float alpha = lerp(top, bottom, fraction.y);
    return float4(input.color.rgb, input.color.a * alpha);
}
)";

constexpr std::string_view metal_ui_source = R"(
#include <metal_stdlib>
using namespace metal;

struct UiFrame {
    float2 scale;
    float2 translate;
    float4 font_info;
};

struct VertexIn {
    float2 position [[attribute(11)]];
    float2 uv [[attribute(12)]];
    float4 color [[attribute(13)]];
};

struct VertexOut {
    float4 position [[position]];
    float2 uv;
    float4 color;
};

vertex VertexOut vice_editor_ui_vs(
    VertexIn input [[stage_in]],
    constant UiFrame& frame [[buffer(0)]]
) {
    VertexOut output;
    output.position = float4(input.position * frame.scale + frame.translate, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;
    return output;
}

float font_alpha(
    device const uint* pixels,
    int2 pixel,
    constant UiFrame& frame
) {
    int width = max(1, (int)frame.font_info.x);
    int height = max(1, (int)frame.font_info.y);
    pixel = clamp(pixel, int2(0), int2(width - 1, height - 1));
    uint packed = pixels[pixel.y * width + pixel.x];
    return float((packed >> 24) & 255u) / 255.0;
}

fragment float4 vice_editor_ui_fs(
    VertexOut input [[stage_in]],
    constant UiFrame& frame [[buffer(0)]],
    device const uint* font_pixels [[buffer(1)]]
) {
    float2 size = max(float2(1.0), frame.font_info.xy);
    float2 sample_position = input.uv * size - 0.5;
    int2 base = int2(floor(sample_position));
    float2 fraction = fract(sample_position);
    float a00 = font_alpha(font_pixels, base, frame);
    float a10 = font_alpha(font_pixels, base + int2(1, 0), frame);
    float a01 = font_alpha(font_pixels, base + int2(0, 1), frame);
    float a11 = font_alpha(font_pixels, base + int2(1, 1), frame);
    float top = mix(a00, a10, fraction.x);
    float bottom = mix(a01, a11, fraction.x);
    float alpha = mix(top, bottom, fraction.y);
    return float4(input.color.rgb, input.color.a * alpha);
}
)";

static constexpr std::array ui_vertex_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x2,
        .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, pos)),
    },
    render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x2,
        .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, uv)),
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 2,
        .format = render::VertexFormat::unorm8x4,
        .offset = static_cast<std::uint32_t>(offsetof(ImDrawVert, col)),
    },
};

static constexpr std::array ui_vertex_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(ImDrawVert),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = ui_vertex_attributes,
    },
};

static constexpr std::array vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

static constexpr std::array fragment_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 1,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

constexpr render::ShaderBindingKey font_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1,
    .space = 0,
    .array_element = 0,
};

[[nodiscard]] std::uint64_t grown_capacity(const std::uint64_t required) noexcept {
    std::uint64_t capacity = 4096U;
    while (capacity < required && capacity <= std::numeric_limits<std::uint64_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

} // namespace

struct UiRhiRenderer::State {
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::Buffer> vertex_buffer;
    std::unique_ptr<render::Buffer> index_buffer;
    std::unique_ptr<render::Buffer> frame_buffer;
    std::unique_ptr<render::Buffer> font_buffer;
    std::uint64_t vertex_capacity = 0;
    std::uint64_t index_capacity = 0;
    std::uint32_t font_width = 0;
    std::uint32_t font_height = 0;
    bool initialized = false;
};

UiRhiRenderer::UiRhiRenderer() : state_{std::make_unique<State>()} {}
UiRhiRenderer::~UiRhiRenderer() = default;

bool UiRhiRenderer::initialize(render::Device& device, const render::TextureFormat color_format) {
    if (state_ == nullptr || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    auto& io = ImGui::GetIO();
    unsigned char* pixels = nullptr;
    int width = 0;
    int height = 0;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    if (pixels == nullptr || width <= 0 || height <= 0) {
        core::log(core::LogLevel::error, "VICE Editor could not build the UI font atlas.");
        return false;
    }

    const std::uint64_t pixel_count =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height);
    if (pixel_count > std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t)) {
        return false;
    }
    const std::uint64_t atlas_size = pixel_count * sizeof(std::uint32_t);

    render::BufferDesc font_upload_desc;
    font_upload_desc.size = atlas_size;
    font_upload_desc.usage = render::BufferUsage::copy_source;
    font_upload_desc.memory = render::MemoryLocation::cpu_to_gpu;
    font_upload_desc.debug_name = "VICE Editor font upload";

    render::BufferDesc font_desc;
    font_desc.size = atlas_size;
    font_desc.usage = render::BufferUsage::storage | render::BufferUsage::copy_destination;
    font_desc.memory = render::MemoryLocation::gpu_only;
    font_desc.debug_name = "VICE Editor font atlas";

    auto font_upload = device.create_buffer(font_upload_desc);
    state_->font_buffer = device.create_buffer(font_desc);
    if (!font_upload || !state_->font_buffer ||
        !font_upload->write(
            0,
            std::span<const std::byte>{
                reinterpret_cast<const std::byte*>(pixels),
                static_cast<std::size_t>(atlas_size)
            }
        )) {
        core::log(core::LogLevel::error, "VICE Editor could not allocate the UI font atlas.");
        return false;
    }

    auto upload_commands = device.create_command_list();
    if (!upload_commands ||
        !upload_commands->copy_buffer(*font_upload, 0, *state_->font_buffer, 0, atlas_size) ||
        device.submit(std::move(upload_commands)) == render::invalid_submission) {
        core::log(core::LogLevel::error, "VICE Editor could not upload the UI font atlas.");
        return false;
    }

    render::BufferDesc frame_desc;
    frame_desc.size = sizeof(UiFrameConstants);
    frame_desc.usage = render::BufferUsage::uniform;
    frame_desc.memory = render::MemoryLocation::cpu_to_gpu;
    frame_desc.debug_name = "VICE Editor UI frame constants";
    state_->frame_buffer = device.create_buffer(frame_desc);
    if (!state_->frame_buffer) {
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal ? metal_ui_source : hlsl_ui_source;
    vertex_desc.resources = vertex_resources;
    vertex_desc.entry_point = "vice_editor_ui_vs";
    vertex_desc.debug_name = "VICE Editor UI vertex";

    render::ShaderModuleDesc fragment_desc;
    fragment_desc.stage = render::ShaderStage::fragment;
    fragment_desc.language = vertex_desc.language;
    fragment_desc.source = vertex_desc.source;
    fragment_desc.resources = fragment_resources;
    fragment_desc.entry_point = "vice_editor_ui_fs";
    fragment_desc.debug_name = "VICE Editor UI fragment";

    state_->vertex_shader = device.create_shader_module(vertex_desc);
    state_->fragment_shader = device.create_shader_module(fragment_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) {
        core::log(core::LogLevel::error, "VICE Editor could not create UI shaders.");
        return false;
    }

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = color_format;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.blend = render::BlendMode::alpha;
    pipeline_desc.vertex_buffers = ui_vertex_layouts;
    pipeline_desc.debug_name = "VICE Editor UI pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        core::log(core::LogLevel::error, "VICE Editor could not create the UI pipeline.");
        return false;
    }

    state_->font_width = static_cast<std::uint32_t>(width);
    state_->font_height = static_cast<std::uint32_t>(height);
    state_->initialized = true;
    io.BackendRendererName = "VICE Render";
    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;
    return true;
}

bool UiRhiRenderer::valid() const noexcept {
    return state_ != nullptr && state_->initialized && state_->pipeline != nullptr &&
        state_->frame_buffer != nullptr && state_->font_buffer != nullptr;
}

bool UiRhiRenderer::render(
    render::Device& device,
    render::CommandList& commands,
    const platform::WindowMetrics metrics
) {
    if (!valid() || ImGui::GetCurrentContext() == nullptr) {
        return false;
    }

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (draw_data == nullptr || draw_data->CmdListsCount == 0 ||
        draw_data->TotalVtxCount <= 0 || draw_data->TotalIdxCount <= 0) {
        return true;
    }

    const std::uint64_t vertex_bytes =
        static_cast<std::uint64_t>(draw_data->TotalVtxCount) * sizeof(ImDrawVert);
    const std::uint64_t index_bytes =
        static_cast<std::uint64_t>(draw_data->TotalIdxCount) * sizeof(ImDrawIdx);

    if (!state_->vertex_buffer || state_->vertex_capacity < vertex_bytes) {
        state_->vertex_capacity = grown_capacity(vertex_bytes);
        render::BufferDesc desc;
        desc.size = state_->vertex_capacity;
        desc.usage = render::BufferUsage::vertex;
        desc.memory = render::MemoryLocation::cpu_to_gpu;
        desc.debug_name = "VICE Editor UI vertices";
        state_->vertex_buffer = device.create_buffer(desc);
    }
    if (!state_->index_buffer || state_->index_capacity < index_bytes) {
        state_->index_capacity = grown_capacity(index_bytes);
        render::BufferDesc desc;
        desc.size = state_->index_capacity;
        desc.usage = render::BufferUsage::index;
        desc.memory = render::MemoryLocation::cpu_to_gpu;
        desc.debug_name = "VICE Editor UI indices";
        state_->index_buffer = device.create_buffer(desc);
    }
    if (!state_->vertex_buffer || !state_->index_buffer) {
        return false;
    }

    std::uint64_t vertex_offset_bytes = 0;
    std::uint64_t index_offset_bytes = 0;
    for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
        const ImDrawList* list = draw_data->CmdLists[list_index];
        const std::size_t list_vertex_bytes =
            static_cast<std::size_t>(list->VtxBuffer.Size) * sizeof(ImDrawVert);
        const std::size_t list_index_bytes =
            static_cast<std::size_t>(list->IdxBuffer.Size) * sizeof(ImDrawIdx);
        if (!state_->vertex_buffer->write(
                vertex_offset_bytes,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(list->VtxBuffer.Data),
                    list_vertex_bytes
                }
            ) ||
            !state_->index_buffer->write(
                index_offset_bytes,
                std::span<const std::byte>{
                    reinterpret_cast<const std::byte*>(list->IdxBuffer.Data),
                    list_index_bytes
                }
            )) {
            return false;
        }
        vertex_offset_bytes += list_vertex_bytes;
        index_offset_bytes += list_index_bytes;
    }

    const ImVec2 display_size = draw_data->DisplaySize;
    if (display_size.x <= 0.0F || display_size.y <= 0.0F) {
        return true;
    }

    UiFrameConstants frame;
    frame.scale_x = 2.0F / display_size.x;
    frame.scale_y = -2.0F / display_size.y;
    frame.translate_x = -1.0F - draw_data->DisplayPos.x * frame.scale_x;
    frame.translate_y = 1.0F - draw_data->DisplayPos.y * frame.scale_y;
    frame.font_width = static_cast<float>(state_->font_width);
    frame.font_height = static_cast<float>(state_->font_height);
    frame.inverse_font_width = 1.0F / std::max(1.0F, frame.font_width);
    frame.inverse_font_height = 1.0F / std::max(1.0F, frame.font_height);
    if (!state_->frame_buffer->write(0, std::as_bytes(std::span{&frame, 1U}))) {
        return false;
    }

    const std::uint32_t framebuffer_width = metrics.drawable_width > 0U
        ? metrics.drawable_width
        : static_cast<std::uint32_t>(
            std::max(1.0F, draw_data->DisplaySize.x * draw_data->FramebufferScale.x)
        );
    const std::uint32_t framebuffer_height = metrics.drawable_height > 0U
        ? metrics.drawable_height
        : static_cast<std::uint32_t>(
            std::max(1.0F, draw_data->DisplaySize.y * draw_data->FramebufferScale.y)
        );

    if (!commands.set_viewport(render::Viewport{
            .x = 0.0F,
            .y = 0.0F,
            .width = static_cast<float>(framebuffer_width),
            .height = static_cast<float>(framebuffer_height),
        }) ||
        !commands.set_scissor_rect(render::ScissorRect{
            .x = 0,
            .y = 0,
            .width = framebuffer_width,
            .height = framebuffer_height,
        }) ||
        !commands.set_graphics_pipeline(*state_->pipeline) ||
        !commands.set_vertex_buffer(0, *state_->vertex_buffer) ||
        !commands.set_index_buffer(
            *state_->index_buffer,
            sizeof(ImDrawIdx) == 2U ? render::IndexFormat::uint16 : render::IndexFormat::uint32
        ) ||
        !commands.set_graphics_constant_buffer(
            frame_binding,
            *state_->frame_buffer,
            0,
            sizeof(UiFrameConstants)
        ) ||
        !commands.set_graphics_buffer(
            font_binding,
            *state_->font_buffer,
            render::BufferViewDesc{
                .offset = 0,
                .size = state_->font_buffer->size(),
                .structure_stride = sizeof(std::uint32_t),
            }
        )) {
        return false;
    }

    const ImVec2 clip_offset = draw_data->DisplayPos;
    const ImVec2 clip_scale = draw_data->FramebufferScale;
    std::uint32_t global_index_offset = 0;
    std::int32_t global_vertex_offset = 0;

    for (int list_index = 0; list_index < draw_data->CmdListsCount; ++list_index) {
        const ImDrawList* list = draw_data->CmdLists[list_index];
        for (const ImDrawCmd& command : list->CmdBuffer) {
            if (command.UserCallback != nullptr) {
                if (command.UserCallback != ImDrawCallback_ResetRenderState) {
                    command.UserCallback(list, &command);
                }
                continue;
            }

            float clip_min_x = (command.ClipRect.x - clip_offset.x) * clip_scale.x;
            float clip_min_y = (command.ClipRect.y - clip_offset.y) * clip_scale.y;
            float clip_max_x = (command.ClipRect.z - clip_offset.x) * clip_scale.x;
            float clip_max_y = (command.ClipRect.w - clip_offset.y) * clip_scale.y;
            clip_min_x = std::clamp(clip_min_x, 0.0F, static_cast<float>(framebuffer_width));
            clip_min_y = std::clamp(clip_min_y, 0.0F, static_cast<float>(framebuffer_height));
            clip_max_x = std::clamp(clip_max_x, 0.0F, static_cast<float>(framebuffer_width));
            clip_max_y = std::clamp(clip_max_y, 0.0F, static_cast<float>(framebuffer_height));
            if (clip_max_x <= clip_min_x || clip_max_y <= clip_min_y) {
                continue;
            }

            const auto left = static_cast<std::int32_t>(std::floor(clip_min_x));
            const auto top = static_cast<std::int32_t>(std::floor(clip_min_y));
            const auto right = static_cast<std::uint32_t>(std::ceil(clip_max_x));
            const auto bottom = static_cast<std::uint32_t>(std::ceil(clip_max_y));
            if (!commands.set_scissor_rect(render::ScissorRect{
                    .x = left,
                    .y = top,
                    .width = right - static_cast<std::uint32_t>(left),
                    .height = bottom - static_cast<std::uint32_t>(top),
                }) ||
                !commands.draw_indexed(
                    command.ElemCount,
                    1,
                    global_index_offset + command.IdxOffset,
                    global_vertex_offset + static_cast<std::int32_t>(command.VtxOffset),
                    0
                )) {
                return false;
            }
        }
        global_index_offset += static_cast<std::uint32_t>(list->IdxBuffer.Size);
        global_vertex_offset += static_cast<std::int32_t>(list->VtxBuffer.Size);
    }

    return true;
}

} // namespace vice::editor

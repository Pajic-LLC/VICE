#include "resource_mesh_renderer.hpp"

#include <vice/core/log.hpp>
#include <vice/math/math.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/mesh_input.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>
#include <variant>

namespace vice::editor::detail {
namespace {

constexpr float scene_fov = 0.82F;
constexpr std::string_view authored_ramp_id_text = "10000000-0000-4000-8000-000000000005";

static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x3,
        .offset = 0,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x3,
        .offset = 12,
    },
};

static constexpr std::array mesh_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(render::MeshVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = mesh_attributes,
    },
};

static constexpr std::array mesh_vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
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

struct MeshConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    std::array<float, 4> accent{};
};

static_assert(sizeof(MeshConstants) == 144U);

constexpr std::string_view hlsl_source = R"(
cbuffer SceneFrame : register(b0) {
    float4x4 view_projection;
    float4x4 model;
    float4 accent;
};

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_editor_resource_mesh_vs(VertexIn input) {
    VertexOut output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.color = saturate(input.color + accent.rgb * accent.a);
    return output;
}

float4 vice_editor_resource_mesh_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

constexpr std::string_view metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct SceneFrame {
    float4x4 view_projection;
    float4x4 model;
    float4 accent;
};

struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vice_editor_resource_mesh_vs(
    VertexIn input [[stage_in]],
    constant SceneFrame& frame [[buffer(0)]]
) {
    VertexOut output;
    float4 world_position = frame.model * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.color = saturate(input.color + frame.accent.rgb * frame.accent.a);
    return output;
}

fragment float4 vice_editor_resource_mesh_fs(VertexOut input [[stage_in]]) {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] math::Vec3 camera_eye(
    const math::Vec3 target,
    const float yaw,
    const float pitch,
    const float distance
) noexcept {
    const float horizontal = std::cos(pitch) * distance;
    return {
        target.x + std::sin(yaw) * horizontal,
        target.y + std::sin(pitch) * distance,
        target.z + std::cos(yaw) * horizontal,
    };
}

[[nodiscard]] math::Mat4 scale_matrix(const world::AuthoredScale scale) noexcept {
    math::Mat4 result = math::identity();
    result(0, 0) = scale.x;
    result(1, 1) = scale.y;
    result(2, 2) = scale.z;
    return result;
}

[[nodiscard]] math::Mat4 model_matrix(
    const world::SpatialState& spatial,
    const std::optional<world::AuthoredTransform>& authored
) noexcept {
    const world::AuthoredTransform transform = authored.value_or(world::AuthoredTransform{});
    auto result = math::translation({
        static_cast<float>(spatial.global_position.x),
        static_cast<float>(spatial.global_position.y),
        static_cast<float>(spatial.global_position.z),
    });
    result = math::multiply(
        result,
        math::rotation({
            transform.rotation.x,
            transform.rotation.y,
            transform.rotation.z,
            transform.rotation.w,
        })
    );
    return math::multiply(result, scale_matrix(transform.scale));
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_buffer_with_data(
    render::Device& device,
    const render::BufferUsage usage,
    const std::span<const std::byte> data,
    const std::string_view debug_name
) {
    if (data.empty()) return nullptr;
    render::BufferDesc desc;
    desc.size = data.size();
    desc.usage = usage;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    auto buffer = device.create_buffer(desc);
    if (!buffer || !buffer->write(0, data)) return nullptr;
    return buffer;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
    render::BufferDesc desc;
    desc.size = sizeof(MeshConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Editor resource mesh constants";
    return device.create_buffer(desc);
}

} // namespace

struct ResourceMeshRenderer::State {
    struct MeshGpu {
        std::unique_ptr<render::Buffer> vertices;
        std::unique_ptr<render::Buffer> indices;
        std::uint32_t index_count = 0;
        std::uint64_t source_fingerprint = 0;
    };

    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unordered_map<world::ResourceId, MeshGpu, world::ResourceIdHash> meshes;
    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;
    std::optional<world::PersistentId> authored_ramp_id;

    math::Vec3 target{0.0F, 0.8F, -2.0F};
    float yaw = 0.62F;
    float pitch = 0.38F;
    float distance = 14.0F;
    bool right_dragging = false;
    bool middle_dragging = false;
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool down = false;
    bool up = false;
    bool initialized = false;
};

ResourceMeshRenderer::ResourceMeshRenderer() : state_{std::make_unique<State>()} {}
ResourceMeshRenderer::~ResourceMeshRenderer() = default;
ResourceMeshRenderer::ResourceMeshRenderer(ResourceMeshRenderer&&) noexcept = default;
ResourceMeshRenderer& ResourceMeshRenderer::operator=(ResourceMeshRenderer&&) noexcept = default;

bool ResourceMeshRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format
) {
    if (state_ == nullptr) return false;
    state_->device = &device;
    state_->authored_ramp_id = world::PersistentId::parse(authored_ramp_id_text);

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal ? render::ShaderSourceLanguage::metal : render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal ? metal_source : hlsl_source;
    vertex_desc.resources = mesh_vertex_resources;
    vertex_desc.entry_point = "vice_editor_resource_mesh_vs";
    vertex_desc.debug_name = "VICE Editor resource mesh vertex";

    render::ShaderModuleDesc fragment_desc;
    fragment_desc.stage = render::ShaderStage::fragment;
    fragment_desc.language = vertex_desc.language;
    fragment_desc.source = vertex_desc.source;
    fragment_desc.entry_point = "vice_editor_resource_mesh_fs";
    fragment_desc.debug_name = "VICE Editor resource mesh fragment";

    state_->vertex_shader = device.create_shader_module(vertex_desc);
    state_->fragment_shader = device.create_shader_module(fragment_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) return false;

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = color_format;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = render::CompareOp::less;
    pipeline_desc.vertex_buffers = mesh_layouts;
    pipeline_desc.debug_name = "VICE Editor resource mesh pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    state_->initialized = state_->pipeline != nullptr;
    return state_->initialized;
}

void ResourceMeshRenderer::handle_event(
    const platform::Event& event,
    const SceneViewport& viewport
) {
    if (state_ == nullptr || !state_->initialized) return;

    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::WindowFocusChanged>) {
                if (!payload.focused) {
                    state_->right_dragging = false;
                    state_->middle_dragging = false;
                    state_->forward = state_->backward = state_->left = state_->right = false;
                    state_->down = state_->up = false;
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseButtonEvent>) {
                if (payload.button == platform::MouseButton::right) {
                    state_->right_dragging = payload.pressed ? viewport.hovered : false;
                } else if (payload.button == platform::MouseButton::middle) {
                    state_->middle_dragging = payload.pressed ? viewport.hovered : false;
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseMoveEvent>) {
                if (state_->right_dragging) {
                    state_->yaw -= static_cast<float>(payload.delta_x) * 0.006F;
                    state_->pitch = std::clamp(
                        state_->pitch - static_cast<float>(payload.delta_y) * 0.006F,
                        0.08F,
                        1.35F
                    );
                } else if (state_->middle_dragging) {
                    const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
                    const math::Vec3 forward = math::normalize(state_->target - eye);
                    const math::Vec3 right = math::normalize(math::cross(forward, {0.0F, 1.0F, 0.0F}));
                    const math::Vec3 up = math::normalize(math::cross(right, forward));
                    const float factor = std::max(0.002F, state_->distance * 0.0015F);
                    state_->target = add(
                        state_->target,
                        add(
                            scaled(right, -static_cast<float>(payload.delta_x) * factor),
                            scaled(up, static_cast<float>(payload.delta_y) * factor)
                        )
                    );
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseWheelEvent>) {
                if (viewport.hovered) {
                    const float divisor = payload.unit == platform::ScrollUnit::pixels ? 80.0F : 1.0F;
                    const float amount = static_cast<float>(payload.delta_y) / divisor;
                    state_->distance = std::clamp(state_->distance * std::exp(-amount * 0.12F), 2.0F, 80.0F);
                }
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                const bool shift = platform::has_modifier(payload.modifiers, platform::Modifier::shift);
                if (!payload.pressed) {
                    switch (payload.key) {
                    case platform::Key::w: state_->forward = false; break;
                    case platform::Key::s: state_->backward = false; break;
                    case platform::Key::a: state_->left = false; break;
                    case platform::Key::d: state_->right = false; break;
                    case platform::Key::q: state_->down = false; break;
                    case platform::Key::e: state_->up = false; break;
                    default: break;
                    }
                    return;
                }
                if (!viewport.focused) return;
                if (shift) {
                    switch (payload.key) {
                    case platform::Key::w: state_->forward = true; break;
                    case platform::Key::s: state_->backward = true; break;
                    case platform::Key::a: state_->left = true; break;
                    case platform::Key::d: state_->right = true; break;
                    case platform::Key::q: state_->down = true; break;
                    case platform::Key::e: state_->up = true; break;
                    default: break;
                    }
                } else if (payload.key == platform::Key::f) {
                    state_->target = {0.0F, 0.8F, -2.0F};
                    state_->yaw = 0.62F;
                    state_->pitch = 0.38F;
                    state_->distance = 14.0F;
                }
            }
        },
        event.payload
    );
}

void ResourceMeshRenderer::update(const float delta_seconds, const SceneViewport& viewport) {
    if (state_ == nullptr || !state_->initialized || !viewport.focused || delta_seconds <= 0.0F) return;

    const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
    math::Vec3 forward = state_->target - eye;
    forward.y = 0.0F;
    forward = math::normalize(forward);
    const math::Vec3 right = math::normalize(math::cross(forward, {0.0F, 1.0F, 0.0F}));
    math::Vec3 movement{};
    if (state_->forward) movement = add(movement, forward);
    if (state_->backward) movement = add(movement, scaled(forward, -1.0F));
    if (state_->right) movement = add(movement, right);
    if (state_->left) movement = add(movement, scaled(right, -1.0F));
    if (state_->up) movement.y += 1.0F;
    if (state_->down) movement.y -= 1.0F;
    if (math::dot(movement, movement) > 0.0F) {
        const float speed = std::max(2.0F, state_->distance * 0.45F);
        state_->target = add(state_->target, scaled(math::normalize(movement), speed * std::min(delta_seconds, 0.1F)));
    }
}

world::GlobalPosition ResourceMeshRenderer::suggested_spawn_position(const world::World& world) const noexcept {
    if (state_ == nullptr) return {};
    const world::GlobalPosition target{state_->target.x, state_->target.y, state_->target.z};
    if (world.region_containing(target).has_value()) return target;

    const auto snapshot = world.snapshot();
    if (!snapshot.regions.empty()) {
        const auto& region = snapshot.regions.front();
        if (region.bounds.has_value()) {
            return {
                region.origin.x + static_cast<double>((region.bounds->min.x + region.bounds->max.x) * 0.5F),
                region.origin.y + static_cast<double>((region.bounds->min.y + region.bounds->max.y) * 0.5F),
                region.origin.z + static_cast<double>((region.bounds->min.z + region.bounds->max.z) * 0.5F),
            };
        }
        return region.origin;
    }
    return target;
}

bool ResourceMeshRenderer::render(
    const world::World& world,
    const SelectionModel& selection,
    const AssetIndex& assets,
    render::CommandList& commands,
    const SceneViewport& viewport,
    const platform::WindowMetrics metrics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr ||
        state_->pipeline == nullptr || metrics.width == 0U || metrics.height == 0U) {
        return false;
    }
    if (!viewport.drawable()) return true;

    const float scale_x = metrics.drawable_width > 0U
        ? static_cast<float>(metrics.drawable_width) / static_cast<float>(metrics.width)
        : 1.0F;
    const float scale_y = metrics.drawable_height > 0U
        ? static_cast<float>(metrics.drawable_height) / static_cast<float>(metrics.height)
        : 1.0F;
    const float viewport_x = viewport.x * scale_x;
    const float viewport_y = viewport.y * scale_y;
    const float viewport_width = viewport.width * scale_x;
    const float viewport_height = viewport.height * scale_y;
    if (viewport_width < 1.0F || viewport_height < 1.0F) return true;

    if (!commands.set_viewport(render::Viewport{
            .x = viewport_x,
            .y = viewport_y,
            .width = viewport_width,
            .height = viewport_height,
            .min_depth = 0.0F,
            .max_depth = 1.0F,
        }) ||
        !commands.set_scissor_rect(render::ScissorRect{
            .x = static_cast<std::int32_t>(std::floor(viewport_x)),
            .y = static_cast<std::int32_t>(std::floor(viewport_y)),
            .width = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_width))),
            .height = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_height))),
        }) ||
        !commands.set_graphics_pipeline(*state_->pipeline)) {
        return false;
    }

    const float aspect = viewport.width / std::max(1.0F, viewport.height);
    const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
    const math::Mat4 view = math::look_at_rh(eye, state_->target, {0.0F, 1.0F, 0.0F});
    const math::Mat4 projection = math::perspective_rh_zo(scene_fov, aspect, 0.05F, 250.0F);
    const math::Mat4 view_projection = math::multiply(projection, view);
    const auto selected = selection.selected();

    const auto ensure_mesh = [&](const world::ResourceId resource_id) -> State::MeshGpu* {
        const AssetRecord* record = assets.find_resource(resource_id);
        if (record == nullptr || !record->instantiable_mesh()) return nullptr;

        const auto existing = state_->meshes.find(resource_id);
        if (existing != state_->meshes.end() &&
            existing->second.source_fingerprint == record->source_fingerprint) {
            return &existing->second;
        }

        const auto loaded = render::load_mesh_asset(record->cooked_path);
        if (!loaded.asset.has_value()) {
            core::log(core::LogLevel::error, "VICE Editor could not load resource mesh: " + loaded.diagnostics);
            return nullptr;
        }

        State::MeshGpu gpu;
        gpu.vertices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::vertex,
            std::as_bytes(std::span{loaded.asset->vertices}),
            "VICE Editor resource mesh vertices"
        );
        gpu.indices = create_buffer_with_data(
            *state_->device,
            render::BufferUsage::index,
            std::as_bytes(std::span{loaded.asset->indices}),
            "VICE Editor resource mesh indices"
        );
        gpu.index_count = static_cast<std::uint32_t>(loaded.asset->indices.size());
        gpu.source_fingerprint = loaded.asset->source_fingerprint;
        if (!gpu.vertices || !gpu.indices || gpu.index_count == 0U) return nullptr;
        state_->meshes.insert_or_assign(resource_id, std::move(gpu));
        return &state_->meshes.find(resource_id)->second;
    };

    for (const auto& entity : world.snapshot().entities) {
        if (!entity.spatial_state.has_value()) continue;
        if (state_->authored_ramp_id.has_value() && entity.persistent_id == *state_->authored_ramp_id) {
            continue;
        }

        std::optional<world::ResourceId> mesh_resource;
        for (const auto reference : entity.resource_references) {
            if (reference.role == world::ResourceRole::render_mesh) {
                mesh_resource = reference.resource;
                break;
            }
        }
        if (!mesh_resource.has_value()) continue;

        State::MeshGpu* mesh = ensure_mesh(*mesh_resource);
        if (mesh == nullptr) continue;

        auto& constants_buffer = state_->constants[entity.persistent_id];
        if (!constants_buffer) constants_buffer = create_constant_buffer(*state_->device);
        if (!constants_buffer) return false;

        const bool highlighted = selected.has_value() && *selected == entity.persistent_id;
        const MeshConstants constants{
            .view_projection = view_projection,
            .model = model_matrix(*entity.spatial_state, entity.authored_transform),
            .accent = {0.12F, 0.75F, 1.0F, highlighted ? 0.50F : 0.0F},
        };
        if (!constants_buffer->write(0, std::as_bytes(std::span{&constants, 1U})) ||
            !commands.set_vertex_buffer(0, *mesh->vertices) ||
            !commands.set_index_buffer(*mesh->indices, render::IndexFormat::uint32) ||
            !commands.set_graphics_constant_buffer(frame_binding, *constants_buffer, 0, sizeof(MeshConstants)) ||
            !commands.draw_indexed(mesh->index_count)) {
            return false;
        }
    }

    return true;
}

} // namespace vice::editor::detail

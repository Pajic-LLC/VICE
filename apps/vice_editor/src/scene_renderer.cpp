#include "scene_renderer.hpp"

#include <vice/core/log.hpp>
#include <vice/math/math.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/mesh_input.hpp>
#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>

namespace vice::editor::detail {
namespace {

using SceneVertex = render::MeshVertex;

constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";
constexpr std::string_view terrain_a_id_text = "10000000-0000-4000-8000-000000000003";
constexpr std::string_view terrain_b_id_text = "10000000-0000-4000-8000-000000000004";
constexpr std::string_view ramp_id_text = "10000000-0000-4000-8000-000000000005";
constexpr world::RegionId region_a_id{1};
constexpr world::RegionId region_b_id{2};
constexpr float scene_fov = 0.82F;
constexpr float ground_min_z = -10.0F;
constexpr float ground_max_z = 10.0F;

static constexpr std::array<SceneVertex, 8> cube_vertices{{
    {{-0.65F, -0.65F, -0.65F}, {0.17F, 0.62F, 0.94F}},
    {{ 0.65F, -0.65F, -0.65F}, {0.22F, 0.72F, 0.98F}},
    {{ 0.65F,  0.65F, -0.65F}, {0.37F, 0.82F, 0.98F}},
    {{-0.65F,  0.65F, -0.65F}, {0.26F, 0.69F, 0.96F}},
    {{-0.65F, -0.65F,  0.65F}, {0.12F, 0.48F, 0.82F}},
    {{ 0.65F, -0.65F,  0.65F}, {0.19F, 0.58F, 0.90F}},
    {{ 0.65F,  0.65F,  0.65F}, {0.42F, 0.88F, 0.98F}},
    {{-0.65F,  0.65F,  0.65F}, {0.30F, 0.76F, 0.98F}},
}};

static constexpr std::array<std::uint16_t, 36> cube_indices{
    4, 5, 6, 4, 6, 7,
    1, 0, 3, 1, 3, 2,
    0, 4, 7, 0, 7, 3,
    5, 1, 2, 5, 2, 6,
    3, 7, 6, 3, 6, 2,
    0, 1, 5, 0, 5, 4,
};

static constexpr std::array<SceneVertex, 4> ground_vertices{{
    {{0.0F, -0.03F, ground_min_z}, {0.105F, 0.124F, 0.153F}},
    {{8.0F, -0.03F, ground_min_z}, {0.125F, 0.145F, 0.175F}},
    {{8.0F, -0.03F, ground_max_z}, {0.095F, 0.114F, 0.143F}},
    {{0.0F, -0.03F, ground_max_z}, {0.115F, 0.134F, 0.163F}},
}};

static constexpr std::array<std::uint16_t, 6> ground_indices{0, 2, 1, 0, 3, 2};

static constexpr std::array scene_attributes{
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

static constexpr std::array scene_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(SceneVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = scene_attributes,
    },
};

static constexpr std::array scene_vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

constexpr render::ShaderBindingKey scene_frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    std::array<float, 4> accent{};
};

static_assert(sizeof(SceneConstants) == 144U);

constexpr std::string_view hlsl_scene_source = R"(
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

VertexOut vice_editor_scene_vs(VertexIn input) {
    VertexOut output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.color = saturate(input.color + accent.rgb * accent.a);
    return output;
}

float4 vice_editor_scene_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

constexpr std::string_view metal_scene_source = R"(
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

vertex VertexOut vice_editor_scene_vs(
    VertexIn input [[stage_in]],
    constant SceneFrame& frame [[buffer(0)]]
) {
    VertexOut output;
    float4 world_position = frame.model * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.color = saturate(input.color + frame.accent.rgb * frame.accent.a);
    return output;
}

fragment float4 vice_editor_scene_fs(VertexOut input [[stage_in]]) {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] math::Mat4 model_matrix(
    const math::Vec3 position,
    const math::Vec3 scale = {1.0F, 1.0F, 1.0F}
) noexcept {
    math::Mat4 result = math::identity();
    result(0, 0) = scale.x;
    result(1, 1) = scale.y;
    result(2, 2) = scale.z;
    result(0, 3) = position.x;
    result(1, 3) = position.y;
    result(2, 3) = position.z;
    return result;
}

[[nodiscard]] bool contains(const SceneViewport& viewport, const double x, const double y) noexcept {
    return viewport.drawable() && x >= viewport.x && y >= viewport.y &&
        x < static_cast<double>(viewport.x + viewport.width) &&
        y < static_cast<double>(viewport.y + viewport.height);
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

[[nodiscard]] float axis_value(const math::Vec3 value, const int axis) noexcept {
    switch (axis) {
    case 0: return value.x;
    case 1: return value.y;
    default: return value.z;
    }
}

[[nodiscard]] std::optional<float> ray_aabb_distance(
    const math::Vec3 origin,
    const math::Vec3 direction,
    const math::Vec3 minimum,
    const math::Vec3 maximum
) noexcept {
    float near_distance = 0.0F;
    float far_distance = std::numeric_limits<float>::max();

    for (int axis = 0; axis < 3; ++axis) {
        const float origin_axis = axis_value(origin, axis);
        const float direction_axis = axis_value(direction, axis);
        const float minimum_axis = axis_value(minimum, axis);
        const float maximum_axis = axis_value(maximum, axis);

        if (std::abs(direction_axis) < 1.0e-6F) {
            if (origin_axis < minimum_axis || origin_axis > maximum_axis) {
                return std::nullopt;
            }
            continue;
        }

        float first = (minimum_axis - origin_axis) / direction_axis;
        float second = (maximum_axis - origin_axis) / direction_axis;
        if (first > second) {
            std::swap(first, second);
        }
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (far_distance < near_distance) {
            return std::nullopt;
        }
    }

    return far_distance >= 0.0F ? std::optional<float>{std::max(0.0F, near_distance)} : std::nullopt;
}

[[nodiscard]] std::optional<math::Vec3> entity_position(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) {
        return std::nullopt;
    }
    const auto spatial = world.spatial_state(*entity);
    if (!spatial.has_value()) {
        return std::nullopt;
    }
    return math::Vec3{
        static_cast<float>(spatial->global_position.x),
        static_cast<float>(spatial->global_position.y),
        static_cast<float>(spatial->global_position.z),
    };
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_buffer_with_data(
    render::Device& device,
    const render::BufferUsage usage,
    const std::span<const std::byte> data,
    const std::string_view debug_name
) {
    if (data.empty()) {
        return nullptr;
    }
    render::BufferDesc desc;
    desc.size = data.size();
    desc.usage = usage;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    auto buffer = device.create_buffer(desc);
    if (!buffer || !buffer->write(0, data)) {
        return nullptr;
    }
    return buffer;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(
    render::Device& device,
    const std::string_view debug_name
) {
    render::BufferDesc desc;
    desc.size = sizeof(SceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    return device.create_buffer(desc);
}

} // namespace

struct SceneRenderer::State {
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;

    std::unique_ptr<render::Buffer> cube_vertices;
    std::unique_ptr<render::Buffer> cube_indices;
    std::unique_ptr<render::Buffer> ground_vertices;
    std::unique_ptr<render::Buffer> ground_indices;
    std::unique_ptr<render::Buffer> ramp_vertices;
    std::unique_ptr<render::Buffer> ramp_indices;

    std::unique_ptr<render::Buffer> ground_a_constants;
    std::unique_ptr<render::Buffer> ground_b_constants;
    std::unique_ptr<render::Buffer> ramp_constants;
    std::unique_ptr<render::Buffer> cube_constants;
    std::unique_ptr<render::Buffer> player_constants;

    std::uint32_t ramp_index_count = 0;
    math::Vec3 ramp_minimum{};
    math::Vec3 ramp_maximum{};
    bool ramp_bounds_valid = false;

    std::optional<world::PersistentId> player_id;
    std::optional<world::PersistentId> cube_id;
    std::optional<world::PersistentId> terrain_a_id;
    std::optional<world::PersistentId> terrain_b_id;
    std::optional<world::PersistentId> ramp_id;

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

SceneRenderer::SceneRenderer() : state_{std::make_unique<State>()} {}
SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::filesystem::path& authored_mesh_path
) {
    if (state_ == nullptr) {
        return false;
    }

    state_->player_id = world::PersistentId::parse(player_id_text);
    state_->cube_id = world::PersistentId::parse(cube_id_text);
    state_->terrain_a_id = world::PersistentId::parse(terrain_a_id_text);
    state_->terrain_b_id = world::PersistentId::parse(terrain_b_id_text);
    state_->ramp_id = world::PersistentId::parse(ramp_id_text);
    if (!state_->player_id.has_value() || !state_->cube_id.has_value() ||
        !state_->terrain_a_id.has_value() || !state_->terrain_b_id.has_value() ||
        !state_->ramp_id.has_value()) {
        return false;
    }

    state_->cube_vertices = create_buffer_with_data(
        device,
        render::BufferUsage::vertex,
        std::as_bytes(std::span{cube_vertices}),
        "VICE Editor cube vertices"
    );
    state_->cube_indices = create_buffer_with_data(
        device,
        render::BufferUsage::index,
        std::as_bytes(std::span{cube_indices}),
        "VICE Editor cube indices"
    );
    state_->ground_vertices = create_buffer_with_data(
        device,
        render::BufferUsage::vertex,
        std::as_bytes(std::span{ground_vertices}),
        "VICE Editor region ground vertices"
    );
    state_->ground_indices = create_buffer_with_data(
        device,
        render::BufferUsage::index,
        std::as_bytes(std::span{ground_indices}),
        "VICE Editor region ground indices"
    );

    if (!state_->cube_vertices || !state_->cube_indices ||
        !state_->ground_vertices || !state_->ground_indices) {
        core::log(core::LogLevel::error, "VICE Editor could not create scene geometry buffers.");
        return false;
    }

    if (!authored_mesh_path.empty()) {
        auto loaded = render::load_mesh_asset(authored_mesh_path);
        if (loaded.asset.has_value() && !loaded.asset->vertices.empty() && !loaded.asset->indices.empty()) {
            state_->ramp_vertices = create_buffer_with_data(
                device,
                render::BufferUsage::vertex,
                std::as_bytes(std::span{loaded.asset->vertices}),
                "VICE Editor authored scene vertices"
            );
            state_->ramp_indices = create_buffer_with_data(
                device,
                render::BufferUsage::index,
                std::as_bytes(std::span{loaded.asset->indices}),
                "VICE Editor authored scene indices"
            );
            state_->ramp_index_count = static_cast<std::uint32_t>(loaded.asset->indices.size());

            const auto& first = loaded.asset->vertices.front().position;
            state_->ramp_minimum = {first[0], first[1], first[2]};
            state_->ramp_maximum = state_->ramp_minimum;
            for (const auto& vertex : loaded.asset->vertices) {
                state_->ramp_minimum.x = std::min(state_->ramp_minimum.x, vertex.position[0]);
                state_->ramp_minimum.y = std::min(state_->ramp_minimum.y, vertex.position[1]);
                state_->ramp_minimum.z = std::min(state_->ramp_minimum.z, vertex.position[2]);
                state_->ramp_maximum.x = std::max(state_->ramp_maximum.x, vertex.position[0]);
                state_->ramp_maximum.y = std::max(state_->ramp_maximum.y, vertex.position[1]);
                state_->ramp_maximum.z = std::max(state_->ramp_maximum.z, vertex.position[2]);
            }
            state_->ramp_bounds_valid = state_->ramp_vertices != nullptr && state_->ramp_indices != nullptr;
        } else {
            core::log(
                core::LogLevel::warning,
                "VICE Editor could not load the authored scene mesh: " + loaded.diagnostics
            );
        }
    }

    state_->ground_a_constants = create_constant_buffer(device, "VICE Editor terrain A constants");
    state_->ground_b_constants = create_constant_buffer(device, "VICE Editor terrain B constants");
    state_->ramp_constants = create_constant_buffer(device, "VICE Editor ramp constants");
    state_->cube_constants = create_constant_buffer(device, "VICE Editor cube constants");
    state_->player_constants = create_constant_buffer(device, "VICE Editor player constants");
    if (!state_->ground_a_constants || !state_->ground_b_constants || !state_->ramp_constants ||
        !state_->cube_constants || !state_->player_constants) {
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal ? metal_scene_source : hlsl_scene_source;
    vertex_desc.resources = scene_vertex_resources;
    vertex_desc.entry_point = "vice_editor_scene_vs";
    vertex_desc.debug_name = "VICE Editor scene vertex";

    render::ShaderModuleDesc fragment_desc;
    fragment_desc.stage = render::ShaderStage::fragment;
    fragment_desc.language = vertex_desc.language;
    fragment_desc.source = vertex_desc.source;
    fragment_desc.entry_point = "vice_editor_scene_fs";
    fragment_desc.debug_name = "VICE Editor scene fragment";

    state_->vertex_shader = device.create_shader_module(vertex_desc);
    state_->fragment_shader = device.create_shader_module(fragment_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) {
        return false;
    }

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = color_format;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = render::CompareOp::less;
    pipeline_desc.vertex_buffers = scene_layouts;
    pipeline_desc.debug_name = "VICE Editor scene pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        return false;
    }

    state_->initialized = true;
    return true;
}

void SceneRenderer::handle_event(
    const platform::Event& event,
    const SceneViewport& viewport,
    const world::World& world,
    SelectionModel& selection
) {
    if (state_ == nullptr || !state_->initialized) {
        return;
    }

    const auto pick_at = [&](const double pointer_x, const double pointer_y) {
        if (!viewport.drawable() || !contains(viewport, pointer_x, pointer_y)) {
            return;
        }

        const float aspect = viewport.width / std::max(1.0F, viewport.height);
        const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
        const math::Vec3 forward = math::normalize(state_->target - eye);
        const math::Vec3 right = math::normalize(math::cross(forward, {0.0F, 1.0F, 0.0F}));
        const math::Vec3 up = math::normalize(math::cross(right, forward));
        const float normalized_x = static_cast<float>((pointer_x - viewport.x) / viewport.width);
        const float normalized_y = static_cast<float>((pointer_y - viewport.y) / viewport.height);
        const float ndc_x = normalized_x * 2.0F - 1.0F;
        const float ndc_y = 1.0F - normalized_y * 2.0F;
        const float half_height = std::tan(scene_fov * 0.5F);
        const math::Vec3 direction = math::normalize(add(
            forward,
            add(
                scaled(right, ndc_x * aspect * half_height),
                scaled(up, ndc_y * half_height)
            )
        ));

        float nearest = std::numeric_limits<float>::max();
        std::optional<world::PersistentId> picked;
        const auto consider_bounds = [&](
            const std::optional<world::PersistentId>& persistent_id,
            const math::Vec3 minimum,
            const math::Vec3 maximum
        ) {
            if (!persistent_id.has_value()) {
                return;
            }
            const auto distance = ray_aabb_distance(eye, direction, minimum, maximum);
            if (distance.has_value() && *distance < nearest) {
                nearest = *distance;
                picked = *persistent_id;
            }
        };

        if (state_->cube_id.has_value()) {
            if (const auto position = entity_position(world, *state_->cube_id); position.has_value()) {
                consider_bounds(
                    state_->cube_id,
                    add(*position, {-0.65F, -0.65F, -0.65F}),
                    add(*position, {0.65F, 0.65F, 0.65F})
                );
            }
        }

        if (state_->player_id.has_value()) {
            if (auto position = entity_position(world, *state_->player_id); position.has_value()) {
                position->y += 0.90F;
                consider_bounds(
                    state_->player_id,
                    add(*position, {-0.32F, -0.88F, -0.32F}),
                    add(*position, {0.32F, 0.88F, 0.32F})
                );
            }
        }

        if (state_->ramp_id.has_value() && state_->ramp_bounds_valid) {
            if (const auto region = world.region(region_a_id); region.has_value()) {
                const math::Vec3 origin{
                    static_cast<float>(region->origin.x),
                    static_cast<float>(region->origin.y),
                    static_cast<float>(region->origin.z),
                };
                consider_bounds(
                    state_->ramp_id,
                    add(origin, state_->ramp_minimum),
                    add(origin, state_->ramp_maximum)
                );
            }
        }

        if (std::abs(direction.y) > 1.0e-6F) {
            const float terrain_distance = (-0.03F - eye.y) / direction.y;
            if (terrain_distance >= 0.0F && terrain_distance < nearest) {
                const math::Vec3 hit = add(eye, scaled(direction, terrain_distance));
                const auto consider_terrain = [&](
                    const world::RegionId region_id,
                    const std::optional<world::PersistentId>& persistent_id
                ) {
                    if (!persistent_id.has_value()) {
                        return;
                    }
                    const auto region = world.region(region_id);
                    if (!region.has_value()) {
                        return;
                    }
                    const float origin_x = static_cast<float>(region->origin.x);
                    const float origin_z = static_cast<float>(region->origin.z);
                    if (hit.x >= origin_x && hit.x <= origin_x + 8.0F &&
                        hit.z >= origin_z + ground_min_z && hit.z <= origin_z + ground_max_z) {
                        picked = *persistent_id;
                        nearest = terrain_distance;
                    }
                };
                consider_terrain(region_a_id, state_->terrain_a_id);
                if (terrain_distance < nearest || !picked.has_value()) {
                    consider_terrain(region_b_id, state_->terrain_b_id);
                }
            }
        }

        if (picked.has_value()) {
            selection.select(*picked);
        } else {
            selection.clear();
        }
    };

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
                const bool inside = contains(viewport, payload.x, payload.y);
                if (payload.button == platform::MouseButton::right) {
                    state_->right_dragging = payload.pressed ? inside : false;
                } else if (payload.button == platform::MouseButton::middle) {
                    state_->middle_dragging = payload.pressed ? inside : false;
                } else if (payload.button == platform::MouseButton::left && payload.pressed && inside &&
                    !state_->right_dragging && !state_->middle_dragging) {
                    pick_at(payload.x, payload.y);
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
                    const math::Vec3 eye = camera_eye(
                        state_->target,
                        state_->yaw,
                        state_->pitch,
                        state_->distance
                    );
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
                    state_->distance = std::clamp(
                        state_->distance * std::exp(-amount * 0.12F),
                        2.0F,
                        80.0F
                    );
                }
            } else if constexpr (std::is_same_v<Payload, platform::KeyEvent>) {
                const bool accept_press = !payload.pressed || viewport.focused;
                if (!accept_press) {
                    return;
                }
                switch (payload.key) {
                case platform::Key::w: state_->forward = payload.pressed; break;
                case platform::Key::s: state_->backward = payload.pressed; break;
                case platform::Key::a: state_->left = payload.pressed; break;
                case platform::Key::d: state_->right = payload.pressed; break;
                case platform::Key::q: state_->down = payload.pressed; break;
                case platform::Key::e: state_->up = payload.pressed; break;
                case platform::Key::f:
                    if (payload.pressed && viewport.focused) {
                        state_->target = {0.0F, 0.8F, -2.0F};
                        state_->yaw = 0.62F;
                        state_->pitch = 0.38F;
                        state_->distance = 14.0F;
                    }
                    break;
                default: break;
                }
            }
        },
        event.payload
    );
}

void SceneRenderer::update(const float delta_seconds, const SceneViewport& viewport) {
    if (state_ == nullptr || !state_->initialized || !viewport.focused || delta_seconds <= 0.0F) {
        return;
    }

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
        state_->target = add(
            state_->target,
            scaled(math::normalize(movement), speed * std::min(delta_seconds, 0.1F))
        );
    }
}

bool SceneRenderer::render(
    const world::World& world,
    const SelectionModel& selection,
    render::CommandList& commands,
    const SceneViewport& viewport,
    const platform::WindowMetrics metrics
) {
    if (state_ == nullptr || !state_->initialized || state_->pipeline == nullptr ||
        metrics.width == 0U || metrics.height == 0U) {
        return false;
    }
    if (!viewport.drawable()) {
        return true;
    }

    const float scale_x = metrics.drawable_width > 0U
        ? static_cast<float>(metrics.drawable_width) / static_cast<float>(metrics.width)
        : std::max(1.0F, metrics.scale_factor);
    const float scale_y = metrics.drawable_height > 0U
        ? static_cast<float>(metrics.drawable_height) / static_cast<float>(metrics.height)
        : std::max(1.0F, metrics.scale_factor);

    const float inset_x = std::max(1.0F, scale_x);
    const float inset_y = std::max(1.0F, scale_y);
    const float viewport_x = viewport.x * scale_x + inset_x;
    const float viewport_y = viewport.y * scale_y + inset_y;
    const float viewport_width = std::max(1.0F, viewport.width * scale_x - inset_x * 2.0F);
    const float viewport_height = std::max(1.0F, viewport.height * scale_y - inset_y * 2.0F);

    const std::int32_t scissor_x = static_cast<std::int32_t>(std::floor(viewport_x));
    const std::int32_t scissor_y = static_cast<std::int32_t>(std::floor(viewport_y));
    const std::uint32_t scissor_width =
        static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_width)));
    const std::uint32_t scissor_height =
        static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_height)));

    if (!commands.set_viewport(render::Viewport{
            .x = viewport_x,
            .y = viewport_y,
            .width = viewport_width,
            .height = viewport_height,
        }) ||
        !commands.set_scissor_rect(render::ScissorRect{
            .x = scissor_x,
            .y = scissor_y,
            .width = scissor_width,
            .height = scissor_height,
        }) ||
        !commands.set_graphics_pipeline(*state_->pipeline)) {
        return false;
    }

    const float aspect = viewport_width / std::max(1.0F, viewport_height);
    const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
    const math::Mat4 view = math::look_at_rh(eye, state_->target, {0.0F, 1.0F, 0.0F});
    const math::Mat4 projection = math::perspective_rh_zo(scene_fov, aspect, 0.05F, 250.0F);
    const math::Mat4 view_projection = math::multiply(projection, view);

    const auto selected = selection.selected();
    const auto draw_object = [&](
        render::Buffer& vertices,
        render::Buffer& indices,
        const render::IndexFormat index_format,
        const std::uint32_t index_count,
        render::Buffer& constants_buffer,
        const math::Mat4& model,
        const bool highlighted,
        const std::array<float, 3> accent_color
    ) -> bool {
        SceneConstants constants;
        constants.view_projection = view_projection;
        constants.model = model;
        constants.accent = {
            accent_color[0],
            accent_color[1],
            accent_color[2],
            highlighted ? 0.55F : 0.0F,
        };
        return constants_buffer.write(0, std::as_bytes(std::span{&constants, 1U})) &&
            commands.set_vertex_buffer(0, vertices) &&
            commands.set_index_buffer(indices, index_format) &&
            commands.set_graphics_constant_buffer(
                scene_frame_binding,
                constants_buffer,
                0,
                sizeof(SceneConstants)
            ) &&
            commands.draw_indexed(index_count);
    };

    const auto draw_ground_region = [&](
        const world::RegionId region_id,
        const std::optional<world::PersistentId>& persistent_id,
        render::Buffer& constants_buffer,
        const std::array<float, 3> accent
    ) -> bool {
        const auto region = world.region(region_id);
        if (!region.has_value()) {
            return true;
        }
        const math::Vec3 origin{
            static_cast<float>(region->origin.x),
            static_cast<float>(region->origin.y),
            static_cast<float>(region->origin.z),
        };
        const bool highlighted = persistent_id.has_value() && selected.has_value() &&
            *persistent_id == *selected;
        return draw_object(
            *state_->ground_vertices,
            *state_->ground_indices,
            render::IndexFormat::uint16,
            static_cast<std::uint32_t>(ground_indices.size()),
            constants_buffer,
            model_matrix(origin),
            highlighted,
            accent
        );
    };

    if (!draw_ground_region(
            region_a_id,
            state_->terrain_a_id,
            *state_->ground_a_constants,
            {0.08F, 0.58F, 0.98F}
        ) ||
        !draw_ground_region(
            region_b_id,
            state_->terrain_b_id,
            *state_->ground_b_constants,
            {0.25F, 0.70F, 1.00F}
        )) {
        return false;
    }

    if (state_->ramp_vertices && state_->ramp_indices && state_->ramp_index_count > 0U) {
        math::Vec3 ramp_position{-8.0F, 0.0F, 0.0F};
        if (const auto region = world.region(region_a_id); region.has_value()) {
            ramp_position = {
                static_cast<float>(region->origin.x),
                static_cast<float>(region->origin.y),
                static_cast<float>(region->origin.z),
            };
        }
        const bool highlighted = state_->ramp_id.has_value() && selected.has_value() &&
            *state_->ramp_id == *selected;
        if (!draw_object(
                *state_->ramp_vertices,
                *state_->ramp_indices,
                render::IndexFormat::uint32,
                state_->ramp_index_count,
                *state_->ramp_constants,
                model_matrix(ramp_position),
                highlighted,
                {0.16F, 0.72F, 1.00F}
            )) {
            return false;
        }
    }

    const auto draw_role = [&](
        const std::optional<world::PersistentId>& persistent_id,
        render::Buffer& constants,
        const math::Vec3 scale,
        const math::Vec3 visual_offset,
        const std::array<float, 3> accent
    ) -> bool {
        if (!persistent_id.has_value()) {
            return true;
        }
        auto position = entity_position(world, *persistent_id);
        if (!position.has_value()) {
            return true;
        }
        *position = add(*position, visual_offset);
        const bool highlighted = selected.has_value() && *selected == *persistent_id;
        return draw_object(
            *state_->cube_vertices,
            *state_->cube_indices,
            render::IndexFormat::uint16,
            static_cast<std::uint32_t>(cube_indices.size()),
            constants,
            model_matrix(*position, scale),
            highlighted,
            accent
        );
    };

    return draw_role(
        state_->cube_id,
        *state_->cube_constants,
        {1.0F, 1.0F, 1.0F},
        {0.0F, 0.0F, 0.0F},
        {0.10F, 0.72F, 1.00F}
    ) && draw_role(
        state_->player_id,
        *state_->player_constants,
        {0.48F, 1.35F, 0.48F},
        {0.0F, 0.90F, 0.0F},
        {0.38F, 1.00F, 0.68F}
    );
}

} // namespace vice::editor::detail

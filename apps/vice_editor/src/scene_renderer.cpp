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
#include <vector>

namespace vice::editor::detail {
namespace {

using SceneVertex = render::MeshVertex;

constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";
constexpr std::string_view terrain_a_id_text = "10000000-0000-4000-8000-000000000003";
constexpr std::string_view terrain_b_id_text = "10000000-0000-4000-8000-000000000004";
constexpr std::string_view ramp_id_text = "10000000-0000-4000-8000-000000000005";
constexpr float scene_fov = 0.82F;
constexpr float ground_min_z = -10.0F;
constexpr float ground_max_z = 10.0F;
constexpr float pi = 3.14159265358979323846F;
constexpr std::size_t gizmo_ring_segments = 64U;

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
    {{-4.0F, -0.03F, ground_min_z}, {0.105F, 0.124F, 0.153F}},
    {{ 4.0F, -0.03F, ground_min_z}, {0.125F, 0.145F, 0.175F}},
    {{ 4.0F, -0.03F, ground_max_z}, {0.095F, 0.114F, 0.143F}},
    {{-4.0F, -0.03F, ground_max_z}, {0.115F, 0.134F, 0.163F}},
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
    std::array<float, 4> tint{};
    std::array<float, 4> accent{};
};

static_assert(sizeof(SceneConstants) == 160U);

constexpr std::string_view hlsl_scene_source = R"(
cbuffer SceneFrame : register(b0) {
    float4x4 view_projection;
    float4x4 model;
    float4 tint;
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
    float3 base_color = lerp(input.color, tint.rgb, saturate(tint.a));
    output.color = saturate(base_color + accent.rgb * accent.a);
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
    float4 tint;
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
    float3 base_color = mix(input.color, frame.tint.rgb, saturate(frame.tint.a));
    output.color = saturate(base_color + frame.accent.rgb * frame.accent.a);
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

[[nodiscard]] math::Vec3 component_multiply(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] float length(const math::Vec3 value) noexcept {
    return std::sqrt(math::dot(value, value));
}

[[nodiscard]] math::Vec3 axis_vector(const int axis) noexcept {
    if (axis == 0) return {1.0F, 0.0F, 0.0F};
    if (axis == 1) return {0.0F, 1.0F, 0.0F};
    return {0.0F, 0.0F, 1.0F};
}

[[nodiscard]] float axis_value(const math::Vec3 value, const int axis) noexcept {
    if (axis == 0) return value.x;
    if (axis == 1) return value.y;
    return value.z;
}

void set_axis_value(math::Vec3& value, const int axis, const float component) noexcept {
    if (axis == 0) value.x = component;
    else if (axis == 1) value.y = component;
    else value.z = component;
}

[[nodiscard]] math::Mat4 scale_matrix(const math::Vec3 value) noexcept {
    math::Mat4 result = math::identity();
    result(0, 0) = value.x;
    result(1, 1) = value.y;
    result(2, 2) = value.z;
    return result;
}

[[nodiscard]] math::Quat math_quaternion(const world::AuthoredRotation rotation) noexcept {
    return {rotation.x, rotation.y, rotation.z, rotation.w};
}

[[nodiscard]] world::AuthoredRotation authored_quaternion(const math::Quat value) noexcept {
    return {value.x, value.y, value.z, value.w};
}

[[nodiscard]] math::Quat normalize_quaternion(const math::Quat value) noexcept {
    const float magnitude = std::sqrt(
        value.x * value.x + value.y * value.y + value.z * value.z + value.w * value.w
    );
    if (magnitude <= 0.0F) return {};
    return {value.x / magnitude, value.y / magnitude, value.z / magnitude, value.w / magnitude};
}

[[nodiscard]] math::Quat multiply_quaternion(const math::Quat left, const math::Quat right) noexcept {
    return normalize_quaternion(math::Quat{
        left.w * right.x + left.x * right.w + left.y * right.z - left.z * right.y,
        left.w * right.y - left.x * right.z + left.y * right.w + left.z * right.x,
        left.w * right.z + left.x * right.y - left.y * right.x + left.z * right.w,
        left.w * right.w - left.x * right.x - left.y * right.y - left.z * right.z,
    });
}

[[nodiscard]] math::Quat axis_angle_quaternion(const int axis, const float angle) noexcept {
    const float half = angle * 0.5F;
    const float sine = std::sin(half);
    const auto direction = axis_vector(axis);
    return {direction.x * sine, direction.y * sine, direction.z * sine, std::cos(half)};
}

[[nodiscard]] math::Mat4 authored_model_matrix(
    const math::Vec3 position,
    const std::optional<world::AuthoredTransform>& authored,
    const math::Vec3 base_scale = {1.0F, 1.0F, 1.0F},
    const math::Vec3 visual_offset = {}
) noexcept {
    const world::AuthoredTransform transform = authored.value_or(world::AuthoredTransform{});
    auto result = math::translation(position);
    result = math::multiply(result, math::rotation(math_quaternion(transform.rotation)));
    result = math::multiply(
        result,
        scale_matrix({transform.scale.x, transform.scale.y, transform.scale.z})
    );
    result = math::multiply(result, math::translation(visual_offset));
    result = math::multiply(result, scale_matrix(base_scale));
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
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (far_distance < near_distance) return std::nullopt;
    }

    return far_distance >= 0.0F ? std::optional<float>{std::max(0.0F, near_distance)} : std::nullopt;
}

[[nodiscard]] std::optional<math::Vec3> entity_position(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) return std::nullopt;
    const auto spatial = world.spatial_state(*entity);
    if (!spatial.has_value()) return std::nullopt;
    return math::Vec3{
        static_cast<float>(spatial->global_position.x),
        static_cast<float>(spatial->global_position.y),
        static_cast<float>(spatial->global_position.z),
    };
}

[[nodiscard]] std::optional<world::AuthoredTransform> entity_authored_transform(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    return entity.has_value() ? world.authored_transform(*entity) : std::nullopt;
}

struct ScreenPoint {
    float x = 0.0F;
    float y = 0.0F;
    bool visible = false;
};

[[nodiscard]] ScreenPoint project_point(
    const math::Mat4& view_projection,
    const math::Vec3 point,
    const SceneViewport& viewport
) noexcept {
    const float clip_x = view_projection(0, 0) * point.x + view_projection(0, 1) * point.y +
        view_projection(0, 2) * point.z + view_projection(0, 3);
    const float clip_y = view_projection(1, 0) * point.x + view_projection(1, 1) * point.y +
        view_projection(1, 2) * point.z + view_projection(1, 3);
    const float clip_w = view_projection(3, 0) * point.x + view_projection(3, 1) * point.y +
        view_projection(3, 2) * point.z + view_projection(3, 3);
    if (clip_w <= 0.001F) return {};
    const float ndc_x = clip_x / clip_w;
    const float ndc_y = clip_y / clip_w;
    return {
        viewport.x + (ndc_x * 0.5F + 0.5F) * viewport.width,
        viewport.y + (0.5F - ndc_y * 0.5F) * viewport.height,
        true,
    };
}

[[nodiscard]] float screen_distance_to_segment(
    const float px,
    const float py,
    const ScreenPoint start,
    const ScreenPoint end
) noexcept {
    const float dx = end.x - start.x;
    const float dy = end.y - start.y;
    const float length_squared = dx * dx + dy * dy;
    if (length_squared <= 0.001F) return std::numeric_limits<float>::max();
    const float t = std::clamp(((px - start.x) * dx + (py - start.y) * dy) / length_squared, 0.0F, 1.0F);
    const float cx = start.x + dx * t;
    const float cy = start.y + dy * t;
    const float ex = px - cx;
    const float ey = py - cy;
    return std::sqrt(ex * ex + ey * ey);
}

[[nodiscard]] float screen_distance(const float px, const float py, const ScreenPoint point) noexcept {
    const float dx = px - point.x;
    const float dy = py - point.y;
    return std::sqrt(dx * dx + dy * dy);
}

[[nodiscard]] float gizmo_world_size(const math::Vec3 eye, const math::Vec3 position) noexcept {
    return std::clamp(length(position - eye) * 0.115F, 0.75F, 4.5F);
}

[[nodiscard]] math::Vec3 ring_point(
    const math::Vec3 center,
    const int axis,
    const float radius,
    const float angle
) noexcept {
    const float cosine = std::cos(angle) * radius;
    const float sine = std::sin(angle) * radius;
    if (axis == 0) return add(center, {0.0F, cosine, sine});
    if (axis == 1) return add(center, {cosine, 0.0F, sine});
    return add(center, {cosine, sine, 0.0F});
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

[[nodiscard]] std::pair<std::vector<SceneVertex>, std::vector<std::uint16_t>> build_ring_mesh() {
    std::vector<SceneVertex> vertices;
    std::vector<std::uint16_t> indices;
    vertices.reserve(gizmo_ring_segments * 2U);
    indices.reserve(gizmo_ring_segments * 6U);
    constexpr float inner_radius = 0.94F;
    constexpr float outer_radius = 1.0F;
    for (std::size_t index = 0; index < gizmo_ring_segments; ++index) {
        const float angle = static_cast<float>(index) * (2.0F * pi / static_cast<float>(gizmo_ring_segments));
        const float cosine = std::cos(angle);
        const float sine = std::sin(angle);
        vertices.push_back(SceneVertex{{cosine * inner_radius, sine * inner_radius, 0.0F}, {1.0F, 1.0F, 1.0F}});
        vertices.push_back(SceneVertex{{cosine * outer_radius, sine * outer_radius, 0.0F}, {1.0F, 1.0F, 1.0F}});
    }
    for (std::size_t index = 0; index < gizmo_ring_segments; ++index) {
        const std::uint16_t current_inner = static_cast<std::uint16_t>(index * 2U);
        const std::uint16_t current_outer = static_cast<std::uint16_t>(index * 2U + 1U);
        const std::uint16_t next_inner = static_cast<std::uint16_t>(((index + 1U) % gizmo_ring_segments) * 2U);
        const std::uint16_t next_outer = static_cast<std::uint16_t>(((index + 1U) % gizmo_ring_segments) * 2U + 1U);
        indices.insert(indices.end(), {current_inner, next_inner, current_outer, current_outer, next_inner, next_outer});
    }
    return {std::move(vertices), std::move(indices)};
}

} // namespace

struct SceneRenderer::State {
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::GraphicsPipeline> gizmo_pipeline;

    std::unique_ptr<render::Buffer> cube_vertices;
    std::unique_ptr<render::Buffer> cube_indices;
    std::unique_ptr<render::Buffer> ground_vertices;
    std::unique_ptr<render::Buffer> ground_indices;
    std::unique_ptr<render::Buffer> ramp_vertices;
    std::unique_ptr<render::Buffer> ramp_indices;
    std::unique_ptr<render::Buffer> ring_vertices;
    std::unique_ptr<render::Buffer> ring_indices;

    std::unique_ptr<render::Buffer> ground_a_constants;
    std::unique_ptr<render::Buffer> ground_b_constants;
    std::unique_ptr<render::Buffer> ramp_constants;
    std::unique_ptr<render::Buffer> cube_constants;
    std::unique_ptr<render::Buffer> player_constants;
    std::array<std::unique_ptr<render::Buffer>, 6> gizmo_constants;

    std::uint32_t ramp_index_count = 0;
    std::uint32_t ring_index_count = 0;
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

    int hovered_axis = -1;
    int active_axis = -1;
    TransformTool active_tool = TransformTool::move;
    std::optional<world::PersistentId> active_entity;
    std::optional<EntityTransformState> gizmo_before;
    double drag_start_x = 0.0;
    double drag_start_y = 0.0;
    float drag_axis_x = 0.0F;
    float drag_axis_y = 0.0F;
    float drag_axis_pixels = 1.0F;
    float drag_gizmo_size = 1.0F;
    float drag_origin_x = 0.0F;
    float drag_origin_y = 0.0F;
    float rotate_start_angle = 0.0F;
    bool initialized = false;
};

SceneRenderer::SceneRenderer() : state_{std::make_unique<State>()} {}
SceneRenderer::~SceneRenderer() = default;

bool SceneRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::filesystem::path& authored_mesh_path
) {
    if (state_ == nullptr) return false;

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
        device, render::BufferUsage::vertex, std::as_bytes(std::span{cube_vertices}), "VICE Editor cube vertices"
    );
    state_->cube_indices = create_buffer_with_data(
        device, render::BufferUsage::index, std::as_bytes(std::span{cube_indices}), "VICE Editor cube indices"
    );
    state_->ground_vertices = create_buffer_with_data(
        device, render::BufferUsage::vertex, std::as_bytes(std::span{ground_vertices}), "VICE Editor region ground vertices"
    );
    state_->ground_indices = create_buffer_with_data(
        device, render::BufferUsage::index, std::as_bytes(std::span{ground_indices}), "VICE Editor region ground indices"
    );

    const auto [ring_vertices, ring_indices] = build_ring_mesh();
    state_->ring_vertices = create_buffer_with_data(
        device, render::BufferUsage::vertex, std::as_bytes(std::span{ring_vertices}), "VICE Editor gizmo ring vertices"
    );
    state_->ring_indices = create_buffer_with_data(
        device, render::BufferUsage::index, std::as_bytes(std::span{ring_indices}), "VICE Editor gizmo ring indices"
    );
    state_->ring_index_count = static_cast<std::uint32_t>(ring_indices.size());

    if (!state_->cube_vertices || !state_->cube_indices || !state_->ground_vertices ||
        !state_->ground_indices || !state_->ring_vertices || !state_->ring_indices) {
        core::log(core::LogLevel::error, "VICE Editor could not create scene geometry buffers.");
        return false;
    }

    if (!authored_mesh_path.empty()) {
        auto loaded = render::load_mesh_asset(authored_mesh_path);
        if (loaded.asset.has_value() && !loaded.asset->vertices.empty() && !loaded.asset->indices.empty()) {
            state_->ramp_vertices = create_buffer_with_data(
                device, render::BufferUsage::vertex, std::as_bytes(std::span{loaded.asset->vertices}),
                "VICE Editor authored scene vertices"
            );
            state_->ramp_indices = create_buffer_with_data(
                device, render::BufferUsage::index, std::as_bytes(std::span{loaded.asset->indices}),
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
            core::log(core::LogLevel::warning, "VICE Editor could not load the authored scene mesh: " + loaded.diagnostics);
        }
    }

    state_->ground_a_constants = create_constant_buffer(device, "VICE Editor terrain A constants");
    state_->ground_b_constants = create_constant_buffer(device, "VICE Editor terrain B constants");
    state_->ramp_constants = create_constant_buffer(device, "VICE Editor ramp constants");
    state_->cube_constants = create_constant_buffer(device, "VICE Editor cube constants");
    state_->player_constants = create_constant_buffer(device, "VICE Editor player constants");
    for (std::size_t index = 0; index < state_->gizmo_constants.size(); ++index) {
        state_->gizmo_constants[index] = create_constant_buffer(device, "VICE Editor gizmo constants");
    }
    if (!state_->ground_a_constants || !state_->ground_b_constants || !state_->ramp_constants ||
        !state_->cube_constants || !state_->player_constants ||
        std::any_of(state_->gizmo_constants.begin(), state_->gizmo_constants.end(), [](const auto& value) { return !value; })) {
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal ? render::ShaderSourceLanguage::metal : render::ShaderSourceLanguage::hlsl;
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
    pipeline_desc.vertex_buffers = scene_layouts;
    pipeline_desc.debug_name = "VICE Editor scene pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);

    pipeline_desc.depth.test_enabled = false;
    pipeline_desc.depth.write_enabled = false;
    pipeline_desc.debug_name = "VICE Editor gizmo pipeline";
    state_->gizmo_pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline || !state_->gizmo_pipeline) return false;

    state_->initialized = true;
    return true;
}

void SceneRenderer::handle_event(
    const platform::Event& event,
    const SceneViewport& viewport,
    world::World& world,
    SelectionModel& selection,
    TransformHistory& history,
    const TransformTool tool
) {
    if (state_ == nullptr || !state_->initialized) return;

    const auto camera_matrices = [&]() {
        const float aspect = viewport.width / std::max(1.0F, viewport.height);
        const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
        const math::Mat4 view = math::look_at_rh(eye, state_->target, {0.0F, 1.0F, 0.0F});
        const math::Mat4 projection = math::perspective_rh_zo(scene_fov, aspect, 0.05F, 250.0F);
        return std::pair{eye, math::multiply(projection, view)};
    };

    const auto selected_position = [&]() -> std::optional<math::Vec3> {
        const auto selected = selection.selected();
        return selected.has_value() ? entity_position(world, *selected) : std::nullopt;
    };

    const auto gizmo_axis_at = [&](const double pointer_x, const double pointer_y) -> int {
        const auto selected = selection.selected();
        const auto center = selected_position();
        if (!selected.has_value() || !center.has_value() || !viewport.drawable()) return -1;
        const auto [eye, view_projection] = camera_matrices();
        const float size = gizmo_world_size(eye, *center);
        const ScreenPoint origin = project_point(view_projection, *center, viewport);
        if (!origin.visible) return -1;

        int best_axis = -1;
        float best_distance = tool == TransformTool::rotate ? 10.0F : 12.0F;
        for (int axis = 0; axis < 3; ++axis) {
            float distance = std::numeric_limits<float>::max();
            if (tool == TransformTool::rotate) {
                ScreenPoint previous = project_point(view_projection, ring_point(*center, axis, size, 0.0F), viewport);
                for (std::size_t segment = 1; segment <= gizmo_ring_segments; ++segment) {
                    const float angle = static_cast<float>(segment) * (2.0F * pi / static_cast<float>(gizmo_ring_segments));
                    const ScreenPoint current = project_point(view_projection, ring_point(*center, axis, size, angle), viewport);
                    if (previous.visible && current.visible) {
                        distance = std::min(
                            distance,
                            screen_distance_to_segment(
                                static_cast<float>(pointer_x), static_cast<float>(pointer_y), previous, current
                            )
                        );
                    }
                    previous = current;
                }
            } else {
                const ScreenPoint endpoint = project_point(
                    view_projection,
                    add(*center, scaled(axis_vector(axis), size)),
                    viewport
                );
                if (endpoint.visible) {
                    distance = std::min(
                        screen_distance_to_segment(
                            static_cast<float>(pointer_x), static_cast<float>(pointer_y), origin, endpoint
                        ),
                        screen_distance(static_cast<float>(pointer_x), static_cast<float>(pointer_y), endpoint)
                    );
                }
            }
            if (distance < best_distance) {
                best_distance = distance;
                best_axis = axis;
            }
        }
        return best_axis;
    };

    const auto pick_at = [&](const double pointer_x, const double pointer_y) {
        if (!viewport.drawable() || !contains(viewport, pointer_x, pointer_y)) return;

        const auto [eye, ignored_view_projection] = camera_matrices();
        (void)ignored_view_projection;
        const float aspect = viewport.width / std::max(1.0F, viewport.height);
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
            add(scaled(right, ndc_x * aspect * half_height), scaled(up, ndc_y * half_height))
        ));

        float nearest = std::numeric_limits<float>::max();
        std::optional<world::PersistentId> picked;
        const auto consider_bounds = [&](
            const std::optional<world::PersistentId>& persistent_id,
            const math::Vec3 minimum,
            const math::Vec3 maximum
        ) {
            if (!persistent_id.has_value()) return;
            const auto distance = ray_aabb_distance(eye, direction, minimum, maximum);
            if (distance.has_value() && *distance < nearest) {
                nearest = *distance;
                picked = *persistent_id;
            }
        };

        const auto consider_box_role = [&](
            const std::optional<world::PersistentId>& persistent_id,
            const math::Vec3 base_scale,
            const math::Vec3 visual_offset
        ) {
            if (!persistent_id.has_value()) return;
            const auto position = entity_position(world, *persistent_id);
            if (!position.has_value()) return;
            const auto authored = entity_authored_transform(world, *persistent_id).value_or(world::AuthoredTransform{});
            const math::Vec3 authored_scale{authored.scale.x, authored.scale.y, authored.scale.z};
            const math::Vec3 center = add(*position, component_multiply(visual_offset, authored_scale));
            const math::Vec3 half = component_multiply(
                component_multiply(base_scale, authored_scale),
                {0.65F, 0.65F, 0.65F}
            );
            consider_bounds(*persistent_id, center - half, add(center, half));
        };

        consider_box_role(state_->cube_id, {1.0F, 1.0F, 1.0F}, {});
        consider_box_role(state_->player_id, {0.48F, 1.35F, 0.48F}, {0.0F, 0.90F, 0.0F});

        if (state_->ramp_id.has_value() && state_->ramp_bounds_valid) {
            if (const auto position = entity_position(world, *state_->ramp_id); position.has_value()) {
                const auto authored = entity_authored_transform(world, *state_->ramp_id).value_or(world::AuthoredTransform{});
                const math::Vec3 scale{authored.scale.x, authored.scale.y, authored.scale.z};
                consider_bounds(
                    state_->ramp_id,
                    add(*position, component_multiply(state_->ramp_minimum, scale)),
                    add(*position, component_multiply(state_->ramp_maximum, scale))
                );
            }
        }

        const auto consider_terrain = [&](const std::optional<world::PersistentId>& persistent_id) {
            if (!persistent_id.has_value()) return;
            const auto position = entity_position(world, *persistent_id);
            if (!position.has_value()) return;
            const auto authored = entity_authored_transform(world, *persistent_id).value_or(world::AuthoredTransform{});
            if (std::abs(direction.y) <= 1.0e-6F) return;
            const float terrain_y = position->y - 0.03F * authored.scale.y;
            const float terrain_distance = (terrain_y - eye.y) / direction.y;
            if (terrain_distance < 0.0F || terrain_distance >= nearest) return;
            const math::Vec3 hit = add(eye, scaled(direction, terrain_distance));
            if (std::abs(hit.x - position->x) <= 4.0F * authored.scale.x &&
                hit.z >= position->z + ground_min_z * authored.scale.z &&
                hit.z <= position->z + ground_max_z * authored.scale.z) {
                nearest = terrain_distance;
                picked = *persistent_id;
            }
        };
        consider_terrain(state_->terrain_a_id);
        consider_terrain(state_->terrain_b_id);

        if (picked.has_value()) selection.select(*picked);
        else selection.clear();
    };

    const auto cancel_gizmo = [&]() {
        if (state_->active_entity.has_value() && state_->gizmo_before.has_value()) {
            (void)apply_entity_transform(world, *state_->active_entity, *state_->gizmo_before);
        }
        state_->active_axis = -1;
        state_->active_entity.reset();
        state_->gizmo_before.reset();
    };

    std::visit(
        [&](const auto& payload) {
            using Payload = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<Payload, platform::WindowFocusChanged>) {
                if (!payload.focused) {
                    cancel_gizmo();
                    state_->right_dragging = false;
                    state_->middle_dragging = false;
                    state_->forward = state_->backward = state_->left = state_->right = false;
                    state_->down = state_->up = false;
                    state_->hovered_axis = -1;
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseButtonEvent>) {
                if (payload.button == platform::MouseButton::left) {
                    if (payload.pressed) {
                        if (!viewport.hovered || !contains(viewport, payload.x, payload.y)) return;
                        const int axis = gizmo_axis_at(payload.x, payload.y);
                        const auto selected = selection.selected();
                        const auto before = selected.has_value()
                            ? capture_entity_transform(world, *selected)
                            : std::optional<EntityTransformState>{};
                        if (axis >= 0 && selected.has_value() && before.has_value()) {
                            const auto [eye, view_projection] = camera_matrices();
                            const math::Vec3 center{
                                static_cast<float>(before->spatial_state.global_position.x),
                                static_cast<float>(before->spatial_state.global_position.y),
                                static_cast<float>(before->spatial_state.global_position.z),
                            };
                            const float size = gizmo_world_size(eye, center);
                            const ScreenPoint origin = project_point(view_projection, center, viewport);
                            const ScreenPoint endpoint = project_point(
                                view_projection,
                                add(center, scaled(axis_vector(axis), size)),
                                viewport
                            );
                            state_->active_axis = axis;
                            state_->active_tool = tool;
                            state_->active_entity = *selected;
                            state_->gizmo_before = *before;
                            state_->drag_start_x = payload.x;
                            state_->drag_start_y = payload.y;
                            state_->drag_gizmo_size = size;
                            state_->drag_origin_x = origin.x;
                            state_->drag_origin_y = origin.y;
                            state_->rotate_start_angle = std::atan2(
                                static_cast<float>(payload.y) - origin.y,
                                static_cast<float>(payload.x) - origin.x
                            );
                            const float dx = endpoint.x - origin.x;
                            const float dy = endpoint.y - origin.y;
                            state_->drag_axis_pixels = std::max(1.0F, std::sqrt(dx * dx + dy * dy));
                            state_->drag_axis_x = dx / state_->drag_axis_pixels;
                            state_->drag_axis_y = dy / state_->drag_axis_pixels;
                            state_->hovered_axis = axis;
                            return;
                        }
                        if (!state_->right_dragging && !state_->middle_dragging) pick_at(payload.x, payload.y);
                    } else if (state_->active_axis >= 0 && state_->active_entity.has_value() && state_->gizmo_before.has_value()) {
                        const auto after = capture_entity_transform(world, *state_->active_entity);
                        if (after.has_value()) {
                            std::string label = "Move Entity";
                            if (state_->active_tool == TransformTool::rotate) label = "Rotate Entity";
                            if (state_->active_tool == TransformTool::scale) label = "Scale Entity";
                            history.record(TransformHistory::Entry{
                                .persistent_id = *state_->active_entity,
                                .before = *state_->gizmo_before,
                                .after = *after,
                                .label = std::move(label),
                            });
                        }
                        state_->active_axis = -1;
                        state_->active_entity.reset();
                        state_->gizmo_before.reset();
                    }
                } else if (payload.button == platform::MouseButton::right) {
                    state_->right_dragging = payload.pressed
                        ? viewport.hovered && state_->active_axis < 0
                        : false;
                } else if (payload.button == platform::MouseButton::middle) {
                    state_->middle_dragging = payload.pressed
                        ? viewport.hovered && state_->active_axis < 0
                        : false;
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseMoveEvent>) {
                if (state_->active_axis >= 0 && state_->active_entity.has_value() && state_->gizmo_before.has_value()) {
                    EntityTransformState after = *state_->gizmo_before;
                    const float dx = static_cast<float>(payload.x - state_->drag_start_x);
                    const float dy = static_cast<float>(payload.y - state_->drag_start_y);
                    if (state_->active_tool == TransformTool::move) {
                        const float pixels = dx * state_->drag_axis_x + dy * state_->drag_axis_y;
                        const float delta = pixels / state_->drag_axis_pixels * state_->drag_gizmo_size;
                        auto position = math::Vec3{
                            static_cast<float>(after.spatial_state.global_position.x),
                            static_cast<float>(after.spatial_state.global_position.y),
                            static_cast<float>(after.spatial_state.global_position.z),
                        };
                        set_axis_value(position, state_->active_axis, axis_value(position, state_->active_axis) + delta);
                        after.spatial_state.global_position = {position.x, position.y, position.z};
                        if (const auto region = world.region_containing(after.spatial_state.global_position)) {
                            after.spatial_state.region = *region;
                        }
                    } else if (state_->active_tool == TransformTool::scale) {
                        const float pixels = dx * state_->drag_axis_x + dy * state_->drag_axis_y;
                        world::AuthoredTransform authored = after.authored_transform.value_or(world::AuthoredTransform{});
                        math::Vec3 scale{authored.scale.x, authored.scale.y, authored.scale.z};
                        const float start = axis_value(scale, state_->active_axis);
                        const float factor = std::exp(pixels / 140.0F);
                        set_axis_value(scale, state_->active_axis, std::max(0.01F, start * factor));
                        authored.scale = {scale.x, scale.y, scale.z};
                        after.authored_transform = authored;
                    } else {
                        const float angle = std::atan2(
                            static_cast<float>(payload.y) - state_->drag_origin_y,
                            static_cast<float>(payload.x) - state_->drag_origin_x
                        ) - state_->rotate_start_angle;
                        world::AuthoredTransform authored = after.authored_transform.value_or(world::AuthoredTransform{});
                        const math::Quat rotated = multiply_quaternion(
                            axis_angle_quaternion(state_->active_axis, angle),
                            math_quaternion(authored.rotation)
                        );
                        authored.rotation = authored_quaternion(rotated);
                        after.authored_transform = authored;
                    }
                    if (!apply_entity_transform(world, *state_->active_entity, after)) {
                        core::log(core::LogLevel::error, "VICE Editor could not apply a transform gizmo edit.");
                        cancel_gizmo();
                    }
                    return;
                }

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
                } else {
                    state_->hovered_axis = viewport.hovered ? gizmo_axis_at(payload.x, payload.y) : -1;
                }
            } else if constexpr (std::is_same_v<Payload, platform::MouseWheelEvent>) {
                if (viewport.hovered && state_->active_axis < 0) {
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

void SceneRenderer::update(const float delta_seconds, const SceneViewport& viewport) {
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

bool SceneRenderer::render(
    const world::World& world,
    const SelectionModel& selection,
    const TransformTool tool,
    render::CommandList& commands,
    const SceneViewport& viewport,
    const platform::WindowMetrics metrics
) {
    if (state_ == nullptr || !state_->initialized || state_->pipeline == nullptr ||
        state_->gizmo_pipeline == nullptr || metrics.width == 0U || metrics.height == 0U) {
        return false;
    }
    if (!viewport.drawable()) return true;

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
    const std::uint32_t scissor_width = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_width)));
    const std::uint32_t scissor_height = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_height)));

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
        const std::array<float, 4> tint,
        const bool highlighted,
        const std::array<float, 3> accent_color
    ) -> bool {
        SceneConstants constants;
        constants.view_projection = view_projection;
        constants.model = model;
        constants.tint = tint;
        constants.accent = {
            accent_color[0], accent_color[1], accent_color[2], highlighted ? 0.55F : 0.0F,
        };
        return constants_buffer.write(0, std::as_bytes(std::span{&constants, 1U})) &&
            commands.set_vertex_buffer(0, vertices) &&
            commands.set_index_buffer(indices, index_format) &&
            commands.set_graphics_constant_buffer(scene_frame_binding, constants_buffer, 0, sizeof(SceneConstants)) &&
            commands.draw_indexed(index_count);
    };

    const auto role_model = [&](
        const std::optional<world::PersistentId>& persistent_id,
        const math::Vec3 fallback_position,
        const math::Vec3 base_scale = math::Vec3{1.0F, 1.0F, 1.0F},
        const math::Vec3 visual_offset = math::Vec3{}
    ) {
        math::Vec3 position = fallback_position;
        std::optional<world::AuthoredTransform> authored;
        if (persistent_id.has_value()) {
            if (const auto actual = entity_position(world, *persistent_id); actual.has_value()) position = *actual;
            authored = entity_authored_transform(world, *persistent_id);
        }
        return authored_model_matrix(position, authored, base_scale, visual_offset);
    };

    const auto draw_ground_role = [&](
        const std::optional<world::PersistentId>& persistent_id,
        render::Buffer& constants_buffer,
        const math::Vec3 fallback,
        const std::array<float, 3> accent
    ) -> bool {
        const bool highlighted = persistent_id.has_value() && selected.has_value() && *persistent_id == *selected;
        return draw_object(
            *state_->ground_vertices,
            *state_->ground_indices,
            render::IndexFormat::uint16,
            static_cast<std::uint32_t>(ground_indices.size()),
            constants_buffer,
            role_model(persistent_id, fallback),
            {0.0F, 0.0F, 0.0F, 0.0F},
            highlighted,
            accent
        );
    };

    if (!draw_ground_role(state_->terrain_a_id, *state_->ground_a_constants, {-4.0F, 0.0F, 0.0F}, {0.08F, 0.58F, 0.98F}) ||
        !draw_ground_role(state_->terrain_b_id, *state_->ground_b_constants, {4.0F, 0.0F, 0.0F}, {0.25F, 0.70F, 1.00F})) {
        return false;
    }

    if (state_->ramp_vertices && state_->ramp_indices && state_->ramp_index_count > 0U) {
        const bool highlighted = state_->ramp_id.has_value() && selected.has_value() && *state_->ramp_id == *selected;
        if (!draw_object(
                *state_->ramp_vertices,
                *state_->ramp_indices,
                render::IndexFormat::uint32,
                state_->ramp_index_count,
                *state_->ramp_constants,
                role_model(state_->ramp_id, {-8.0F, 0.0F, 0.0F}),
                {0.0F, 0.0F, 0.0F, 0.0F},
                highlighted,
                {0.16F, 0.72F, 1.00F}
            )) {
            return false;
        }
    }

    const auto draw_role = [&](
        const std::optional<world::PersistentId>& persistent_id,
        render::Buffer& constants,
        const math::Vec3 base_scale,
        const math::Vec3 visual_offset,
        const std::array<float, 3> accent
    ) -> bool {
        if (!persistent_id.has_value()) return true;
        const bool highlighted = selected.has_value() && *selected == *persistent_id;
        return draw_object(
            *state_->cube_vertices,
            *state_->cube_indices,
            render::IndexFormat::uint16,
            static_cast<std::uint32_t>(cube_indices.size()),
            constants,
            role_model(persistent_id, {}, base_scale, visual_offset),
            {0.0F, 0.0F, 0.0F, 0.0F},
            highlighted,
            accent
        );
    };

    if (!draw_role(state_->cube_id, *state_->cube_constants, {1.0F, 1.0F, 1.0F}, {}, {0.10F, 0.72F, 1.00F}) ||
        !draw_role(state_->player_id, *state_->player_constants, {0.48F, 1.35F, 0.48F}, {0.0F, 0.90F, 0.0F}, {0.38F, 1.00F, 0.68F})) {
        return false;
    }

    if (!selected.has_value()) return true;
    const auto position = entity_position(world, *selected);
    if (!position.has_value()) return true;

    if (!commands.set_graphics_pipeline(*state_->gizmo_pipeline)) return false;

    const float size = gizmo_world_size(eye, *position);
    const float thickness = std::clamp(size * 0.035F, 0.03F, 0.12F);
    static constexpr std::array<std::array<float, 3>, 3> axis_colors{{
        {0.96F, 0.30F, 0.40F},
        {0.32F, 0.88F, 0.48F},
        {0.24F, 0.58F, 1.00F},
    }};
    const auto axis_color = [&](const int axis) {
        auto color = axis_colors[static_cast<std::size_t>(axis)];
        if (state_->active_axis == axis) {
            color = {1.0F, 0.92F, 0.55F};
        } else if (state_->hovered_axis == axis) {
            color = {
                std::min(1.0F, color[0] + 0.28F),
                std::min(1.0F, color[1] + 0.28F),
                std::min(1.0F, color[2] + 0.28F),
            };
        }
        return color;
    };

    if (tool == TransformTool::rotate) {
        for (int axis = 0; axis < 3; ++axis) {
            math::Mat4 model = math::translation(*position);
            if (axis == 0) model = math::multiply(model, math::rotation_y(pi * 0.5F));
            else if (axis == 1) model = math::multiply(model, math::rotation_x(-pi * 0.5F));
            model = math::multiply(model, scale_matrix({size, size, size}));
            const auto color = axis_color(axis);
            if (!draw_object(
                    *state_->ring_vertices,
                    *state_->ring_indices,
                    render::IndexFormat::uint16,
                    state_->ring_index_count,
                    *state_->gizmo_constants[static_cast<std::size_t>(axis)],
                    model,
                    {color[0], color[1], color[2], 1.0F},
                    false,
                    {}
                )) {
                return false;
            }
        }
        return true;
    }

    for (int axis = 0; axis < 3; ++axis) {
        const math::Vec3 direction = axis_vector(axis);
        const auto color = axis_color(axis);
        math::Vec3 stem_scale{thickness, thickness, thickness};
        set_axis_value(stem_scale, axis, size * 0.62F);
        const math::Vec3 stem_center = add(*position, scaled(direction, size * 0.40F));
        if (!draw_object(
                *state_->cube_vertices,
                *state_->cube_indices,
                render::IndexFormat::uint16,
                static_cast<std::uint32_t>(cube_indices.size()),
                *state_->gizmo_constants[static_cast<std::size_t>(axis * 2)],
                authored_model_matrix(stem_center, std::nullopt, stem_scale),
                {color[0], color[1], color[2], 1.0F},
                false,
                {}
            )) {
            return false;
        }

        const float cap = size * (tool == TransformTool::scale ? 0.14F : 0.11F);
        const math::Vec3 endpoint = add(*position, scaled(direction, size));
        if (!draw_object(
                *state_->cube_vertices,
                *state_->cube_indices,
                render::IndexFormat::uint16,
                static_cast<std::uint32_t>(cube_indices.size()),
                *state_->gizmo_constants[static_cast<std::size_t>(axis * 2 + 1)],
                authored_model_matrix(endpoint, std::nullopt, {cap, cap, cap}),
                {color[0], color[1], color[2], 1.0F},
                false,
                {}
            )) {
            return false;
        }
    }
    return true;
}

} // namespace vice::editor::detail

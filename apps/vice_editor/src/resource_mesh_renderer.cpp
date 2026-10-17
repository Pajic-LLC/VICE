#include "resource_mesh_renderer.hpp"

#include <vice/core/log.hpp>
#include <vice/editor/mesh_picking.hpp>
#include <vice/math/math.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/mesh_input.hpp>

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
#include <unordered_map>
#include <utility>
#include <variant>

namespace vice::editor::detail {
namespace {

constexpr float scene_fov = 0.82F;
constexpr float ground_min_z = -10.0F;
constexpr float ground_max_z = 10.0F;
constexpr float pi = 3.14159265358979323846F;
constexpr std::size_t gizmo_ring_segments = 64U;
constexpr std::string_view player_id_text = "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_id_text = "10000000-0000-4000-8000-000000000002";
constexpr std::string_view terrain_a_id_text = "10000000-0000-4000-8000-000000000003";
constexpr std::string_view terrain_b_id_text = "10000000-0000-4000-8000-000000000004";

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

[[nodiscard]] constexpr math::Vec3 add(const math::Vec3 left, const math::Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] constexpr math::Vec3 component_multiply(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return {left.x * right.x, left.y * right.y, left.z * right.z};
}

[[nodiscard]] float length(const math::Vec3 value) noexcept {
    return std::sqrt(math::dot(value, value));
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

[[nodiscard]] bool contains(const SceneViewport& viewport, const double x, const double y) noexcept {
    return viewport.drawable() && x >= viewport.x && y >= viewport.y &&
        x < static_cast<double>(viewport.x + viewport.width) &&
        y < static_cast<double>(viewport.y + viewport.height);
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

[[nodiscard]] math::Vec3 axis_vector(const int axis) noexcept {
    if (axis == 0) return {1.0F, 0.0F, 0.0F};
    if (axis == 1) return {0.0F, 1.0F, 0.0F};
    return {0.0F, 0.0F, 1.0F};
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

[[nodiscard]] float gizmo_world_size(const math::Vec3 eye, const math::Vec3 position) noexcept {
    return std::clamp(length(position - eye) * 0.115F, 0.75F, 4.5F);
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

[[nodiscard]] std::optional<float> ray_aabb_distance(
    const math::Vec3 origin,
    const math::Vec3 direction,
    const math::Vec3 minimum,
    const math::Vec3 maximum
) noexcept {
    float near_distance = 0.0F;
    float far_distance = std::numeric_limits<float>::max();
    const auto axis = [](const math::Vec3 value, const int index) noexcept {
        if (index == 0) return value.x;
        if (index == 1) return value.y;
        return value.z;
    };
    for (int index = 0; index < 3; ++index) {
        const float ray_origin = axis(origin, index);
        const float ray_direction = axis(direction, index);
        const float minimum_axis = axis(minimum, index);
        const float maximum_axis = axis(maximum, index);
        if (std::abs(ray_direction) < 1.0e-6F) {
            if (ray_origin < minimum_axis || ray_origin > maximum_axis) return std::nullopt;
            continue;
        }
        float first = (minimum_axis - ray_origin) / ray_direction;
        float second = (maximum_axis - ray_origin) / ray_direction;
        if (first > second) std::swap(first, second);
        near_distance = std::max(near_distance, first);
        far_distance = std::min(far_distance, second);
        if (far_distance < near_distance) return std::nullopt;
    }
    return far_distance >= 0.0F
        ? std::optional<float>{std::max(0.0F, near_distance)}
        : std::nullopt;
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
    std::optional<world::PersistentId> player_id;
    std::optional<world::PersistentId> cube_id;
    std::optional<world::PersistentId> terrain_a_id;
    std::optional<world::PersistentId> terrain_b_id;

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
    state_->player_id = world::PersistentId::parse(player_id_text);
    state_->cube_id = world::PersistentId::parse(cube_id_text);
    state_->terrain_a_id = world::PersistentId::parse(terrain_a_id_text);
    state_->terrain_b_id = world::PersistentId::parse(terrain_b_id_text);
    if (!state_->player_id.has_value() || !state_->cube_id.has_value() ||
        !state_->terrain_a_id.has_value() || !state_->terrain_b_id.has_value()) {
        return false;
    }

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
    const SceneViewport& viewport,
    const world::World& world,
    SelectionModel& selection,
    const AssetIndex& assets,
    const std::optional<world::PersistentId> selection_before_event
) {
    if (state_ == nullptr || !state_->initialized) return;

    const auto camera_matrices = [&]() {
        const float aspect = viewport.width / std::max(1.0F, viewport.height);
        const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
        const math::Mat4 view = math::look_at_rh(eye, state_->target, {0.0F, 1.0F, 0.0F});
        const math::Mat4 projection = math::perspective_rh_zo(scene_fov, aspect, 0.05F, 250.0F);
        return std::pair{eye, math::multiply(projection, view)};
    };

    const auto pointer_near_previous_gizmo = [&](const double pointer_x, const double pointer_y) {
        if (!selection_before_event.has_value() || !viewport.drawable()) return false;
        const auto center = entity_position(world, *selection_before_event);
        if (!center.has_value()) return false;

        const auto [eye, view_projection] = camera_matrices();
        const float size = gizmo_world_size(eye, *center);
        const ScreenPoint origin = project_point(view_projection, *center, viewport);
        if (!origin.visible) return false;

        constexpr float threshold = 13.0F;
        for (int axis = 0; axis < 3; ++axis) {
            const ScreenPoint endpoint = project_point(
                view_projection,
                add(*center, scaled(axis_vector(axis), size)),
                viewport
            );
            if (endpoint.visible && screen_distance_to_segment(
                    static_cast<float>(pointer_x),
                    static_cast<float>(pointer_y),
                    origin,
                    endpoint
                ) <= threshold) {
                return true;
            }

            ScreenPoint previous = project_point(
                view_projection,
                ring_point(*center, axis, size, 0.0F),
                viewport
            );
            for (std::size_t segment = 1; segment <= gizmo_ring_segments; ++segment) {
                const float angle = static_cast<float>(segment) *
                    (2.0F * pi / static_cast<float>(gizmo_ring_segments));
                const ScreenPoint current = project_point(
                    view_projection,
                    ring_point(*center, axis, size, angle),
                    viewport
                );
                if (previous.visible && current.visible && screen_distance_to_segment(
                        static_cast<float>(pointer_x),
                        static_cast<float>(pointer_y),
                        previous,
                        current
                    ) <= threshold) {
                    return true;
                }
                previous = current;
            }
        }
        return false;
    };

    const auto camera_ray = [&](const double pointer_x, const double pointer_y)
        -> std::optional<std::pair<math::Vec3, math::Vec3>> {
        if (!viewport.drawable() || !contains(viewport, pointer_x, pointer_y)) return std::nullopt;
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
        return std::pair{eye, direction};
    };

    const auto nearest_legacy_hit = [&](const math::Vec3 origin, const math::Vec3 direction) {
        float nearest = std::numeric_limits<float>::max();
        const auto consider_box = [&](
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
            if (const auto distance = ray_aabb_distance(origin, direction, center - half, add(center, half));
                distance.has_value()) {
                nearest = std::min(nearest, *distance);
            }
        };
        consider_box(state_->cube_id, {1.0F, 1.0F, 1.0F}, {});
        consider_box(state_->player_id, {0.48F, 1.35F, 0.48F}, {0.0F, 0.90F, 0.0F});

        const auto consider_terrain = [&](const std::optional<world::PersistentId>& persistent_id) {
            if (!persistent_id.has_value()) return;
            const auto position = entity_position(world, *persistent_id);
            if (!position.has_value()) return;
            const auto authored = entity_authored_transform(world, *persistent_id).value_or(world::AuthoredTransform{});
            if (std::abs(direction.y) <= 1.0e-6F) return;
            const float terrain_y = position->y - 0.03F * authored.scale.y;
            const float distance = (terrain_y - origin.y) / direction.y;
            if (distance < 0.0F || distance >= nearest) return;
            const math::Vec3 hit = add(origin, scaled(direction, distance));
            if (std::abs(hit.x - position->x) <= 4.0F * authored.scale.x &&
                hit.z >= position->z + ground_min_z * authored.scale.z &&
                hit.z <= position->z + ground_max_z * authored.scale.z) {
                nearest = distance;
            }
        };
        consider_terrain(state_->terrain_a_id);
        consider_terrain(state_->terrain_b_id);
        return nearest;
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
                if (payload.button == platform::MouseButton::left && payload.pressed && viewport.hovered &&
                    !pointer_near_previous_gizmo(payload.x, payload.y)) {
                    const auto ray = camera_ray(payload.x, payload.y);
                    if (ray.has_value()) {
                        const auto picked = pick_render_mesh(world, assets, ray->first, ray->second);
                        if (picked.has_value() && picked->distance <= nearest_legacy_hit(ray->first, ray->second)) {
                            selection.select(picked->persistent_id);
                        }
                    }
                } else if (payload.button == platform::MouseButton::right) {
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

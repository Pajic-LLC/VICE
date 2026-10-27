#include "physics_debug_renderer.hpp"

#include <vice/math/math.hpp>
#include <vice/render/mesh_input.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace vice::editor::detail {
namespace {

constexpr float scene_fov = 0.82F;
constexpr std::size_t max_debug_vertices = 65'536U;
constexpr std::size_t max_debug_indices = 196'608U;

struct DebugVertex {
    std::array<float, 3> position{};
    std::array<float, 4> color{};
};

struct DebugConstants {
    math::Mat4 view_projection{};
};

static_assert(sizeof(DebugConstants) == 64U);

static constexpr std::array debug_attributes{
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
        .format = render::VertexFormat::float32x4,
        .offset = 12,
    },
};

static constexpr std::array debug_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(DebugVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = debug_attributes,
    },
};

static constexpr std::array debug_resources{
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

constexpr std::string_view hlsl_source = R"(
cbuffer DebugFrame : register(b0) {
    float4x4 view_projection;
};

struct VertexIn {
    float3 position : POSITION0;
    float4 color : COLOR0;
};

struct VertexOut {
    float4 position : SV_Position;
    float4 color : COLOR0;
};

VertexOut vice_editor_debug_vs(VertexIn input) {
    VertexOut output;
    output.position = mul(view_projection, float4(input.position, 1.0));
    output.color = input.color;
    return output;
}

float4 vice_editor_debug_fs(VertexOut input) : SV_Target0 {
    return input.color;
}
)";

constexpr std::string_view metal_source = R"(
#include <metal_stdlib>
using namespace metal;

struct DebugFrame {
    float4x4 view_projection;
};

struct VertexIn {
    float3 position [[attribute(11)]];
    float4 color [[attribute(12)]];
};

struct VertexOut {
    float4 position [[position]];
    float4 color;
};

vertex VertexOut vice_editor_debug_vs(
    VertexIn input [[stage_in]],
    constant DebugFrame& frame [[buffer(0)]]
) {
    VertexOut output;
    output.position = frame.view_projection * float4(input.position, 1.0);
    output.color = input.color;
    return output;
}

fragment float4 vice_editor_debug_fs(VertexOut input [[stage_in]]) {
    return input.color;
}
)";

[[nodiscard]] constexpr math::Vec3 add(const math::Vec3 a, const math::Vec3 b) noexcept {
    return {a.x + b.x, a.y + b.y, a.z + b.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(const math::Vec3 value, const float scale) noexcept {
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

[[nodiscard]] math::Vec3 float_position(const world::GlobalPosition value) noexcept {
    return {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
}

[[nodiscard]] math::Vec3 rotate(const math::Quat q, const math::Vec3 value) noexcept {
    const math::Vec3 u{q.x, q.y, q.z};
    const float uu = math::dot(u, u);
    const float uv = math::dot(u, value);
    return add(
        add(scaled(u, 2.0F * uv), scaled(value, q.w * q.w - uu)),
        scaled(math::cross(u, value), 2.0F * q.w)
    );
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

void append_vertex(
    std::vector<DebugVertex>& vertices,
    const math::Vec3 position,
    const std::array<float, 4> color
) {
    vertices.push_back(DebugVertex{
        .position = {position.x, position.y, position.z},
        .color = color,
    });
}

void add_segment(
    std::vector<DebugVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const math::Vec3 start,
    const math::Vec3 end,
    const float thickness,
    const std::array<float, 4> color
) {
    if (vertices.size() + 8U > max_debug_vertices || indices.size() + 36U > max_debug_indices) return;
    math::Vec3 direction = end - start;
    const float length_squared = math::dot(direction, direction);
    if (length_squared <= 1.0e-10F) return;
    direction = math::normalize(direction);
    const math::Vec3 reference = std::abs(direction.y) < 0.9F
        ? math::Vec3{0.0F, 1.0F, 0.0F}
        : math::Vec3{1.0F, 0.0F, 0.0F};
    const math::Vec3 side = scaled(math::normalize(math::cross(direction, reference)), thickness);
    const math::Vec3 up = scaled(math::normalize(math::cross(side, direction)), thickness);

    const std::uint32_t base = static_cast<std::uint32_t>(vertices.size());
    append_vertex(vertices, start - side - up, color);
    append_vertex(vertices, add(start - side, up), color);
    append_vertex(vertices, add(start, side) - up, color);
    append_vertex(vertices, add(add(start, side), up), color);
    append_vertex(vertices, end - side - up, color);
    append_vertex(vertices, add(end - side, up), color);
    append_vertex(vertices, add(end, side) - up, color);
    append_vertex(vertices, add(add(end, side), up), color);

    static constexpr std::array<std::uint32_t, 36> cube_indices{
        0, 1, 3, 0, 3, 2,
        4, 6, 7, 4, 7, 5,
        0, 4, 5, 0, 5, 1,
        2, 3, 7, 2, 7, 6,
        1, 5, 7, 1, 7, 3,
        0, 2, 6, 0, 6, 4,
    };
    for (const auto index : cube_indices) indices.push_back(base + index);
}

void add_box(
    std::vector<DebugVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const math::Vec3 center,
    const math::Vec3 half,
    const math::Quat orientation,
    const float thickness,
    const std::array<float, 4> color
) {
    std::array<math::Vec3, 8> corners{};
    std::size_t cursor = 0;
    for (const float x : {-half.x, half.x}) {
        for (const float y : {-half.y, half.y}) {
            for (const float z : {-half.z, half.z}) {
                corners[cursor++] = add(center, rotate(orientation, {x, y, z}));
            }
        }
    }
    static constexpr std::array<std::array<std::size_t, 2>, 12> edges{{
        {{0, 1}}, {{0, 2}}, {{0, 4}}, {{1, 3}}, {{1, 5}}, {{2, 3}},
        {{2, 6}}, {{3, 7}}, {{4, 5}}, {{4, 6}}, {{5, 7}}, {{6, 7}},
    }};
    for (const auto edge : edges) add_segment(vertices, indices, corners[edge[0]], corners[edge[1]], thickness, color);
}

void add_ring(
    std::vector<DebugVertex>& vertices,
    std::vector<std::uint32_t>& indices,
    const math::Vec3 center,
    const float radius,
    const float thickness,
    const std::array<float, 4> color
) {
    constexpr std::size_t segments = 20U;
    math::Vec3 previous{center.x + radius, center.y, center.z};
    for (std::size_t index = 1; index <= segments; ++index) {
        const float angle = static_cast<float>(index) * (6.28318530718F / static_cast<float>(segments));
        const math::Vec3 current{
            center.x + std::cos(angle) * radius,
            center.y,
            center.z + std::sin(angle) * radius,
        };
        add_segment(vertices, indices, previous, current, thickness, color);
        previous = current;
    }
}

[[nodiscard]] std::optional<math::Vec3> region_center(
    const PreviewSnapshot& preview,
    const world::RegionId region
) noexcept {
    const auto found = std::find_if(
        preview.regions.begin(), preview.regions.end(),
        [region](const PreviewRegionSnapshot& value) { return value.region.id == region; }
    );
    if (found == preview.regions.end()) return std::nullopt;
    if (found->global_minimum.has_value() && found->global_maximum.has_value()) {
        const auto minimum = float_position(*found->global_minimum);
        const auto maximum = float_position(*found->global_maximum);
        return scaled(add(minimum, maximum), 0.5F);
    }
    return float_position(found->region.origin);
}

} // namespace

struct PhysicsDebugRenderer::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::Buffer> vertices;
    std::unique_ptr<render::Buffer> indices;
    std::unique_ptr<render::Buffer> constants;

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

PhysicsDebugRenderer::PhysicsDebugRenderer() : state_{std::make_unique<State>()} {}
PhysicsDebugRenderer::~PhysicsDebugRenderer() = default;

bool PhysicsDebugRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format
) {
    if (state_ == nullptr) return false;
    state_->device = &device;

    render::BufferDesc vertex_desc;
    vertex_desc.size = max_debug_vertices * sizeof(DebugVertex);
    vertex_desc.usage = render::BufferUsage::vertex;
    vertex_desc.memory = render::MemoryLocation::cpu_to_gpu;
    vertex_desc.debug_name = "VICE Editor debug vertices";
    state_->vertices = device.create_buffer(vertex_desc);

    render::BufferDesc index_desc;
    index_desc.size = max_debug_indices * sizeof(std::uint32_t);
    index_desc.usage = render::BufferUsage::index;
    index_desc.memory = render::MemoryLocation::cpu_to_gpu;
    index_desc.debug_name = "VICE Editor debug indices";
    state_->indices = device.create_buffer(index_desc);

    render::BufferDesc constants_desc;
    constants_desc.size = sizeof(DebugConstants);
    constants_desc.usage = render::BufferUsage::uniform;
    constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
    constants_desc.debug_name = "VICE Editor debug constants";
    state_->constants = device.create_buffer(constants_desc);
    if (!state_->vertices || !state_->indices || !state_->constants) return false;

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_shader_desc;
    vertex_shader_desc.stage = render::ShaderStage::vertex;
    vertex_shader_desc.language = metal ? render::ShaderSourceLanguage::metal : render::ShaderSourceLanguage::hlsl;
    vertex_shader_desc.source = metal ? metal_source : hlsl_source;
    vertex_shader_desc.resources = debug_resources;
    vertex_shader_desc.entry_point = "vice_editor_debug_vs";
    vertex_shader_desc.debug_name = "VICE Editor debug vertex";

    render::ShaderModuleDesc fragment_shader_desc;
    fragment_shader_desc.stage = render::ShaderStage::fragment;
    fragment_shader_desc.language = vertex_shader_desc.language;
    fragment_shader_desc.source = vertex_shader_desc.source;
    fragment_shader_desc.entry_point = "vice_editor_debug_fs";
    fragment_shader_desc.debug_name = "VICE Editor debug fragment";

    state_->vertex_shader = device.create_shader_module(vertex_shader_desc);
    state_->fragment_shader = device.create_shader_module(fragment_shader_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) return false;

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = color_format;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.blend = render::BlendMode::alpha;
    pipeline_desc.vertex_buffers = debug_layouts;
    pipeline_desc.debug_name = "VICE Editor Physics debug overlay";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    state_->initialized = state_->pipeline != nullptr;
    return state_->initialized;
}

void PhysicsDebugRenderer::handle_event(
    const platform::Event& event,
    const SceneViewport& viewport,
    const PreviewSnapshot& preview,
    SelectionModel& selection
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
                } else if (payload.button == platform::MouseButton::left && payload.pressed && viewport.hovered &&
                    preview.playback != PreviewPlayback::stopped && viewport.drawable()) {
                    const math::Vec3 eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
                    const float aspect = viewport.width / std::max(1.0F, viewport.height);
                    const math::Vec3 forward = math::normalize(state_->target - eye);
                    const math::Vec3 right = math::normalize(math::cross(forward, {0.0F, 1.0F, 0.0F}));
                    const math::Vec3 up = math::normalize(math::cross(right, forward));
                    const float normalized_x = static_cast<float>((payload.x - viewport.x) / viewport.width);
                    const float normalized_y = static_cast<float>((payload.y - viewport.y) / viewport.height);
                    const float ndc_x = normalized_x * 2.0F - 1.0F;
                    const float ndc_y = 1.0F - normalized_y * 2.0F;
                    const float half_height = std::tan(scene_fov * 0.5F);
                    const math::Vec3 ray = math::normalize(add(
                        forward,
                        add(scaled(right, ndc_x * aspect * half_height), scaled(up, ndc_y * half_height))
                    ));
                    float nearest = std::numeric_limits<float>::max();
                    std::optional<world::PersistentId> picked;
                    for (const auto& body : preview.bodies) {
                        if (!body.body.box_half_extents.has_value()) continue;
                        const auto center = float_position(body.global_position);
                        const auto half = *body.body.box_half_extents;
                        const float radius = std::max({half.x, half.y, half.z});
                        const math::Vec3 extent{radius, radius, radius};
                        const auto hit = ray_aabb_distance(eye, ray, center - extent, add(center, extent));
                        if (hit.has_value() && *hit < nearest) {
                            nearest = *hit;
                            picked = body.owner;
                        }
                    }
                    for (const auto& character : preview.characters) {
                        const auto base = float_position(character.global_position);
                        const math::Vec3 half{
                            character.character.radius,
                            character.character.height * 0.5F,
                            character.character.radius,
                        };
                        const math::Vec3 center = add(base, {0.0F, half.y, 0.0F});
                        const auto hit = ray_aabb_distance(eye, ray, center - half, add(center, half));
                        if (hit.has_value() && *hit < nearest) {
                            nearest = *hit;
                            picked = character.owner;
                        }
                    }
                    if (picked.has_value()) selection.select(*picked);
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

void PhysicsDebugRenderer::update(const float delta_seconds, const SceneViewport& viewport) {
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
        state_->target = add(
            state_->target,
            scaled(math::normalize(movement), speed * std::min(delta_seconds, 0.1F))
        );
    }
}

bool PhysicsDebugRenderer::render(
    const PreviewSnapshot& preview,
    const PhysicsDebugLayers& layers,
    const SelectionModel& selection,
    render::CommandList& commands,
    const SceneViewport& viewport,
    const platform::WindowMetrics metrics
) {
    if (state_ == nullptr || !state_->initialized || preview.playback == PreviewPlayback::stopped ||
        !viewport.drawable() || metrics.width == 0U || metrics.height == 0U) {
        return true;
    }
    if (!layers.physics && !layers.regions && !layers.contacts && !layers.handoffs) return true;

    std::vector<DebugVertex> vertices;
    std::vector<std::uint32_t> indices;
    vertices.reserve(4'096U);
    indices.reserve(18'432U);
    const auto selected = selection.selected();

    if (layers.physics) {
        for (const auto& body : preview.bodies) {
            if (!body.body.box_half_extents.has_value()) continue;
            const bool highlighted = selected.has_value() && *selected == body.owner;
            const std::array<float, 4> color = highlighted
                ? std::array<float, 4>{0.20F, 0.82F, 1.00F, 0.98F}
                : (body.body.active
                    ? std::array<float, 4>{0.16F, 0.64F, 0.96F, 0.72F}
                    : std::array<float, 4>{0.38F, 0.50F, 0.62F, 0.46F});
            const auto center = float_position(body.global_position);
            add_box(
                vertices,
                indices,
                center,
                *body.body.box_half_extents,
                body.body.orientation,
                highlighted ? 0.035F : 0.022F,
                color
            );
            if (layers.velocities && math::dot(body.body.linear_velocity, body.body.linear_velocity) > 0.0001F) {
                add_segment(
                    vertices,
                    indices,
                    center,
                    add(center, scaled(body.body.linear_velocity, 0.35F)),
                    0.018F,
                    {0.35F, 0.86F, 1.00F, 0.88F}
                );
            }
        }

        if(layers.physics) for(const auto& vehicle:preview.vehicles) {
            for(const auto& wheel:vehicle.vehicle.wheels) if(wheel.grounded) {
                const auto point=float_position(wheel.point);
                const std::array<float,4> color{0.95F,0.72F,0.20F,0.95F};
                add_ring(vertices,indices,point,vehicle.vehicle.state.wheel_radius,0.018F,color);
                add_segment(vertices,indices,point,add(point,scaled(wheel.normal,std::clamp(wheel.normal_force/10000.0F,0.1F,2.0F))),0.018F,color);
            }
        }
        if (layers.characters) {
            for (const auto& character : preview.characters) {
                const bool highlighted = selected.has_value() && *selected == character.owner;
                const auto base = float_position(character.global_position);
                const float radius = character.character.radius;
                const float height = character.character.height;
                const std::array<float, 4> color = highlighted
                    ? std::array<float, 4>{0.20F, 0.92F, 0.78F, 0.98F}
                    : std::array<float, 4>{0.22F, 0.78F, 0.66F, 0.72F};
                add_ring(vertices, indices, add(base, {0.0F, radius, 0.0F}), radius, 0.018F, color);
                add_ring(vertices, indices, add(base, {0.0F, height - radius, 0.0F}), radius, 0.018F, color);
                add_segment(
                    vertices, indices,
                    add(base, {radius, radius, 0.0F}),
                    add(base, {radius, height - radius, 0.0F}),
                    0.018F, color
                );
                add_segment(
                    vertices, indices,
                    add(base, {-radius, radius, 0.0F}),
                    add(base, {-radius, height - radius, 0.0F}),
                    0.018F, color
                );
                add_segment(
                    vertices, indices,
                    add(base, {0.0F, radius, radius}),
                    add(base, {0.0F, height - radius, radius}),
                    0.018F, color
                );
                add_segment(
                    vertices, indices,
                    add(base, {0.0F, radius, -radius}),
                    add(base, {0.0F, height - radius, -radius}),
                    0.018F, color
                );
                if (character.character.ground_state == rigid_body::CharacterGroundState::grounded) {
                    add_segment(
                        vertices,
                        indices,
                        base,
                        add(base, scaled(character.character.ground_normal, 0.65F)),
                        0.017F,
                        {0.36F, 0.92F, 0.52F, 0.90F}
                    );
                }
            }
        }
    }

    if (layers.regions) {
        for (const auto& region : preview.regions) {
            if (!region.global_minimum.has_value() || !region.global_maximum.has_value()) continue;
            const auto minimum = float_position(*region.global_minimum);
            const auto maximum = float_position(*region.global_maximum);
            const math::Vec3 center = scaled(add(minimum, maximum), 0.5F);
            const math::Vec3 half = scaled(maximum - minimum, 0.5F);
            std::array<float, 4> color{};
            switch (region.region.fidelity) {
            case world::RegionFidelity::full: color = {0.24F, 0.84F, 0.56F, 0.58F}; break;
            case world::RegionFidelity::reduced: color = {0.96F, 0.66F, 0.22F, 0.58F}; break;
            case world::RegionFidelity::inactive: color = {0.46F, 0.50F, 0.56F, 0.28F}; break;
            }
            add_box(
                vertices,
                indices,
                center,
                half,
                math::Quat{},
                region.region.fidelity == world::RegionFidelity::inactive ? 0.014F : 0.020F,
                color
            );
        }
    }

    if (layers.contacts) {
        for (const auto& contact : preview.contacts) {
            const auto point = float_position(contact.global_point);
            const float size = 0.16F;
            const std::array<float, 4> color = contact.contact.kind == rigid_body::ContactKind::trigger
                ? std::array<float, 4>{0.82F, 0.34F, 0.98F, 0.92F}
                : std::array<float, 4>{1.00F, 0.30F, 0.62F, 0.94F};
            add_segment(vertices, indices, point - math::Vec3{size, 0.0F, 0.0F}, add(point, {size, 0.0F, 0.0F}), 0.014F, color);
            add_segment(vertices, indices, point - math::Vec3{0.0F, size, 0.0F}, add(point, {0.0F, size, 0.0F}), 0.014F, color);
            add_segment(vertices, indices, point - math::Vec3{0.0F, 0.0F, size}, add(point, {0.0F, 0.0F, size}), 0.014F, color);
            add_segment(vertices, indices, point, add(point, scaled(contact.contact.normal, 0.55F)), 0.018F, color);
        }
    }

    if (layers.handoffs) {
        for (const auto& request : preview.activation_requests) {
            const auto source = region_center(preview, request.source_region);
            const auto target = region_center(preview, request.target_region);
            if (source.has_value() && target.has_value()) {
                add_segment(vertices, indices, *source, *target, 0.028F, {1.00F, 0.72F, 0.20F, 0.90F});
            }
        }
        for (const auto& event : preview.box_region_events) {
            if (!event.target_region.has_value() || event.action != rigid_body::BoxRegionAction::handed_off) continue;
            const auto source = region_center(preview, event.source_region);
            const auto target = region_center(preview, *event.target_region);
            if (source.has_value() && target.has_value()) {
                add_segment(vertices, indices, *source, *target, 0.022F, {0.72F, 0.42F, 1.00F, 0.82F});
            }
        }
        for (const auto& event : preview.character_region_events) {
            if (!event.target_region.has_value() || event.action != rigid_body::CharacterRegionAction::handed_off) continue;
            const auto source = region_center(preview, event.source_region);
            const auto target = region_center(preview, *event.target_region);
            if (source.has_value() && target.has_value()) {
                add_segment(vertices, indices, *source, *target, 0.022F, {0.30F, 0.76F, 1.00F, 0.82F});
            }
        }
    }

    if (vertices.empty() || indices.empty()) return true;
    if (!state_->vertices->write(0, std::as_bytes(std::span{vertices})) ||
        !state_->indices->write(0, std::as_bytes(std::span{indices}))) {
        return false;
    }

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

    const float aspect = viewport.width / std::max(1.0F, viewport.height);
    const auto eye = camera_eye(state_->target, state_->yaw, state_->pitch, state_->distance);
    const auto view = math::look_at_rh(eye, state_->target, {0.0F, 1.0F, 0.0F});
    const auto projection = math::perspective_rh_zo(scene_fov, aspect, 0.05F, 250.0F);
    const DebugConstants constants{.view_projection = math::multiply(projection, view)};
    if (!state_->constants->write(0, std::as_bytes(std::span{&constants, 1U}))) return false;

    return commands.set_viewport(render::Viewport{
               .x = viewport_x,
               .y = viewport_y,
               .width = viewport_width,
               .height = viewport_height,
               .min_depth = 0.0F,
               .max_depth = 1.0F,
           }) &&
        commands.set_scissor_rect(render::ScissorRect{
            .x = static_cast<std::int32_t>(std::floor(viewport_x)),
            .y = static_cast<std::int32_t>(std::floor(viewport_y)),
            .width = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_width))),
            .height = static_cast<std::uint32_t>(std::max(1.0F, std::ceil(viewport_height))),
        }) &&
        commands.set_graphics_pipeline(*state_->pipeline) &&
        commands.set_vertex_buffer(0, *state_->vertices) &&
        commands.set_index_buffer(*state_->indices, render::IndexFormat::uint32) &&
        commands.set_graphics_constant_buffer(frame_binding, *state_->constants, 0, sizeof(DebugConstants)) &&
        commands.draw_indexed(static_cast<std::uint32_t>(indices.size()));
}

} // namespace vice::editor::detail

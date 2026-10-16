#include "lab_materials.hpp"

#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/jobs/jobs.hpp>
#include <vice/math/math.hpp>
#include <vice/platform/event.hpp>
#include <vice/platform/services.hpp>
#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/rigid_body/collision_asset.hpp>
#include <vice/rigid_body/material_asset.hpp>
#include <vice/rigid_body/rigid_body.hpp>
#include <vice/rigid_body/world_bridge.hpp>
#include <vice/simulation/entity_bindings.hpp>
#include <vice/world/snapshot_file.hpp>
#include <vice/world/world.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <type_traits>
#include <variant>

namespace {

using LabVertex = vice::render::MeshVertex;

struct LabInputState {
    bool forward = false;
    bool backward = false;
    bool left = false;
    bool right = false;
    bool jump_queued = false;
};

static_assert(sizeof(LabVertex) == 24U);
static_assert(sizeof(vice::math::Mat4) == 64U);

constexpr std::string_view player_persistent_id_text =
    "10000000-0000-4000-8000-000000000001";
constexpr std::string_view cube_persistent_id_text =
    "10000000-0000-4000-8000-000000000002";

constexpr vice::world::RegionId region_a{1};
constexpr vice::world::RegionId region_b{2};
constexpr vice::simulation::DomainId rigid_domain_a_id{100};
constexpr vice::simulation::DomainId rigid_domain_b_id{200};
constexpr vice::simulation::ParticipantId terrain_participant{1};
constexpr vice::simulation::ParticipantId cube_participant{2};
constexpr vice::simulation::ParticipantId ramp_participant{3};

static constexpr std::array<LabVertex, 8> cube_vertices{{
    {{-0.7F, -0.7F, -0.7F}, {0.16F, 0.68F, 1.00F}},
    {{ 0.7F, -0.7F, -0.7F}, {0.96F, 0.26F, 0.54F}},
    {{ 0.7F,  0.7F, -0.7F}, {0.98F, 0.80F, 0.18F}},
    {{-0.7F,  0.7F, -0.7F}, {0.34F, 0.92F, 0.48F}},
    {{-0.7F, -0.7F,  0.7F}, {0.66F, 0.34F, 1.00F}},
    {{ 0.7F, -0.7F,  0.7F}, {1.00F, 0.48F, 0.20F}},
    {{ 0.7F,  0.7F,  0.7F}, {0.24F, 0.88F, 0.92F}},
    {{-0.7F,  0.7F,  0.7F}, {0.96F, 0.42F, 0.82F}},
}};

static constexpr std::array<std::uint16_t, 36> cube_indices{
    4, 5, 6, 4, 6, 7,
    1, 0, 3, 1, 3, 2,
    0, 4, 7, 0, 7, 3,
    5, 1, 2, 5, 2, 6,
    3, 7, 6, 3, 6, 2,
    0, 1, 5, 0, 5, 4,
};

static constexpr std::array cube_vertex_attributes{
    vice::render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = vice::render::VertexFormat::float32x3,
        .offset = 0,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = vice::render::VertexFormat::float32x3,
        .offset = 12,
    },
};

static constexpr std::array cube_instance_attributes{
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0,
        .shader_location = 2,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 0,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 1,
        .shader_location = 3,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 16,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 2,
        .shader_location = 4,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 32,
    },
    vice::render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 3,
        .shader_location = 5,
        .format = vice::render::VertexFormat::float32x4,
        .offset = 48,
    },
};

static constexpr std::array cube_vertex_layouts{
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(LabVertex),
        .step_mode = vice::render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = cube_vertex_attributes,
    },
    vice::render::VertexBufferLayoutDesc{
        .stride = sizeof(vice::math::Mat4),
        .step_mode = vice::render::VertexStepMode::per_instance,
        .step_rate = 1,
        .attributes = cube_instance_attributes,
    },
};

constexpr std::string_view metal_cube_source = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
    float4 model_col0 [[attribute(13)]];
    float4 model_col1 [[attribute(14)]];
    float4 model_col2 [[attribute(15)]];
    float4 model_col3 [[attribute(16)]];
};

struct VertexOut {
    float4 position [[position]];
    float3 color;
};

vertex VertexOut vice_cube_vs(VertexIn input [[stage_in]]) {
    float4 world_position =
        input.model_col0 * input.position.x +
        input.model_col1 * input.position.y +
        input.model_col2 * input.position.z +
        input.model_col3;
    float z_view = world_position.z - 5.0;

    VertexOut out;
    out.position = float4(
        0.9742786 * world_position.x,
        1.7320508 * world_position.y,
        -1.001001 * z_view - 0.1001001,
        -z_view
    );
    out.color = input.color;
    return out;
}

fragment float4 vice_cube_fs(VertexOut in [[stage_in]]) {
    return float4(in.color, 1.0);
}
)";

constexpr std::string_view hlsl_cube_source = R"(
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float4 model_col0 : TEXCOORD0;
    float4 model_col1 : TEXCOORD1;
    float4 model_col2 : TEXCOORD2;
    float4 model_col3 : TEXCOORD3;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};

VertexOut vice_cube_vs(VertexIn input) {
    float4 world_position =
        input.model_col0 * input.position.x +
        input.model_col1 * input.position.y +
        input.model_col2 * input.position.z +
        input.model_col3;
    float z_view = world_position.z - 5.0;

    VertexOut output;
    output.position = float4(
        0.9742786 * world_position.x,
        1.7320508 * world_position.y,
        -1.001001 * z_view - 0.1001001,
        -z_view
    );
    output.color = input.color;
    return output;
}

float4 vice_cube_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] std::string_view character_ground_state_name(
    const vice::rigid_body::CharacterGroundState state
) noexcept {
    switch (state) {
        case vice::rigid_body::CharacterGroundState::grounded:
            return "grounded";
        case vice::rigid_body::CharacterGroundState::steep_ground:
            return "steep_ground";
        case vice::rigid_body::CharacterGroundState::unsupported:
            return "unsupported";
        case vice::rigid_body::CharacterGroundState::airborne:
            return "airborne";
    }
    return "unknown";
}

[[nodiscard]] std::unique_ptr<vice::render::ShaderModule> load_lab_shader(
    vice::render::Device& device,
    const std::filesystem::path& cooked_directory,
    const vice::render::ShaderModuleDesc& development_fallback
) {
    const std::filesystem::path manifest = cooked_directory / "shader.vice.json";
    std::error_code error;
    const bool manifest_exists = std::filesystem::exists(manifest, error);
    if (error) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab could not inspect cooked shader manifest: " + error.message()
        );
        return nullptr;
    }

    if (manifest_exists) {
        auto loaded = vice::render::load_cooked_shader(device, manifest);
        if (!loaded) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Lab cooked shader load failed: " + loaded.diagnostics
            );
            return nullptr;
        }
        vice::core::log(
            vice::core::LogLevel::debug,
            "VICE Lab loaded cooked shader: " + cooked_directory.filename().string()
        );
        return std::move(loaded.module);
    }

    vice::core::log(
        vice::core::LogLevel::debug,
        "VICE Lab cooked shader is absent; using development source fallback: " +
            cooked_directory.filename().string()
    );
    return device.create_shader_module(development_fallback);
}

[[nodiscard]] std::unique_ptr<vice::render::Texture> create_depth_target(
    vice::render::Device& device,
    const vice::platform::WindowMetrics metrics
) {
    vice::render::TextureDesc depth_desc;
    depth_desc.dimension = vice::render::TextureDimension::d2;
    depth_desc.width = std::max<std::uint32_t>(1U, metrics.drawable_width);
    depth_desc.height = std::max<std::uint32_t>(1U, metrics.drawable_height);
    depth_desc.depth = 1;
    depth_desc.mip_levels = 1;
    depth_desc.array_layers = 1;
    depth_desc.sample_count = 1;
    depth_desc.format = vice::render::TextureFormat::depth32_float;
    depth_desc.usage = vice::render::TextureUsage::depth_attachment;
    depth_desc.debug_name = "VICE Lab depth";
    return device.create_texture(depth_desc);
}

[[nodiscard]] bool build_fallback_world(vice::world::World& world) {
    const auto player_id = vice::world::PersistentId::parse(player_persistent_id_text);
    const auto cube_id = vice::world::PersistentId::parse(cube_persistent_id_text);
    if (!player_id.has_value() || !cube_id.has_value()) return false;

    const vice::world::RegionBounds bounds{
        .min = vice::world::LocalPosition{0.0F, -100.0F, -100.0F},
        .max = vice::world::LocalPosition{8.0F, 100.0F, 100.0F},
    };
    if (!world.register_region(vice::world::RegionDesc{
            .id = region_a,
            .simulation_island = vice::world::SimulationIslandId{101},
            .origin = vice::world::GlobalPosition{-8.0, 0.0, 0.0},
            .streaming_cell = vice::world::StreamingCellId{0, 0, 0},
            .fidelity = vice::world::RegionFidelity::full,
            .bounds = bounds,
        }) ||
        !world.register_region(vice::world::RegionDesc{
            .id = region_b,
            .simulation_island = vice::world::SimulationIslandId{202},
            .origin = vice::world::GlobalPosition{0.0, 0.0, 0.0},
            .streaming_cell = vice::world::StreamingCellId{1, 0, 0},
            .fidelity = vice::world::RegionFidelity::inactive,
            .bounds = bounds,
        })) {
        return false;
    }

    const auto player = world.create_entity(*player_id);
    const auto cube = world.create_entity(*cube_id);
    return player.has_value() && cube.has_value() &&
        world.set_spatial_state(*player, vice::world::SpatialState{
            .global_position = vice::world::GlobalPosition{-4.0, 0.0, 0.0},
            .region = region_a,
        }) &&
        world.set_spatial_state(*cube, vice::world::SpatialState{
            .global_position = vice::world::GlobalPosition{-2.5, 2.2, -2.0},
            .region = region_a,
        });
}

[[nodiscard]] bool restore_lab_world(vice::world::World& world) {
    const std::filesystem::path save_path{VICE_LAB_WORLD_SAVE};
    std::error_code error;
    const bool save_exists = std::filesystem::exists(save_path, error);
    if (error) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab could not inspect its World save: " + error.message()
        );
        return false;
    }

    if (save_exists) {
        const auto loaded = vice::world::load_world_snapshot(save_path);
        if (!loaded.snapshot.has_value() || !world.restore(*loaded.snapshot)) {
            vice::core::log(
                vice::core::LogLevel::error,
                "VICE Lab World save load failed: " + loaded.diagnostics
            );
            return false;
        }
        vice::core::log(vice::core::LogLevel::info, "VICE Lab restored saved World state.");
        return true;
    }

#if defined(VICE_LAB_COOKED_WORLD)
    const auto loaded = vice::world::load_world_snapshot(VICE_LAB_COOKED_WORLD);
    if (!loaded.snapshot.has_value() || !world.restore(*loaded.snapshot)) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab authored World load failed: " + loaded.diagnostics
        );
        return false;
    }
    vice::core::log(vice::core::LogLevel::info, "VICE Lab restored the authored World asset.");
    return true;
#else
    if (!build_fallback_world(world)) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab could not construct the development World fallback."
        );
        return false;
    }
    vice::core::log(
        vice::core::LogLevel::debug,
        "VICE Lab cooked World is unavailable; using the development World fallback."
    );
    return true;
#endif
}

[[nodiscard]] bool save_lab_world(const vice::world::World& world) {
    std::string diagnostics;
    if (!vice::world::save_world_snapshot(
            std::filesystem::path{VICE_LAB_WORLD_SAVE},
            world.snapshot(),
            &diagnostics
        )) {
        vice::core::log(
            vice::core::LogLevel::error,
            "VICE Lab could not save World state: " + diagnostics
        );
        return false;
    }
    vice::core::log(vice::core::LogLevel::info, "VICE Lab saved authoritative World state.");
    return true;
}

} // namespace

int main() {
    using namespace vice::core;

    const auto platform = current_platform();
    if (platform == Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE development target.");
        return 1;
    }

    log(LogLevel::info, std::string{"VICE "} + std::string{version_string()});
    log(LogLevel::info, std::string{"Platform: "} + std::string{platform_name(platform)});
    log(LogLevel::info, std::string{"Architecture: "} + std::string{architecture_name()});
    log(LogLevel::info, std::string{"Compiler: "} + std::string{compiler_name()});

    vice::jobs::Scheduler jobs;
    log(LogLevel::info, std::string{"Job workers: "} + std::to_string(jobs.worker_count()));

    auto platform_services = vice::platform::PlatformServices::create();
    if (!platform_services) {
        log(LogLevel::error, "VICE could not initialize platform services.");
        return 1;
    }

    vice::platform::WindowDesc window_desc;
    window_desc.title = "VICE Lab";
    window_desc.width = 1280;
    window_desc.height = 720;

    auto window = platform_services->create_window(window_desc);
    if (!window) {
        log(LogLevel::error, "VICE Lab could not create a native window.");
        return 1;
    }

    auto render_device = vice::render::Device::create();
    if (!render_device) {
        log(LogLevel::error, "VICE Lab could not create a render device.");
        return 1;
    }

    auto swapchain = render_device->create_swapchain(*window);
    auto depth_target = create_depth_target(*render_device, window->metrics());
    if (!swapchain || !depth_target) {
        log(LogLevel::error, "VICE Lab could not create render targets.");
        return 1;
    }

    std::optional<vice::render::MeshAsset> scene_mesh;
#if defined(VICE_LAB_COOKED_SCENE_MESH)
    auto loaded_scene_mesh = vice::render::load_mesh_asset(VICE_LAB_COOKED_SCENE_MESH);
    if (!loaded_scene_mesh.asset.has_value()) {
        log(LogLevel::error, "VICE Lab authored mesh load failed: " + loaded_scene_mesh.diagnostics);
        return 1;
    }
    scene_mesh = std::move(*loaded_scene_mesh.asset);
    log(
        LogLevel::info,
        std::string{"VICE Lab loaded authored mesh: "} +
            std::to_string(scene_mesh->vertices.size()) + " vertices, " +
            std::to_string(scene_mesh->indices.size() / 3U) + " triangles"
    );
#endif

    vice::render::BufferDesc vertex_buffer_desc;
    vertex_buffer_desc.size = sizeof(cube_vertices);
    vertex_buffer_desc.usage = vice::render::BufferUsage::vertex;
    vertex_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    vertex_buffer_desc.debug_name = "VICE Lab cube vertices";

    vice::render::BufferDesc instance_buffer_desc;
    instance_buffer_desc.size = sizeof(vice::math::Mat4);
    instance_buffer_desc.usage = vice::render::BufferUsage::vertex;
    instance_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    instance_buffer_desc.debug_name = "VICE Lab cube pose";

    vice::render::BufferDesc index_buffer_desc;
    index_buffer_desc.size = sizeof(cube_indices);
    index_buffer_desc.usage = vice::render::BufferUsage::index;
    index_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    index_buffer_desc.debug_name = "VICE Lab cube indices";

    vice::render::BufferDesc frame_buffer_desc;
    frame_buffer_desc.size = sizeof(vice::math::Mat4);
    frame_buffer_desc.usage = vice::render::BufferUsage::uniform;
    frame_buffer_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
    frame_buffer_desc.debug_name = "VICE Lab frame constants";

    auto vertex_buffer = render_device->create_buffer(vertex_buffer_desc);
    auto instance_buffer = render_device->create_buffer(instance_buffer_desc);
    auto index_buffer = render_device->create_buffer(index_buffer_desc);
    auto frame_buffer = render_device->create_buffer(frame_buffer_desc);
    if (!vertex_buffer || !instance_buffer || !index_buffer || !frame_buffer ||
        !vertex_buffer->write(0, std::as_bytes(std::span{cube_vertices})) ||
        !index_buffer->write(0, std::as_bytes(std::span{cube_indices}))) {
        log(LogLevel::error, "VICE Lab could not upload cube resources.");
        return 1;
    }

    std::unique_ptr<vice::render::Buffer> scene_vertex_buffer;
    std::unique_ptr<vice::render::Buffer> scene_index_buffer;
    std::unique_ptr<vice::render::Buffer> scene_instance_buffer;
    if (scene_mesh.has_value()) {
        vice::render::BufferDesc scene_vertex_desc;
        scene_vertex_desc.size = scene_mesh->vertices.size() * sizeof(vice::render::MeshVertex);
        scene_vertex_desc.usage = vice::render::BufferUsage::vertex;
        scene_vertex_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
        scene_vertex_desc.debug_name = "VICE Lab authored mesh vertices";

        vice::render::BufferDesc scene_index_desc;
        scene_index_desc.size = scene_mesh->indices.size() * sizeof(std::uint32_t);
        scene_index_desc.usage = vice::render::BufferUsage::index;
        scene_index_desc.memory = vice::render::MemoryLocation::cpu_to_gpu;
        scene_index_desc.debug_name = "VICE Lab authored mesh indices";

        vice::render::BufferDesc scene_instance_desc = instance_buffer_desc;
        scene_instance_desc.debug_name = "VICE Lab authored mesh pose";

        scene_vertex_buffer = render_device->create_buffer(scene_vertex_desc);
        scene_index_buffer = render_device->create_buffer(scene_index_desc);
        scene_instance_buffer = render_device->create_buffer(scene_instance_desc);
        if (!scene_vertex_buffer || !scene_index_buffer || !scene_instance_buffer ||
            !scene_vertex_buffer->write(0, std::as_bytes(std::span{scene_mesh->vertices})) ||
            !scene_index_buffer->write(0, std::as_bytes(std::span{scene_mesh->indices}))) {
            log(LogLevel::error, "VICE Lab could not upload authored mesh resources.");
            return 1;
        }
    }

    const bool metal_backend = render_device->backend() == vice::render::Backend::metal;
    vice::render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = vice::render::ShaderStage::vertex;
    vertex_desc.language = metal_backend
        ? vice::render::ShaderSourceLanguage::metal
        : vice::render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal_backend ? metal_cube_source : hlsl_cube_source;
    vertex_desc.entry_point = "vice_cube_vs";
    vertex_desc.debug_name = "VICE Lab vertex";

    vice::render::ShaderModuleDesc fragment_desc = vertex_desc;
    fragment_desc.stage = vice::render::ShaderStage::fragment;
    fragment_desc.entry_point = "vice_cube_fs";
    fragment_desc.debug_name = "VICE Lab fragment";

    const std::filesystem::path cooked_shader_root{VICE_LAB_COOKED_SHADER_ROOT};
    auto vertex_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_cube_vs",
        vertex_desc
    );
    auto fragment_shader = load_lab_shader(
        *render_device,
        cooked_shader_root / "vice_lab_cube_fs",
        fragment_desc
    );
    if (!vertex_shader || !fragment_shader) {
        log(LogLevel::error, "VICE Lab could not create shader modules.");
        return 1;
    }

    vice::render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex_shader.get();
    pipeline_desc.fragment_shader = fragment_shader.get();
    pipeline_desc.color_format = swapchain->color_format();
    pipeline_desc.cull_mode = vice::render::CullMode::none;
    pipeline_desc.depth.format = vice::render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = vice::render::CompareOp::less;
    pipeline_desc.vertex_buffers = cube_vertex_layouts;
    pipeline_desc.debug_name = "VICE Lab scene pipeline";

    auto scene_pipeline = render_device->create_graphics_pipeline(pipeline_desc);
    if (!scene_pipeline) {
        log(LogLevel::error, "VICE Lab could not create the scene pipeline.");
        return 1;
    }

    const bool cooked_transform_binding = !vertex_shader->resources().empty();
    constexpr vice::render::ShaderBindingKey frame_binding{
        .register_class = vice::render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .array_element = 0,
    };

    vice::world::World world;
    if (!restore_lab_world(world)) return 1;
    std::string material_role_diagnostics;
    if (!vice::lab::ensure_material_roles(world, &material_role_diagnostics)) {
        log(LogLevel::error, "VICE Lab material role migration failed: " + material_role_diagnostics);
        return 1;
    }

    const auto player_persistent_id = vice::world::PersistentId::parse(player_persistent_id_text);
    const auto cube_persistent_id = vice::world::PersistentId::parse(cube_persistent_id_text);
    if (!player_persistent_id.has_value() || !cube_persistent_id.has_value()) {
        log(LogLevel::error, "VICE Lab runtime role IDs are invalid.");
        return 1;
    }

    const auto player_entity_record = world.find(*player_persistent_id);
    const auto cube_entity_record = world.find(*cube_persistent_id);
    const auto first_region = world.region(region_a);
    const auto second_region = world.region(region_b);
    if (!player_entity_record.has_value() || !cube_entity_record.has_value() ||
        !first_region.has_value() || !second_region.has_value()) {
        log(LogLevel::error, "VICE Lab restored World is missing required authored records.");
        return 1;
    }
    const auto player_entity = *player_entity_record;
    const auto cube_entity = *cube_entity_record;

    const auto initial_player_spatial = world.spatial_state(player_entity);
    const auto initial_cube_spatial = world.spatial_state(cube_entity);
    if (!initial_player_spatial.has_value() || !initial_cube_spatial.has_value()) {
        log(LogLevel::error, "VICE Lab restored roles are missing spatial state.");
        return 1;
    }

    auto rigid_domain_a = vice::rigid_body::Domain::create(rigid_domain_a_id, "lab.rigid.a", jobs);
    auto rigid_domain_b = vice::rigid_body::Domain::create(rigid_domain_b_id, "lab.rigid.b", jobs);
    vice::simulation::Coordinator simulation{1.0 / 120.0};
    vice::simulation::EntityBindings bindings;
    vice::rigid_body::WorldBridge world_bridge;
    if (!rigid_domain_a || !rigid_domain_b) {
        log(LogLevel::error, "VICE Lab could not create regional rigid simulation.");
        return 1;
    }

    const auto domain_for_region = [&](const vice::world::RegionId region)
        -> vice::rigid_body::Domain* {
        if (region == region_a) return rigid_domain_a.get();
        if (region == region_b) return rigid_domain_b.get();
        return nullptr;
    };

    std::optional<vice::lab::LabMaterialSet> lab_materials;
#if defined(VICE_LAB_COOKED_RIGID_MATERIALS)
    auto loaded_materials = vice::lab::load_materials(world, VICE_LAB_COOKED_RIGID_MATERIALS);
    if (!loaded_materials.materials.has_value()) {
        log(LogLevel::error, "VICE Lab rigid material load failed: " + loaded_materials.diagnostics);
        return 1;
    }
    lab_materials = std::move(*loaded_materials.materials);
    std::string material_diagnostics;
    if (!vice::rigid_body::register_rigid_material_asset(
            *rigid_domain_a,
            lab_materials->asset,
            &material_diagnostics
        ) ||
        !vice::rigid_body::register_rigid_material_asset(
            *rigid_domain_b,
            lab_materials->asset,
            &material_diagnostics
        )) {
        log(LogLevel::error, "VICE Lab rigid material registration failed: " + material_diagnostics);
        return 1;
    }
    log(
        LogLevel::info,
        "VICE Lab materials: cube=" + lab_materials->cube.name + " (" +
            std::to_string(lab_materials->cube.runtime_id.value) + "), terrain=" +
            lab_materials->terrain_a.name + " (" +
            std::to_string(lab_materials->terrain_a.runtime_id.value) + ")"
    );
#endif

    const auto terrain_a_material = lab_materials.has_value()
        ? lab_materials->terrain_a.runtime_id
        : vice::simulation::MaterialId{};
    const auto terrain_b_material = lab_materials.has_value()
        ? lab_materials->terrain_b.runtime_id
        : vice::simulation::MaterialId{};
    const auto ramp_material = lab_materials.has_value()
        ? lab_materials->ramp.runtime_id
        : vice::simulation::MaterialId{};
    const auto cube_material = lab_materials.has_value()
        ? lab_materials->cube.runtime_id
        : vice::simulation::MaterialId{};

    bool authored_collision_ready = false;
#if defined(VICE_LAB_COOKED_SCENE_COLLISION) && defined(VICE_LAB_COOKED_TERRAIN_COLLISION)
    auto terrain_collision = vice::rigid_body::load_collision_asset(
        VICE_LAB_COOKED_TERRAIN_COLLISION
    );
    auto ramp_collision = vice::rigid_body::load_collision_asset(
        VICE_LAB_COOKED_SCENE_COLLISION
    );
    if (!terrain_collision.asset.has_value() || terrain_collision.asset->shapes.empty()) {
        log(
            LogLevel::error,
            "VICE Lab terrain collision load failed: " + terrain_collision.diagnostics
        );
        return 1;
    }
    if (!ramp_collision.asset.has_value() || ramp_collision.asset->shapes.empty()) {
        log(
            LogLevel::error,
            "VICE Lab authored collision load failed: " + ramp_collision.diagnostics
        );
        return 1;
    }

    std::string collision_diagnostics;
    if (!vice::rigid_body::register_collision_asset(
            *rigid_domain_a,
            *terrain_collision.asset,
            &collision_diagnostics
        ) ||
        !vice::rigid_body::register_collision_asset(
            *rigid_domain_b,
            *terrain_collision.asset,
            &collision_diagnostics
        ) ||
        !vice::rigid_body::register_collision_asset(
            *rigid_domain_a,
            *ramp_collision.asset,
            &collision_diagnostics
        )) {
        log(LogLevel::error, "VICE Lab collision registration failed: " + collision_diagnostics);
        return 1;
    }

    vice::rigid_body::BodyDesc terrain_body_a;
    terrain_body_a.participant = terrain_participant;
    terrain_body_a.shape = terrain_collision.asset->shapes.front().id;
    terrain_body_a.material = terrain_a_material;
    terrain_body_a.motion = vice::rigid_body::MotionType::static_body;
    vice::rigid_body::BodyDesc terrain_body_b = terrain_body_a;
    terrain_body_b.material = terrain_b_material;

    vice::rigid_body::BodyDesc ramp_body;
    ramp_body.participant = ramp_participant;
    ramp_body.shape = ramp_collision.asset->shapes.front().id;
    ramp_body.material = ramp_material;
    ramp_body.motion = vice::rigid_body::MotionType::static_body;

    if (!rigid_domain_a->create_body(terrain_body_a) ||
        !rigid_domain_b->create_body(terrain_body_b) ||
        !rigid_domain_a->create_body(ramp_body)) {
        log(LogLevel::error, "VICE Lab could not create cooked scene collision bodies.");
        return 1;
    }
    authored_collision_ready = true;
    log(LogLevel::info, "VICE Lab loaded VICECOLL heightfield terrain and authored ramp collision.");
#endif

    if (!authored_collision_ready) {
        vice::rigid_body::BoxBodyDesc floor_a;
        floor_a.participant = terrain_participant;
        floor_a.material = terrain_a_material;
        floor_a.motion = vice::rigid_body::MotionType::static_body;
        floor_a.half_extents = vice::math::Vec3{4.0F, 0.25F, 20.0F};
        floor_a.position = vice::math::Vec3{4.0F, -0.25F, 0.0F};
        vice::rigid_body::BoxBodyDesc floor_b = floor_a;
        floor_b.material = terrain_b_material;
        if (!rigid_domain_a->create_box(floor_a) || !rigid_domain_b->create_box(floor_b)) {
            log(LogLevel::error, "VICE Lab could not create fallback regional floor collision.");
            return 1;
        }
        log(LogLevel::debug, "VICE Lab authored collision is unavailable; using development floors.");
    }

    if (!world_bridge.activate_region(world, simulation, region_a, *rigid_domain_a)) {
        log(LogLevel::error, "VICE Lab could not activate its primary region.");
        return 1;
    }

    const bool region_b_needed_at_start =
        second_region->fidelity != vice::world::RegionFidelity::inactive ||
        initial_player_spatial->region == region_b ||
        initial_cube_spatial->region == region_b;
    if (region_b_needed_at_start &&
        !world_bridge.activate_region(world, simulation, region_b, *rigid_domain_b)) {
        log(LogLevel::error, "VICE Lab could not restore its active adjacent region.");
        return 1;
    }

    const auto activate_requested_regions = [&]() -> bool {
        for (const auto& request : world_bridge.region_activation_requests()) {
            auto* target_domain = domain_for_region(request.target_region);
            if (target_domain == nullptr ||
                !world_bridge.activate_region(
                    world,
                    simulation,
                    request.target_region,
                    *target_domain
                )) {
                log(
                    LogLevel::error,
                    std::string{"VICE Lab could not activate requested region: "} +
                        std::to_string(request.target_region.value)
                );
                return false;
            }
            log(
                LogLevel::info,
                std::string{"VICE Lab activated region: "} +
                    std::to_string(request.target_region.value)
            );
        }
        world_bridge.clear_region_activation_requests();
        return true;
    };

    vice::rigid_body::BoxBodyDesc simulated_cube;
    simulated_cube.participant = cube_participant;
    simulated_cube.material = cube_material;
    simulated_cube.motion = vice::rigid_body::MotionType::dynamic_body;
    simulated_cube.motion_quality = vice::rigid_body::MotionQuality::continuous;
    simulated_cube.half_extents = vice::math::Vec3{0.7F, 0.7F, 0.7F};
    simulated_cube.orientation = vice::math::Quat{0.0F, 0.0F, 0.2588190451F, 0.9659258263F};
    if (second_region->fidelity == vice::world::RegionFidelity::inactive &&
        initial_cube_spatial->region == region_a) {
        simulated_cube.linear_velocity = vice::math::Vec3{5.0F, 0.0F, 0.0F};
        simulated_cube.angular_velocity = vice::math::Vec3{0.0F, 1.0F, 0.25F};
    }
    if (!world_bridge.create_box(world, simulation, bindings, cube_entity, simulated_cube)) {
        log(LogLevel::error, "VICE Lab could not rebuild the simulated cube binding.");
        return 1;
    }

    vice::rigid_body::CharacterDesc player;
    player.id = vice::rigid_body::CharacterId{1};
    player.height = 1.8F;
    player.radius = 0.3F;
    player.step_height = 0.4F;
    player.stick_to_floor_distance = 0.5F;
    if (!world_bridge.create_character(world, simulation, player_entity, player)) {
        log(LogLevel::error, "VICE Lab could not rebuild the character binding.");
        return 1;
    }

    log(LogLevel::info, std::string{"VICE Lab player: "} + player_persistent_id->to_string());
    log(
        LogLevel::info,
        std::string{"VICE Lab player region: "} + std::to_string(initial_player_spatial->region.value)
    );
    log(
        LogLevel::info,
        std::string{"VICE Lab cube region: "} + std::to_string(initial_cube_spatial->region.value)
    );
    log(
        LogLevel::info,
        std::string{"VICE Lab active rigid regions: "} +
            std::to_string(world_bridge.registered_region_count())
    );
    if (!region_b_needed_at_start) {
        log(LogLevel::info, "VICE Lab adjacent region 2 is inactive until requested.");
    }
    log(LogLevel::info, "VICE Lab controls: W/A/S/D or arrows move, Space jumps, Escape quits.");

    const auto log_material_contacts = [&](
        const vice::rigid_body::Domain& domain,
        const vice::world::RegionId region
    ) {
        for (const auto& event : domain.contact_events()) {
            if (event.phase != vice::rigid_body::ContactPhase::began ||
                event.material_interaction.empty() ||
                (event.first != cube_participant && event.second != cube_participant)) {
                continue;
            }
            log(
                LogLevel::info,
                "VICE Lab material interaction: region=" + std::to_string(region.value) +
                    " label=" + event.material_interaction +
                    " friction=" + std::to_string(event.combined_friction) +
                    " restitution=" + std::to_string(event.combined_restitution)
            );
        }
    };

    constexpr float player_speed = 3.5F;
    constexpr float player_jump_speed = 5.5F;
    constexpr vice::render::Color clear_color{0.018F, 0.022F, 0.030F, 1.0F};
    FrameClock clock;
    LabInputState input;
    auto previous_ground_state = vice::rigid_body::CharacterGroundState::airborne;
    auto previous_region = initial_player_spatial->region;
    auto previous_cube_region = initial_cube_spatial->region;
    bool ground_state_initialized = false;
    double simulation_accumulator = 0.0;
    bool running = true;

    while (running) {
        vice::platform::Event event{vice::platform::QuitRequested{}};
        while (platform_services->poll_event(event)) {
            std::visit(
                [&](const auto& payload) {
                    using Payload = std::decay_t<decltype(payload)>;
                    if constexpr (
                        std::is_same_v<Payload, vice::platform::QuitRequested> ||
                        std::is_same_v<Payload, vice::platform::WindowCloseRequested>
                    ) {
                        running = false;
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowMetricsChanged>) {
                        if (payload.metrics.drawable_width > 0U && payload.metrics.drawable_height > 0U) {
                            if (!render_device->wait_idle() || !swapchain->resize(payload.metrics)) {
                                log(LogLevel::error, "VICE Lab could not resize the render targets.");
                                running = false;
                                return;
                            }
                            auto replacement_depth = create_depth_target(*render_device, payload.metrics);
                            if (!replacement_depth) {
                                log(LogLevel::error, "VICE Lab could not resize the depth target.");
                                running = false;
                                return;
                            }
                            depth_target = std::move(replacement_depth);
                        }
                    } else if constexpr (std::is_same_v<Payload, vice::platform::WindowFocusChanged>) {
                        if (!payload.focused) input = {};
                    } else if constexpr (std::is_same_v<Payload, vice::platform::KeyEvent>) {
                        if (payload.pressed && payload.key == vice::platform::Key::escape) {
                            running = false;
                            return;
                        }

                        switch (payload.key) {
                            case vice::platform::Key::w:
                            case vice::platform::Key::up:
                                input.forward = payload.pressed;
                                break;
                            case vice::platform::Key::s:
                            case vice::platform::Key::down:
                                input.backward = payload.pressed;
                                break;
                            case vice::platform::Key::a:
                            case vice::platform::Key::left:
                                input.left = payload.pressed;
                                break;
                            case vice::platform::Key::d:
                            case vice::platform::Key::right:
                                input.right = payload.pressed;
                                break;
                            case vice::platform::Key::space:
                                if (payload.pressed && !payload.repeat) input.jump_queued = true;
                                break;
                            default:
                                break;
                        }
                    }
                },
                event.payload
            );
        }

        (void)jobs.pump_main(64);
        clock.tick();
        simulation_accumulator += std::min(clock.delta_seconds(), 0.25);
        while (running && simulation_accumulator >= simulation.fixed_delta_seconds()) {
            const auto player_spatial = world.spatial_state(player_entity);
            const auto player_character = world_bridge.character_id(world, simulation, player_entity);
            if (!player_spatial.has_value() || !player_character.has_value()) {
                log(LogLevel::error, "VICE Lab lost the World-owned player binding.");
                running = false;
                break;
            }

            auto* player_domain = domain_for_region(player_spatial->region);
            if (player_domain == nullptr || !simulation.registered(player_domain->id())) {
                log(LogLevel::error, "VICE Lab player entered an unavailable region.");
                running = false;
                break;
            }

            float move_x = (input.right ? 1.0F : 0.0F) - (input.left ? 1.0F : 0.0F);
            float move_z = (input.backward ? 1.0F : 0.0F) - (input.forward ? 1.0F : 0.0F);
            if (move_x != 0.0F && move_z != 0.0F) {
                constexpr float diagonal_scale = 0.7071067812F;
                move_x *= diagonal_scale;
                move_z *= diagonal_scale;
            }

            if (!player_domain->set_character_horizontal_velocity(
                    *player_character,
                    vice::math::Vec3{move_x * player_speed, 0.0F, move_z * player_speed}
                )) {
                log(LogLevel::error, "VICE Lab could not update player movement.");
                running = false;
                break;
            }

            if (input.jump_queued) {
                if (!player_domain->request_character_jump(*player_character, player_jump_speed)) {
                    log(LogLevel::error, "VICE Lab could not queue the player jump.");
                    running = false;
                    break;
                }
                input.jump_queued = false;
            }

            simulation.step();
            if (simulation.registered(rigid_domain_a_id)) {
                log_material_contacts(*rigid_domain_a, region_a);
            }
            if (simulation.registered(rigid_domain_b_id)) {
                log_material_contacts(*rigid_domain_b, region_b);
            }

            world_bridge.orchestrate_characters(world, simulation);
            if (world_bridge.character_region_events().empty() ||
                world_bridge.character_region_events().front().action ==
                    vice::rigid_body::CharacterRegionAction::failed) {
                log(LogLevel::error, "VICE Lab character region orchestration failed.");
                running = false;
                break;
            }

            world_bridge.orchestrate_boxes(world, simulation, bindings);
            if (world_bridge.box_region_events().empty() ||
                world_bridge.box_region_events().front().action ==
                    vice::rigid_body::BoxRegionAction::failed) {
                log(LogLevel::error, "VICE Lab rigid region orchestration failed.");
                running = false;
                break;
            }

            if (!activate_requested_regions()) {
                running = false;
                break;
            }

            const auto updated_spatial = world.spatial_state(player_entity);
            const auto updated_cube_spatial = world.spatial_state(cube_entity);
            if (!updated_spatial.has_value() || !updated_cube_spatial.has_value()) {
                log(LogLevel::error, "VICE Lab lost authoritative spatial state.");
                running = false;
                break;
            }
            if (updated_spatial->region != previous_region) {
                previous_region = updated_spatial->region;
                log(
                    LogLevel::info,
                    std::string{"VICE Lab player handed off to region: "} +
                        std::to_string(previous_region.value)
                );
            }
            if (updated_cube_spatial->region != previous_cube_region) {
                previous_cube_region = updated_cube_spatial->region;
                log(
                    LogLevel::info,
                    std::string{"VICE Lab cube handed off to region: "} +
                        std::to_string(previous_cube_region.value)
                );
            }

            simulation_accumulator -= simulation.fixed_delta_seconds();
        }

        if (running) {
            const auto player_spatial = world.spatial_state(player_entity);
            const auto cube_spatial = world.spatial_state(cube_entity);
            const auto player_character = world_bridge.character_id(world, simulation, player_entity);
            if (!player_spatial.has_value() || !cube_spatial.has_value() ||
                !player_character.has_value()) {
                log(LogLevel::error, "VICE Lab lost authoritative World state.");
                running = false;
                continue;
            }

            auto* player_domain = domain_for_region(player_spatial->region);
            auto* cube_domain = domain_for_region(cube_spatial->region);
            const auto player_state = player_domain != nullptr
                ? player_domain->character_state(*player_character)
                : std::nullopt;
            const auto cube_state = cube_domain != nullptr
                ? cube_domain->body_state(cube_participant)
                : std::nullopt;
            if (!player_state.has_value() || !cube_state.has_value()) {
                log(LogLevel::error, "VICE Lab lost a live simulated participant.");
                running = false;
                continue;
            }

            if (!ground_state_initialized || player_state->ground_state != previous_ground_state) {
                previous_ground_state = player_state->ground_state;
                ground_state_initialized = true;
                log(
                    LogLevel::debug,
                    std::string{"VICE Lab player ground state: "} +
                        std::string{character_ground_state_name(player_state->ground_state)}
                );
            }

            const auto player_global = player_spatial->global_position;
            const auto cube_global = cube_spatial->global_position;
            vice::math::Vec3 cube_render_position{
                static_cast<float>(cube_global.x),
                static_cast<float>(cube_global.y),
                static_cast<float>(cube_global.z),
            };
            vice::math::Vec3 ramp_render_position{
                static_cast<float>(first_region->origin.x),
                static_cast<float>(first_region->origin.y),
                static_cast<float>(first_region->origin.z),
            };
            if (!cooked_transform_binding) {
                cube_render_position.x -= static_cast<float>(player_global.x);
                cube_render_position.y -= static_cast<float>(player_global.y);
                cube_render_position.z -= static_cast<float>(player_global.z);
                ramp_render_position.x -= static_cast<float>(player_global.x);
                ramp_render_position.y -= static_cast<float>(player_global.y);
                ramp_render_position.z -= static_cast<float>(player_global.z);
            }

            const auto cube_model = vice::math::pose(cube_render_position, cube_state->orientation);
            if (!instance_buffer->write(0, std::as_bytes(std::span{cube_model.values}))) {
                log(LogLevel::error, "VICE Lab could not upload the simulated cube pose.");
                running = false;
                continue;
            }
            if (scene_instance_buffer) {
                const auto ramp_model = vice::math::pose(ramp_render_position, vice::math::Quat{});
                if (!scene_instance_buffer->write(0, std::as_bytes(std::span{ramp_model.values}))) {
                    log(LogLevel::error, "VICE Lab could not upload the authored mesh pose.");
                    running = false;
                    continue;
                }
            }

            const float aspect = static_cast<float>(depth_target->width()) /
                static_cast<float>(std::max<std::uint32_t>(1U, depth_target->height()));
            const auto camera_target = vice::math::Vec3{
                static_cast<float>(player_global.x),
                static_cast<float>(player_global.y) + 1.0F,
                static_cast<float>(player_global.z),
            };
            const auto camera_position = vice::math::Vec3{
                camera_target.x,
                camera_target.y + 2.2F,
                camera_target.z + 6.5F,
            };
            const auto view = vice::math::look_at_rh(
                camera_position,
                camera_target,
                vice::math::Vec3{0.0F, 1.0F, 0.0F}
            );
            const auto projection = vice::math::perspective_rh_zo(
                1.0471975512F,
                aspect,
                0.1F,
                100.0F
            );
            const auto view_projection = vice::math::multiply(projection, view);
            if (!frame_buffer->write(0, std::as_bytes(std::span{view_projection.values}))) {
                log(LogLevel::error, "VICE Lab could not update frame constants.");
                running = false;
                continue;
            }

            auto frame_commands = render_device->create_command_list();
            vice::render::RenderPassDesc pass_desc;
            pass_desc.clear_color = clear_color;
            pass_desc.depth_attachment = depth_target.get();
            pass_desc.clear_depth = 1.0F;
            pass_desc.clear_depth_attachment = true;

            bool recorded = frame_commands &&
                frame_commands->begin_render_pass(*swapchain, pass_desc) &&
                frame_commands->set_graphics_pipeline(*scene_pipeline);
            if (recorded && cooked_transform_binding) {
                recorded = frame_commands->set_graphics_constant_buffer(
                    frame_binding,
                    *frame_buffer,
                    0,
                    sizeof(vice::math::Mat4)
                );
            }
            if (recorded && scene_mesh.has_value()) {
                recorded = scene_vertex_buffer && scene_index_buffer && scene_instance_buffer &&
                    frame_commands->set_vertex_buffer(0, *scene_vertex_buffer) &&
                    frame_commands->set_vertex_buffer(1, *scene_instance_buffer) &&
                    frame_commands->set_index_buffer(
                        *scene_index_buffer,
                        vice::render::IndexFormat::uint32
                    ) &&
                    frame_commands->draw_indexed(
                        static_cast<std::uint32_t>(scene_mesh->indices.size())
                    );
            }
            recorded = recorded &&
                frame_commands->set_vertex_buffer(0, *vertex_buffer) &&
                frame_commands->set_vertex_buffer(1, *instance_buffer) &&
                frame_commands->set_index_buffer(
                    *index_buffer,
                    vice::render::IndexFormat::uint16
                ) &&
                frame_commands->draw_indexed(static_cast<std::uint32_t>(cube_indices.size())) &&
                frame_commands->end_render_pass() &&
                frame_commands->present(*swapchain);

            if (!recorded) {
                log(LogLevel::error, "VICE Lab failed to record the persisted playable frame.");
                running = false;
            } else {
                const auto frame_submission = render_device->submit(std::move(frame_commands));
                if (frame_submission == vice::render::invalid_submission) {
                    log(LogLevel::error, "VICE Lab frame submission failed.");
                    running = false;
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::milliseconds{1});
    }

    (void)render_device->wait_idle();

    const bool player_synced = world_bridge.sync_character_to_world(
        world,
        simulation,
        player_entity
    );
    const bool cube_synced = world_bridge.sync_body_to_world(
        world,
        simulation,
        bindings,
        cube_entity
    );
    if (player_synced && cube_synced) {
        (void)save_lab_world(world);
    } else {
        log(
            LogLevel::error,
            "VICE Lab skipped World save because live simulation state could not be synchronized."
        );
    }

    if (const auto player_spatial = world.spatial_state(player_entity)) {
        if (auto* player_domain = domain_for_region(player_spatial->region)) {
            if (const auto player_character = world_bridge.character_id(world, simulation, player_entity)) {
                (void)player_domain->destroy_character(*player_character);
            }
        }
    }

    if (const auto cube_spatial = world.spatial_state(cube_entity)) {
        if (auto* cube_domain = domain_for_region(cube_spatial->region)) {
            (void)bindings.unbind(cube_entity, cube_domain->id());
            (void)cube_domain->destroy_body(cube_participant);
        }
    }
    (void)rigid_domain_a->destroy_body(ramp_participant);
    (void)rigid_domain_a->destroy_body(terrain_participant);
    (void)rigid_domain_b->destroy_body(terrain_participant);
    (void)world_bridge.unregister_region(region_a);
    (void)world_bridge.unregister_region(region_b);
    (void)simulation.unregister_domain(rigid_domain_a_id);
    (void)simulation.unregister_domain(rigid_domain_b_id);

    scene_pipeline.reset();
    fragment_shader.reset();
    vertex_shader.reset();
    frame_buffer.reset();
    scene_instance_buffer.reset();
    scene_index_buffer.reset();
    scene_vertex_buffer.reset();
    index_buffer.reset();
    instance_buffer.reset();
    vertex_buffer.reset();
    depth_target.reset();
    swapchain.reset();
    render_device.reset();
    window.reset();
    jobs.shutdown();
    log(LogLevel::info, "VICE Lab closed.");
    return 0;
}

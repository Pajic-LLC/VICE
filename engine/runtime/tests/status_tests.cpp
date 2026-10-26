#include <vice/runtime/status.hpp>

#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "VICE runtime status test failed: " << message << '\n';
    std::exit(1);
}

} // namespace

int main() {
    using namespace vice;

    const world::PersistentId camera_id{world::PersistentId::Bytes{
        0x82, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x40, 0x00,
        0x80, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x31,
    }};

    runtime::RuntimeStatusSnapshot status;
    status.project_id = "82000000-0000-4000-8000-000000000001";
    status.game_target = "vice-status-test";
    status.startup_world = "worlds/main.vworld";
    status.execution_state = runtime::RuntimeExecutionState::running;
    status.frame_index = 77U;
    status.frame_seconds = 0.016;
    status.frames_per_second = 62.5;
    status.profile.count = 2U;
    status.profile.frames[0] = runtime::RuntimeFrameProfileSample{
        .frame_index = 76U,
        .frame_seconds = 0.017,
        .update_seconds = 0.004,
        .render_seconds = 0.009,
    };
    status.profile.frames[1] = runtime::RuntimeFrameProfileSample{
        .frame_index = 77U,
        .frame_seconds = 0.016,
        .update_seconds = 0.0035,
        .render_seconds = 0.008,
    };
    status.world = runtime::RuntimeWorldDiagnostics{
        .entities = 12U,
        .resources = 7U,
        .relations = 3U,
        .regions = 3U,
        .full_regions = 1U,
        .reduced_regions = 1U,
        .inactive_regions = 1U,
    };
    status.camera.entity = camera_id;
    status.camera.global_position = world::GlobalPosition{
        1000000000000.25,
        -2000000000000.5,
        3000000000000.75,
    };
    status.camera.region = world::RegionId{9U};
    status.presentation = runtime::RuntimePresentationDiagnostics{
        .backend = render::Backend::direct3d12,
        .width = 2560U,
        .height = 1440U,
        .drawable = true,
    };
    status.render_device.capabilities = render::DeviceCapabilities{
        .unified_memory = false,
        .ray_tracing = true,
        .mesh_shaders = true,
        .max_sampler_anisotropy = 16U,
        .max_texture_dimension_1d = 16384U,
        .max_texture_dimension_2d = 16384U,
        .max_texture_dimension_3d = 2048U,
        .max_texture_array_layers = 2048U,
        .recommended_frames_in_flight = 3U,
    };
    status.render_device.counters = render::DeviceStats{
        .submitted_command_lists = 42U,
        .completed_submission = 41U,
        .created_buffers = 18U,
        .created_textures = 9U,
        .created_samplers = 4U,
        .created_shader_modules = 12U,
        .created_graphics_pipelines = 6U,
    };
    status.renderer.directional_shadow = runtime::RuntimeDirectionalShadowTelemetry{
        .requested_mode = runtime::RuntimeDirectionalShadowMode::ray_traced,
        .active_path = runtime::RuntimeDirectionalShadowPath::cascaded,
        .ray_tracing_supported = true,
        .ray_query_visibility_available = false,
    };
    status.renderer.ray_tracing = runtime::RuntimeRayTracingTelemetry{
        .supported = true,
        .scene_active = true,
        .blas_count = 4U,
        .tlas_instance_count = 12U,
        .blas_bytes = 4096U,
        .tlas_bytes = 1024U,
        .total_bytes = 5120U,
        .last_successful_execution = runtime::RuntimeRayTracingExecutionCounters{
            .blas_reuses = 4U,
            .tlas_refits = 1U,
        },
        .cumulative = runtime::RuntimeRayTracingExecutionCounters{
            .blas_creates = 4U,
            .blas_reuses = 40U,
            .tlas_builds = 1U,
            .tlas_refits = 20U,
        },
    };

    const auto json = runtime::serialize_runtime_status_snapshot(status, "session-17");
    require(json.starts_with("{\"schema\":\"vice.dev.runtime.v1\""), "versioned schema");
    require(json.find("\"session_id\":\"session-17\"") != std::string::npos, "session identity");
    require(json.find("\"execution_state\":\"running\"") != std::string::npos, "execution state");
    require(json.find("\"units\":\"seconds\"") != std::string::npos, "profile units");
    require(json.find("\"capacity\":120") != std::string::npos, "bounded profile capacity");
    require(json.find("\"count\":2") != std::string::npos, "profile count");
    require(json.find("\"stages\":[\"frame\",\"update\",\"render\"]") != std::string::npos,
        "named profile stages");
    require(json.find("\"frame_index\":77") != std::string::npos, "profile frame index");
    require(json.find("\"update\":0.0035000000000000001") != std::string::npos ||
        json.find("\"update\":0.0035") != std::string::npos, "profile update timing");
    require(json.find("\"render\":0.008") != std::string::npos, "profile render timing");
    require(json.find("\"backend\":\"direct3d12\"") != std::string::npos, "portable backend name");
    require(json.find("\"ray_tracing\":true") != std::string::npos, "portable RT capability");
    require(json.find("\"mesh_shaders\":true") != std::string::npos, "portable mesh shader capability");
    require(json.find("\"max_texture_dimension_2d\":16384") != std::string::npos,
        "portable texture capability");
    require(json.find("\"submitted_command_lists\":42") != std::string::npos,
        "portable submission counter");
    require(json.find("\"completed_submission\":41") != std::string::npos,
        "portable completion counter");
    require(json.find("\"created_graphics_pipelines\":6") != std::string::npos,
        "portable pipeline counter");
    require(json.find("MTL") == std::string::npos, "no Metal-native names");
    require(json.find("ID3D12") == std::string::npos, "no D3D12-native names");
    require(json.find("1000000000000.25") != std::string::npos, "large-world camera x precision");
    require(json.find("-2000000000000.5") != std::string::npos, "large-world camera y precision");
    require(json.find("3000000000000.75") != std::string::npos, "large-world camera z precision");
    require(json.find("\"full\":1,\"reduced\":1,\"inactive\":1") != std::string::npos, "fidelity counts");
    require(
        json.find("\"fallback_reason\":\"ray_query_visibility_unavailable\"") != std::string::npos,
        "shadow fallback reason"
    );
    require(json.find("\"blas_count\":4") != std::string::npos, "portable BLAS count");
    require(json.find("\"total_bytes\":5120") != std::string::npos, "portable RT memory total");
    require(json.ends_with("}\n"), "single JSON record newline");
    require(
        runtime::serialize_runtime_status_snapshot(status, "session-17") == json,
        "deterministic serialization"
    );

    world::World world;
    require(world.create_entity().valid(), "create authoritative World entity");
    require(world.create_resource().valid(), "create authoritative World resource");
    require(
        world.register_region(world::RegionDesc{
            .id = world::RegionId{1U},
            .simulation_island = world::SimulationIslandId{1U},
            .origin = world::GlobalPosition{1000000000000.0, 0.0, 0.0},
            .streaming_cell = world::StreamingCellId{1, 0, 0},
            .fidelity = world::RegionFidelity::reduced,
        }),
        "register authoritative World region"
    );

    const auto sampled = runtime::make_runtime_status_snapshot(
        "project",
        "target",
        "worlds/main.vworld",
        runtime::RuntimeExecutionState::starting,
        0U,
        0.0,
        render::Backend::metal,
        render::DeviceCapabilities{
            .unified_memory = true,
            .ray_tracing = false,
            .mesh_shaders = false,
            .max_sampler_anisotropy = 8U,
            .max_texture_dimension_2d = 8192U,
            .recommended_frames_in_flight = 2U,
        },
        render::DeviceStats{
            .submitted_command_lists = 2U,
            .completed_submission = 1U,
        },
        platform::WindowMetrics{
            .width = 1280U,
            .height = 720U,
            .drawable_width = 2560U,
            .drawable_height = 1440U,
            .scale_factor = 2.0F,
        },
        world,
        nullptr
    );
    require(sampled.world.entities == 1U, "status reads authoritative entity count");
    require(sampled.world.resources == 1U, "status reads authoritative resource count");
    require(sampled.world.regions == 1U, "status reads authoritative region count");
    require(sampled.world.reduced_regions == 1U, "status reads authoritative fidelity");
    require(sampled.profile.count == 0U, "sampling does not invent profile history");
    require(sampled.presentation.width == 2560U, "drawable width used");
    require(sampled.presentation.height == 1440U, "drawable height used");
    require(sampled.presentation.drawable, "drawable presentation state");
    require(sampled.render_device.capabilities.unified_memory, "portable capabilities preserved");
    require(sampled.render_device.capabilities.max_texture_dimension_2d == 8192U,
        "portable limits preserved");
    require(sampled.render_device.counters.submitted_command_lists == 2U,
        "portable counters preserved");
    require(!sampled.renderer.available(), "missing renderer remains explicitly unavailable");
    require(!sampled.camera.entity.has_value(), "no authored camera is not invented");

    return 0;
}

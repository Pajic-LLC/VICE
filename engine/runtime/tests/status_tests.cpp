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
    require(json.find("\"backend\":\"direct3d12\"") != std::string::npos, "portable backend name");
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
    require(sampled.presentation.width == 2560U, "drawable width used");
    require(sampled.presentation.height == 1440U, "drawable height used");
    require(sampled.presentation.drawable, "drawable presentation state");
    require(!sampled.renderer.available(), "missing renderer remains explicitly unavailable");
    require(!sampled.camera.entity.has_value(), "no authored camera is not invented");

    return 0;
}

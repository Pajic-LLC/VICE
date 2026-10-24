#include <vice/runtime/ray_tracing_telemetry.hpp>
#include <vice/runtime/scene.hpp>

#include <iostream>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

int main() {
    using namespace vice::runtime;

    RuntimeSceneRenderer renderer;
    const RuntimeRayTracingTelemetry empty{};
    CHECK(renderer.ray_tracing_telemetry() == empty);

    RuntimeRayTracingTelemetry populated{};
    populated.supported = true;
    populated.scene_active = true;
    populated.blas_count = 3U;
    populated.tlas_instance_count = 11U;
    populated.blas_bytes = 1024U;
    populated.tlas_bytes = 256U;
    populated.total_bytes = 1280U;
    populated.last_successful_execution.blas_reuses = 3U;
    populated.last_successful_execution.tlas_refits = 1U;
    populated.cumulative.blas_creates = 4U;
    populated.cumulative.tlas_builds = 2U;
    CHECK(populated != empty);
    CHECK(populated.total_bytes == populated.blas_bytes + populated.tlas_bytes);

    return 0;
}

#undef CHECK

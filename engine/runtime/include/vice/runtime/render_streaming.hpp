#pragma once

#include <vice/runtime/render_residency.hpp>
#include <vice/runtime/scene.hpp>

#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeRenderResidentScene {
    world::WorldSnapshot snapshot;
    RuntimeRenderResidencyPlan residency;
    std::vector<RenderMeshInstance> meshes;
    std::vector<RuntimeLight> lights;
    std::vector<RuntimeReflectionProbe> reflection_probes;
};

struct RuntimeRenderResidentSceneBuildResult {
    RuntimeRenderResidentScene scene;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeRenderResidentSceneBuildResult build_runtime_render_resident_scene(
    const world::World& world
);

} // namespace vice::runtime

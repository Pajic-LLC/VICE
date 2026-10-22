#include "gpu_shadow_batches.hpp"

#include <cassert>
#include <vector>

namespace {

[[nodiscard]] vice::world::PersistentId required_id(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    const world::ResourceId mesh_a{required_id("81000000-0000-4000-8000-000000000001")};
    const world::ResourceId mesh_b{required_id("81000000-0000-4000-8000-000000000002")};
    const world::ResourceId material{required_id("82000000-0000-4000-8000-000000000001")};

    std::vector<CookedMeshRecord> meshes(2U);
    meshes[0].resource_id = mesh_a;
    meshes[0].asset.indices = {0U, 1U, 2U};
    meshes[1].resource_id = mesh_b;
    meshes[1].asset.indices = {0U, 1U, 2U, 2U, 3U, 0U};

    std::vector<RenderMeshInstance> instances(4U);
    instances[0].mesh = mesh_a;
    instances[0].material.reset();
    instances[1].mesh = mesh_a;
    instances[1].material = material;
    instances[2].mesh = mesh_b;
    instances[2].material = material;
    instances[3].mesh = mesh_a;
    instances[3].material = material;

    const auto plan = detail::build_runtime_gpu_shadow_batch_plan(instances, meshes);
    assert(plan);
    assert(plan.plan.instance_batches.size() == instances.size());
    assert(plan.plan.batches.size() == 2U);
    assert(plan.plan.batch_meshes.size() == 2U);
    assert(plan.plan.batch_meshes[0] == mesh_a);
    assert(plan.plan.batch_meshes[1] == mesh_b);
    assert(plan.plan.batches[0].index_count == 3U);
    assert(plan.plan.batches[1].index_count == 6U);
    assert(plan.plan.instance_batches[0] == 0U);
    assert(plan.plan.instance_batches[1] == 0U);
    assert(plan.plan.instance_batches[2] == 1U);
    assert(plan.plan.instance_batches[3] == 0U);

    auto missing_mesh_instances = instances;
    missing_mesh_instances[0].mesh = world::ResourceId{
        required_id("81000000-0000-4000-8000-000000000099")
    };
    const auto missing_mesh = detail::build_runtime_gpu_shadow_batch_plan(
        missing_mesh_instances, meshes
    );
    assert(!missing_mesh);

    return 0;
}

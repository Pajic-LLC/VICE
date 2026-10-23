#include <vice/runtime/render_streaming.hpp>
#include <vice/runtime/scene.hpp>
#include <vice/runtime/virtual_geometry.hpp>
#include <vice/runtime/visibility.hpp>

#include <algorithm>
#include <limits>
#include <utility>

namespace vice::runtime::detail {
[[nodiscard]] RuntimeRenderResidentSceneBuildResult
build_runtime_render_resident_scene_with_virtual_geometry(const world::World& world);
} // namespace vice::runtime::detail

#define build_runtime_render_resident_scene(...) \
    detail::build_runtime_render_resident_scene_with_virtual_geometry(__VA_ARGS__)
#define render(...) render_legacy(__VA_ARGS__)
#include "scene_legacy.inc"
#undef render
#undef build_runtime_render_resident_scene

namespace vice::runtime {
namespace {

struct RuntimeVirtualGeometrySceneContext {
    CookedMeshCatalog* catalog = nullptr;
    RuntimeGpuMeshletTablePlan* gpu_meshlet_plan = nullptr;
    platform::WindowMetrics metrics{};
};

thread_local RuntimeVirtualGeometrySceneContext* active_virtual_geometry_context = nullptr;

class RuntimeVirtualGeometrySceneScope final {
public:
    explicit RuntimeVirtualGeometrySceneScope(RuntimeVirtualGeometrySceneContext& context) noexcept
        : previous_{active_virtual_geometry_context} {
        active_virtual_geometry_context = &context;
    }

    ~RuntimeVirtualGeometrySceneScope() {
        active_virtual_geometry_context = previous_;
    }

    RuntimeVirtualGeometrySceneScope(const RuntimeVirtualGeometrySceneScope&) = delete;
    RuntimeVirtualGeometrySceneScope& operator=(const RuntimeVirtualGeometrySceneScope&) = delete;

private:
    RuntimeVirtualGeometrySceneContext* previous_ = nullptr;
};

[[nodiscard]] world::GlobalPosition runtime_camera_global_position(
    const world::World& world,
    const RuntimeCameraView& camera
) noexcept {
    world::GlobalPosition result{
        static_cast<double>(camera.position.x),
        static_cast<double>(camera.position.y),
        static_cast<double>(camera.position.z),
    };
    if (const auto camera_entity = world.primary_camera(); camera_entity.has_value()) {
        if (const auto spatial = world.spatial_state(*camera_entity); spatial.has_value()) {
            result = spatial->global_position;
        }
    }
    return result;
}

[[nodiscard]] bool ensure_runtime_virtual_geometry_meshlet_alias(
    RuntimeGpuMeshletTablePlan& plan,
    const world::ResourceId resource,
    std::string* const diagnostics
) {
    if (const auto existing = plan.mesh_index(resource); existing.has_value()) {
        if (*existing >= plan.meshes.size() || plan.meshes[*existing].meshlet_count != 0U) {
            set_diagnostics(
                diagnostics,
                "Runtime virtual geometry resource collides with authoritative meshlet metadata."
            );
            return false;
        }
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (plan.mesh_ids.size() != plan.meshes.size() ||
        plan.mesh_ids.size() >= std::numeric_limits<std::uint32_t>::max() ||
        plan.meshlets.size() > std::numeric_limits<std::uint32_t>::max()) {
        set_diagnostics(
            diagnostics,
            "Runtime virtual geometry meshlet alias exceeds the CPU meshlet table contract."
        );
        return false;
    }
    plan.mesh_ids.push_back(resource);
    plan.meshes.push_back(RuntimeGpuMeshletMeshRecord{
        .first_meshlet = static_cast<std::uint32_t>(plan.meshlets.size()),
        .meshlet_count = 0U,
    });
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace

bool CookedMeshCatalog::upsert_runtime_record(
    CookedMeshRecord record,
    std::string* const diagnostics
) {
    if (!record.resource_id.valid() || record.asset.vertices.empty() || record.asset.indices.empty()) {
        set_diagnostics(diagnostics, "Runtime-generated mesh record is incomplete.");
        return false;
    }
    record.runtime_generated = true;
    const auto found = std::find_if(records_.begin(), records_.end(), [&](const auto& existing) {
        return existing.resource_id == record.resource_id;
    });
    if (found != records_.end()) {
        if (!found->runtime_generated) {
            set_diagnostics(
                diagnostics,
                "Runtime virtual geometry resource ID collides with an authored cooked mesh `" +
                    record.resource_id.value.to_string() + "`."
            );
            return false;
        }
        *found = std::move(record);
    } else {
        records_.push_back(std::move(record));
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

namespace detail {

RuntimeRenderResidentSceneBuildResult build_runtime_render_resident_scene_with_virtual_geometry(
    const world::World& world
) {
    auto result = ::vice::runtime::build_runtime_render_resident_scene(world);
    if (!result || active_virtual_geometry_context == nullptr ||
        active_virtual_geometry_context->catalog == nullptr ||
        active_virtual_geometry_context->gpu_meshlet_plan == nullptr ||
        result.scene.meshes.empty()) {
        return result;
    }

    auto& catalog = *active_virtual_geometry_context->catalog;
    auto& gpu_meshlet_plan = *active_virtual_geometry_context->gpu_meshlet_plan;
    const auto metrics = active_virtual_geometry_context->metrics;
    const float aspect = static_cast<float>(metrics.drawable_width) /
        static_cast<float>(std::max(1U, metrics.drawable_height));
    const auto authored_camera = primary_camera_view(world, aspect);
    const RuntimeCameraView camera = authored_camera.has_value()
        ? *authored_camera
        : fallback_camera(result.scene.meshes, catalog, aspect);
    const auto camera_global_position = runtime_camera_global_position(world, camera);
    const auto frustum = runtime_visibility_frustum(
        camera.forward,
        camera.up,
        camera.camera.vertical_fov_radians,
        camera.aspect_ratio,
        camera.camera.near_plane,
        camera.camera.far_plane
    );
    if (!valid(frustum)) {
        result.scene = {};
        result.diagnostics = "Runtime virtual geometry could not build the camera selection frustum.";
        return result;
    }

    const auto selected = select_runtime_virtual_geometry_instances(
        result.scene.meshes,
        catalog.records(),
        frustum,
        camera_global_position,
        camera.camera.vertical_fov_radians,
        static_cast<float>(metrics.drawable_height)
    );
    if (!selected) {
        result.scene = {};
        result.diagnostics = selected.diagnostics;
        return result;
    }

    auto selected_instances = selected.instances;
    for (auto& instance : selected_instances) {
        if (instance.geometry_node_index == render::virtual_geometry_invalid_node) continue;

        const auto source_mesh = instance.mesh;
        const auto node_index = instance.geometry_node_index;
        const auto variant_id = runtime_virtual_geometry_variant_resource_id(source_mesh, node_index);
        const auto* existing = catalog.find(variant_id);
        if (existing == nullptr) {
            const auto* const source = catalog.find(source_mesh);
            if (source == nullptr || source->runtime_generated) {
                result.scene = {};
                result.diagnostics = "Runtime virtual geometry could not resolve its authored source mesh.";
                return result;
            }
            auto variant = build_runtime_virtual_geometry_variant_asset(source->asset, node_index);
            if (!variant) {
                result.scene = {};
                result.diagnostics = variant.diagnostics;
                return result;
            }
            const auto bytes = variant_id.value.bytes();
            std::copy(bytes.begin(), bytes.end(), variant.asset->stable_id.begin());
            if (!catalog.upsert_runtime_record(CookedMeshRecord{
                    .resource_id = variant_id,
                    .path = {},
                    .asset = std::move(*variant.asset),
                    .runtime_generated = true,
                }, &result.diagnostics)) {
                result.scene = {};
                return result;
            }
        } else if (!existing->runtime_generated) {
            result.scene = {};
            result.diagnostics = "Runtime virtual geometry variant collides with an authored mesh resource.";
            return result;
        }
        if (!ensure_runtime_virtual_geometry_meshlet_alias(
                gpu_meshlet_plan,
                variant_id,
                &result.diagnostics
            )) {
            result.scene = {};
            return result;
        }

        instance.mesh = variant_id;
        instance.geometry_node_index = render::virtual_geometry_invalid_node;
    }
    result.scene.meshes = std::move(selected_instances);
    return result;
}

} // namespace detail

bool RuntimeSceneRenderer::render(
    const world::World& world,
    render::Swapchain& swapchain,
    const platform::WindowMetrics metrics,
    const render::Color clear_color,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        return render_legacy(world, swapchain, metrics, clear_color, diagnostics);
    }
    RuntimeVirtualGeometrySceneContext context{
        .catalog = &state_->catalog,
        .gpu_meshlet_plan = &state_->gpu_meshlet_plan,
        .metrics = metrics,
    };
    const RuntimeVirtualGeometrySceneScope scope{context};
    return render_legacy(world, swapchain, metrics, clear_color, diagnostics);
}

} // namespace vice::runtime

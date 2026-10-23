#include <vice/runtime/virtual_geometry_streaming.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) noexcept {
    return std::lexicographical_compare(
        left.value.bytes().begin(), left.value.bytes().end(),
        right.value.bytes().begin(), right.value.bytes().end()
    );
}

[[nodiscard]] bool key_less(
    const RuntimeVirtualGeometryPageKey& left,
    const RuntimeVirtualGeometryPageKey& right
) noexcept {
    if (left.mesh != right.mesh) return resource_less(left.mesh, right.mesh);
    return left.page_index < right.page_index;
}

[[nodiscard]] const CookedMeshRecord* find_mesh(
    const std::span<const CookedMeshRecord> meshes,
    const world::ResourceId resource
) noexcept {
    const auto found = std::find_if(meshes.begin(), meshes.end(), [&](const auto& mesh) {
        return mesh.resource_id == resource;
    });
    return found == meshes.end() ? nullptr : &*found;
}

[[nodiscard]] const RuntimeGpuVirtualGeometryNodeRecord* find_plan_node(
    const RuntimeGpuVirtualGeometryPlan& plan,
    const std::uint32_t mesh_index,
    const std::uint32_t local_node_index
) noexcept {
    const auto found = std::find_if(plan.nodes.begin(), plan.nodes.end(), [&](const auto& node) {
        return node.mesh_index == mesh_index && node.local_node_index == local_node_index;
    });
    return found == plan.nodes.end() ? nullptr : &*found;
}

[[nodiscard]] bool requested(
    const std::span<const RuntimeVirtualGeometryPageKey> requests,
    const RuntimeVirtualGeometryPageKey& key
) noexcept {
    return std::binary_search(requests.begin(), requests.end(), key, key_less);
}

} // namespace

RuntimeVirtualGeometryPageRequestBuildResult build_runtime_virtual_geometry_page_requests(
    const std::span<const RuntimeGpuVirtualGeometryPageRequest> gpu_requests,
    const RuntimeGpuVirtualGeometryPlan& plan
) {
    RuntimeVirtualGeometryPageRequestBuildResult result;
    result.requests.reserve(gpu_requests.size());
    for (const auto& request : gpu_requests) {
        if (request.scene_instance_index >= plan.scene_instance_count ||
            request.mesh_index >= plan.mesh_ids.size()) {
            result.requests.clear();
            result.diagnostics = "GPU virtual geometry page request references an invalid scene instance or mesh.";
            return result;
        }
        const auto* const node = find_plan_node(plan, request.mesh_index, request.node_index);
        if (node == nullptr || node->child_count != 0U || node->page_count != 1U ||
            node->page_offset != request.page_index) {
            result.requests.clear();
            result.diagnostics = "GPU virtual geometry page request does not reference its canonical leaf page.";
            return result;
        }
        result.requests.push_back(RuntimeVirtualGeometryPageKey{
            .mesh = plan.mesh_ids[request.mesh_index],
            .page_index = request.page_index,
        });
    }
    std::sort(result.requests.begin(), result.requests.end(), key_less);
    result.requests.erase(
        std::unique(result.requests.begin(), result.requests.end()),
        result.requests.end()
    );
    return result;
}

RuntimeVirtualGeometryResidentPool::RuntimeVirtualGeometryResidentPool(
    const std::uint64_t budget_bytes
) noexcept : budget_bytes_{budget_bytes} {}

bool RuntimeVirtualGeometryResidentPool::set_budget(const std::uint64_t budget_bytes) noexcept {
    if (budget_bytes < resident_bytes_) return false;
    budget_bytes_ = budget_bytes;
    return true;
}

std::uint64_t RuntimeVirtualGeometryResidentPool::budget_bytes() const noexcept {
    return budget_bytes_;
}

std::uint64_t RuntimeVirtualGeometryResidentPool::resident_bytes() const noexcept {
    return resident_bytes_;
}

std::span<const RuntimeVirtualGeometryResidentPage> RuntimeVirtualGeometryResidentPool::pages() const noexcept {
    return pages_;
}

const RuntimeVirtualGeometryResidentPage* RuntimeVirtualGeometryResidentPool::find(
    const RuntimeVirtualGeometryPageKey& key
) const noexcept {
    const auto found = std::lower_bound(
        pages_.begin(), pages_.end(), key,
        [](const RuntimeVirtualGeometryResidentPage& page, const RuntimeVirtualGeometryPageKey& value) {
            return key_less(page.key, value);
        }
    );
    return found != pages_.end() && found->key == key ? &*found : nullptr;
}

bool RuntimeVirtualGeometryResidentPool::resident(
    const RuntimeVirtualGeometryPageKey& key
) const noexcept {
    return find(key) != nullptr;
}

RuntimeVirtualGeometryResidentPoolUpdateResult RuntimeVirtualGeometryResidentPool::update(
    const std::span<const RuntimeVirtualGeometryPageKey> requests,
    const std::span<const CookedMeshRecord> meshes
) {
    RuntimeVirtualGeometryResidentPoolUpdateResult result;
    std::vector<RuntimeVirtualGeometryPageKey> ordered(requests.begin(), requests.end());
    for (const auto& request : ordered) {
        if (!request.mesh.valid()) {
            result.diagnostics = "Virtual geometry residency received an invalid mesh resource ID.";
            return result;
        }
    }
    std::sort(ordered.begin(), ordered.end(), key_less);
    ordered.erase(std::unique(ordered.begin(), ordered.end()), ordered.end());

    struct PendingPage {
        RuntimeVirtualGeometryPageKey key{};
        render::VirtualGeometryStreamPage geometry;
    };
    std::vector<PendingPage> pending;
    std::uint64_t requested_bytes = 0U;
    for (const auto& request : ordered) {
        if (const auto* const existing = find(request); existing != nullptr) {
            if (existing->byte_size() > std::numeric_limits<std::uint64_t>::max() - requested_bytes) {
                result.diagnostics = "Virtual geometry requested residency byte count overflowed.";
                return result;
            }
            requested_bytes += existing->byte_size();
            continue;
        }
        const auto* const mesh = find_mesh(meshes, request.mesh);
        if (mesh == nullptr || mesh->runtime_generated || mesh->path.empty()) {
            result.diagnostics = "Virtual geometry residency cannot resolve a file-backed authored mesh `" +
                request.mesh.value.to_string() + "`.";
            return result;
        }
        const auto directory = render::load_virtual_geometry_stream_directory(mesh->path);
        if (!directory) {
            result.diagnostics = "Virtual geometry residency could not read the page directory for `" +
                request.mesh.value.to_string() + "`: " + directory.diagnostics;
            return result;
        }
        const auto page = render::load_virtual_geometry_stream_page(
            mesh->path,
            *directory.directory,
            request.page_index
        );
        if (!page) {
            result.diagnostics = "Virtual geometry residency could not stream page " +
                std::to_string(request.page_index) + " for `" + request.mesh.value.to_string() +
                "`: " + page.diagnostics;
            return result;
        }
        const auto bytes = page.page->byte_size();
        if (bytes > std::numeric_limits<std::uint64_t>::max() - requested_bytes) {
            result.diagnostics = "Virtual geometry requested residency byte count overflowed.";
            return result;
        }
        requested_bytes += bytes;
        pending.push_back(PendingPage{.key = request, .geometry = std::move(*page.page)});
    }

    if (requested_bytes > budget_bytes_) {
        result.diagnostics = "Virtual geometry requested leaf pages exceed the resident pool budget.";
        return result;
    }

    auto staged = pages_;
    const std::uint64_t next_serial = serial_ + 1U;
    for (auto& page : staged) {
        if (requested(ordered, page.key)) page.last_use_serial = next_serial;
    }

    std::uint64_t staged_bytes = 0U;
    for (const auto& page : staged) staged_bytes += page.byte_size();
    std::uint64_t pending_bytes = 0U;
    for (const auto& page : pending) pending_bytes += page.geometry.byte_size();

    std::vector<std::size_t> eviction_order;
    eviction_order.reserve(staged.size());
    for (std::size_t index = 0U; index < staged.size(); ++index) {
        if (!requested(ordered, staged[index].key)) eviction_order.push_back(index);
    }
    std::sort(eviction_order.begin(), eviction_order.end(), [&](const auto left, const auto right) {
        if (staged[left].last_use_serial != staged[right].last_use_serial) {
            return staged[left].last_use_serial < staged[right].last_use_serial;
        }
        return key_less(staged[left].key, staged[right].key);
    });

    std::vector<RuntimeVirtualGeometryPageKey> evicted;
    std::vector<bool> remove(staged.size(), false);
    for (const auto index : eviction_order) {
        if (staged_bytes + pending_bytes <= budget_bytes_) break;
        remove[index] = true;
        staged_bytes -= staged[index].byte_size();
        evicted.push_back(staged[index].key);
    }
    if (staged_bytes + pending_bytes > budget_bytes_) {
        result.diagnostics = "Virtual geometry resident pool could not satisfy the requested working set.";
        return result;
    }

    std::vector<RuntimeVirtualGeometryResidentPage> committed;
    committed.reserve(staged.size() + pending.size());
    for (std::size_t index = 0U; index < staged.size(); ++index) {
        if (!remove[index]) committed.push_back(std::move(staged[index]));
    }
    for (auto& page : pending) {
        result.admitted.push_back(page.key);
        committed.push_back(RuntimeVirtualGeometryResidentPage{
            .key = page.key,
            .geometry = std::move(page.geometry),
            .last_use_serial = next_serial,
        });
    }
    std::sort(committed.begin(), committed.end(), [](const auto& left, const auto& right) {
        return key_less(left.key, right.key);
    });

    std::uint64_t committed_bytes = 0U;
    for (const auto& page : committed) committed_bytes += page.byte_size();
    pages_ = std::move(committed);
    resident_bytes_ = committed_bytes;
    serial_ = next_serial;
    std::sort(result.admitted.begin(), result.admitted.end(), key_less);
    std::sort(evicted.begin(), evicted.end(), key_less);
    result.evicted = std::move(evicted);
    result.resident_bytes = resident_bytes_;
    return result;
}

} // namespace vice::runtime

#pragma once

#include <vice/render/virtual_geometry_stream.hpp>
#include <vice/runtime/gpu_virtual_geometry.hpp>
#include <vice/runtime/scene.hpp>

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeVirtualGeometryPageKey {
    world::ResourceId mesh{};
    std::uint32_t page_index = 0U;

    friend bool operator==(const RuntimeVirtualGeometryPageKey&, const RuntimeVirtualGeometryPageKey&) = default;
};

struct RuntimeVirtualGeometryPageRequestBuildResult {
    std::vector<RuntimeVirtualGeometryPageKey> requests;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

[[nodiscard]] RuntimeVirtualGeometryPageRequestBuildResult
build_runtime_virtual_geometry_page_requests(
    std::span<const RuntimeGpuVirtualGeometryPageRequest> gpu_requests,
    const RuntimeGpuVirtualGeometryPlan& plan
);

struct RuntimeVirtualGeometryResidentPage {
    RuntimeVirtualGeometryPageKey key{};
    render::VirtualGeometryStreamPage geometry;
    std::uint64_t last_use_serial = 0U;

    [[nodiscard]] std::uint64_t byte_size() const noexcept {
        return geometry.byte_size();
    }
};

struct RuntimeVirtualGeometryResidentPoolUpdateResult {
    std::vector<RuntimeVirtualGeometryPageKey> admitted;
    std::vector<RuntimeVirtualGeometryPageKey> evicted;
    std::uint64_t resident_bytes = 0U;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

class RuntimeVirtualGeometryResidentPool final {
public:
    explicit RuntimeVirtualGeometryResidentPool(std::uint64_t budget_bytes = 0U) noexcept;

    [[nodiscard]] bool set_budget(std::uint64_t budget_bytes) noexcept;
    [[nodiscard]] std::uint64_t budget_bytes() const noexcept;
    [[nodiscard]] std::uint64_t resident_bytes() const noexcept;
    [[nodiscard]] std::span<const RuntimeVirtualGeometryResidentPage> pages() const noexcept;

    [[nodiscard]] bool resident(const RuntimeVirtualGeometryPageKey& key) const noexcept;
    [[nodiscard]] const RuntimeVirtualGeometryResidentPage* find(
        const RuntimeVirtualGeometryPageKey& key
    ) const noexcept;

    [[nodiscard]] RuntimeVirtualGeometryResidentPoolUpdateResult update(
        std::span<const RuntimeVirtualGeometryPageKey> requests,
        std::span<const CookedMeshRecord> meshes
    );

private:
    std::uint64_t budget_bytes_ = 0U;
    std::uint64_t resident_bytes_ = 0U;
    std::uint64_t serial_ = 0U;
    std::vector<RuntimeVirtualGeometryResidentPage> pages_;
};

} // namespace vice::runtime

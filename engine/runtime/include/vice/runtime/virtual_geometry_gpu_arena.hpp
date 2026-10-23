#pragma once

#include <vice/render/mesh_asset.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/resource.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::runtime {

inline constexpr std::uint64_t runtime_virtual_geometry_vertex_alignment = 16U;
inline constexpr std::uint64_t runtime_virtual_geometry_index_alignment = 4U;

struct RuntimeVirtualGeometryGpuArenaSource {
    world::ResourceId resource_id{};
    std::span<const render::MeshVertex> vertices;
    std::span<const std::uint32_t> indices;
};

struct RuntimeVirtualGeometryGpuArenaBinding {
    world::ResourceId resource_id{};
    render::Buffer* vertex_buffer = nullptr;
    render::Buffer* index_buffer = nullptr;
    std::uint64_t vertex_offset = 0U;
    std::uint64_t vertex_size = 0U;
    std::uint64_t index_offset = 0U;
    std::uint64_t index_size = 0U;
    std::uint32_t index_count = 0U;
    std::uint64_t generation = 0U;

    [[nodiscard]] bool valid() const noexcept {
        return resource_id.valid() && vertex_buffer != nullptr && index_buffer != nullptr &&
            vertex_size != 0U && index_size != 0U && index_count != 0U;
    }
};

struct RuntimeVirtualGeometryGpuArenaUpdateResult {
    bool repacked = false;
    bool grew = false;
    std::uint64_t generation = 0U;
    std::uint64_t vertex_bytes = 0U;
    std::uint64_t index_bytes = 0U;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return diagnostics.empty();
    }
};

class RuntimeVirtualGeometryBufferSlice final : public render::Buffer {
public:
    RuntimeVirtualGeometryBufferSlice(
        render::Buffer& backing,
        std::uint64_t offset,
        std::uint64_t size,
        render::BufferUsage usage
    ) noexcept;

    [[nodiscard]] std::uint64_t size() const noexcept override;
    [[nodiscard]] render::BufferUsage usage() const noexcept override;
    [[nodiscard]] render::MemoryLocation memory_location() const noexcept override;
    bool write(std::uint64_t offset, std::span<const std::byte> data) override;
    bool read(std::uint64_t offset, std::span<std::byte> data) override;

    [[nodiscard]] render::Buffer& binding_buffer() noexcept override;
    [[nodiscard]] std::uint64_t binding_offset() const noexcept override;

private:
    render::Buffer* backing_ = nullptr;
    render::Buffer* binding_ = nullptr;
    std::uint64_t offset_ = 0U;
    std::uint64_t binding_offset_ = 0U;
    std::uint64_t size_ = 0U;
    render::BufferUsage usage_ = render::BufferUsage::none;
};

class RuntimeVirtualGeometryGpuArena final {
public:
    RuntimeVirtualGeometryGpuArena();
    ~RuntimeVirtualGeometryGpuArena();

    RuntimeVirtualGeometryGpuArena(const RuntimeVirtualGeometryGpuArena&) = delete;
    RuntimeVirtualGeometryGpuArena& operator=(const RuntimeVirtualGeometryGpuArena&) = delete;
    RuntimeVirtualGeometryGpuArena(RuntimeVirtualGeometryGpuArena&&) noexcept;
    RuntimeVirtualGeometryGpuArena& operator=(RuntimeVirtualGeometryGpuArena&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeVirtualGeometryGpuArenaUpdateResult update(
        std::span<const RuntimeVirtualGeometryGpuArenaSource> sources
    );

    [[nodiscard]] std::optional<RuntimeVirtualGeometryGpuArenaBinding> find(
        world::ResourceId resource_id
    ) const noexcept;

    [[nodiscard]] std::span<const RuntimeVirtualGeometryGpuArenaBinding> bindings() const noexcept;
    [[nodiscard]] std::uint64_t generation() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime

#include <vice/runtime/virtual_geometry_gpu_arena.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint64_t initial_vertex_capacity = 64U * 1024U;
constexpr std::uint64_t initial_index_capacity = 32U * 1024U;

struct ArenaLayout {
    world::ResourceId resource_id{};
    std::uint64_t vertex_offset = 0U;
    std::uint64_t vertex_size = 0U;
    std::uint64_t index_offset = 0U;
    std::uint64_t index_size = 0U;
    std::uint32_t index_count = 0U;

    friend bool operator==(const ArenaLayout&, const ArenaLayout&) = default;
};

[[nodiscard]] bool resource_less(
    const world::ResourceId left,
    const world::ResourceId right
) noexcept {
    const auto& left_bytes = left.value.bytes();
    const auto& right_bytes = right.value.bytes();
    return std::lexicographical_compare(
        left_bytes.begin(), left_bytes.end(), right_bytes.begin(), right_bytes.end()
    );
}

[[nodiscard]] bool aligned_offset(
    const std::uint64_t value,
    const std::uint64_t alignment,
    std::uint64_t& result
) noexcept {
    if (alignment == 0U || value > std::numeric_limits<std::uint64_t>::max() - (alignment - 1U)) {
        return false;
    }
    result = (value + alignment - 1U) / alignment * alignment;
    return true;
}

[[nodiscard]] bool append_range(
    std::uint64_t& cursor,
    const std::uint64_t size,
    const std::uint64_t alignment,
    std::uint64_t& offset
) noexcept {
    if (!aligned_offset(cursor, alignment, offset) ||
        size > std::numeric_limits<std::uint64_t>::max() - offset) {
        return false;
    }
    cursor = offset + size;
    return true;
}

[[nodiscard]] std::uint64_t grown_capacity(
    const std::uint64_t required,
    const std::uint64_t minimum
) noexcept {
    if (required == 0U) return minimum;
    std::uint64_t capacity = minimum;
    while (capacity < required && capacity <= std::numeric_limits<std::uint64_t>::max() / 2U) {
        capacity *= 2U;
    }
    return std::max(capacity, required);
}

[[nodiscard]] std::unique_ptr<render::Buffer> make_arena_buffer(
    render::Device& device,
    const std::uint64_t size,
    const render::BufferUsage usage,
    const char* const debug_name
) {
    render::BufferDesc desc;
    desc.size = size;
    desc.usage = usage;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    return device.create_buffer(desc);
}

} // namespace

RuntimeVirtualGeometryBufferSlice::RuntimeVirtualGeometryBufferSlice(
    render::Buffer& backing,
    const std::uint64_t offset,
    const std::uint64_t size,
    const render::BufferUsage usage
) noexcept
    : backing_{&backing},
      binding_{&backing.binding_buffer()},
      offset_{offset},
      binding_offset_{backing.binding_offset() + offset},
      size_{size},
      usage_{usage} {}

std::uint64_t RuntimeVirtualGeometryBufferSlice::size() const noexcept {
    return size_;
}

render::BufferUsage RuntimeVirtualGeometryBufferSlice::usage() const noexcept {
    return usage_;
}

render::MemoryLocation RuntimeVirtualGeometryBufferSlice::memory_location() const noexcept {
    return backing_ != nullptr ? backing_->memory_location() : render::MemoryLocation::gpu_only;
}

bool RuntimeVirtualGeometryBufferSlice::write(
    const std::uint64_t offset,
    const std::span<const std::byte> data
) {
    return backing_ != nullptr && offset <= size_ && data.size() <= size_ - offset &&
        offset_ <= backing_->size() && offset <= backing_->size() - offset_ &&
        data.size() <= backing_->size() - offset_ - offset &&
        backing_->write(offset_ + offset, data);
}

bool RuntimeVirtualGeometryBufferSlice::read(
    const std::uint64_t offset,
    const std::span<std::byte> data
) {
    return backing_ != nullptr && offset <= size_ && data.size() <= size_ - offset &&
        offset_ <= backing_->size() && offset <= backing_->size() - offset_ &&
        data.size() <= backing_->size() - offset_ - offset &&
        backing_->read(offset_ + offset, data);
}

render::Buffer& RuntimeVirtualGeometryBufferSlice::binding_buffer() noexcept {
    return *binding_;
}

std::uint64_t RuntimeVirtualGeometryBufferSlice::binding_offset() const noexcept {
    return binding_offset_;
}

struct RuntimeVirtualGeometryGpuArena::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::Buffer> vertices;
    std::unique_ptr<render::Buffer> indices;
    std::uint64_t vertex_capacity = 0U;
    std::uint64_t index_capacity = 0U;
    std::uint64_t generation = 0U;
    std::vector<ArenaLayout> layout;
    std::vector<RuntimeVirtualGeometryGpuArenaBinding> bindings;
};

RuntimeVirtualGeometryGpuArena::RuntimeVirtualGeometryGpuArena()
    : state_{std::make_unique<State>()} {}
RuntimeVirtualGeometryGpuArena::~RuntimeVirtualGeometryGpuArena() = default;
RuntimeVirtualGeometryGpuArena::RuntimeVirtualGeometryGpuArena(
    RuntimeVirtualGeometryGpuArena&&
) noexcept = default;
RuntimeVirtualGeometryGpuArena& RuntimeVirtualGeometryGpuArena::operator=(
    RuntimeVirtualGeometryGpuArena&&
) noexcept = default;

bool RuntimeVirtualGeometryGpuArena::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        if (diagnostics != nullptr) {
            *diagnostics = "Runtime virtual geometry GPU arena state is unavailable.";
        }
        return false;
    }
    state_->device = &device;
    state_->vertices.reset();
    state_->indices.reset();
    state_->vertex_capacity = 0U;
    state_->index_capacity = 0U;
    state_->generation = 0U;
    state_->layout.clear();
    state_->bindings.clear();
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeVirtualGeometryGpuArenaUpdateResult RuntimeVirtualGeometryGpuArena::update(
    const std::span<const RuntimeVirtualGeometryGpuArenaSource> sources
) {
    RuntimeVirtualGeometryGpuArenaUpdateResult result;
    if (state_ == nullptr || state_->device == nullptr) {
        result.diagnostics = "Runtime virtual geometry GPU arena is not initialized.";
        return result;
    }

    std::vector<const RuntimeVirtualGeometryGpuArenaSource*> ordered;
    ordered.reserve(sources.size());
    for (const auto& source : sources) {
        if (!source.resource_id.valid() || source.vertices.empty() || source.indices.empty() ||
            source.indices.size() > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Runtime virtual geometry GPU arena received invalid source geometry.";
            return result;
        }
        ordered.push_back(&source);
    }
    std::sort(ordered.begin(), ordered.end(), [](const auto* left, const auto* right) {
        return resource_less(left->resource_id, right->resource_id);
    });
    for (std::size_t index = 1U; index < ordered.size(); ++index) {
        if (ordered[index - 1U]->resource_id == ordered[index]->resource_id) {
            result.diagnostics = "Runtime virtual geometry GPU arena received duplicate resource geometry.";
            return result;
        }
    }

    std::vector<ArenaLayout> next_layout;
    next_layout.reserve(ordered.size());
    std::uint64_t vertex_cursor = 0U;
    std::uint64_t index_cursor = 0U;
    for (const auto* source : ordered) {
        if (source->vertices.size() >
                std::numeric_limits<std::uint64_t>::max() / sizeof(render::MeshVertex) ||
            source->indices.size() >
                std::numeric_limits<std::uint64_t>::max() / sizeof(std::uint32_t)) {
            result.diagnostics = "Runtime virtual geometry GPU arena source byte size overflowed.";
            return result;
        }
        const std::uint64_t vertex_size = static_cast<std::uint64_t>(source->vertices.size()) *
            sizeof(render::MeshVertex);
        const std::uint64_t index_size = static_cast<std::uint64_t>(source->indices.size()) *
            sizeof(std::uint32_t);
        std::uint64_t vertex_offset = 0U;
        std::uint64_t index_offset = 0U;
        if (!append_range(
                vertex_cursor,
                vertex_size,
                runtime_virtual_geometry_vertex_alignment,
                vertex_offset
            ) || !append_range(
                index_cursor,
                index_size,
                runtime_virtual_geometry_index_alignment,
                index_offset
            )) {
            result.diagnostics = "Runtime virtual geometry GPU arena layout overflowed.";
            return result;
        }
        next_layout.push_back(ArenaLayout{
            .resource_id = source->resource_id,
            .vertex_offset = vertex_offset,
            .vertex_size = vertex_size,
            .index_offset = index_offset,
            .index_size = index_size,
            .index_count = static_cast<std::uint32_t>(source->indices.size()),
        });
    }
    result.vertex_bytes = vertex_cursor;
    result.index_bytes = index_cursor;

    if (next_layout == state_->layout) {
        result.generation = state_->generation;
        return result;
    }

    const bool need_vertex_growth = !ordered.empty() &&
        (state_->vertices == nullptr || vertex_cursor > state_->vertex_capacity);
    const bool need_index_growth = !ordered.empty() &&
        (state_->indices == nullptr || index_cursor > state_->index_capacity);
    if (need_vertex_growth || need_index_growth) {
        const std::uint64_t vertex_capacity = grown_capacity(
            vertex_cursor,
            std::max(initial_vertex_capacity, state_->vertex_capacity)
        );
        const std::uint64_t index_capacity = grown_capacity(
            index_cursor,
            std::max(initial_index_capacity, state_->index_capacity)
        );
        auto vertices = make_arena_buffer(
            *state_->device,
            vertex_capacity,
            render::BufferUsage::vertex,
            "VICE Runtime packed virtual geometry vertices"
        );
        auto indices = make_arena_buffer(
            *state_->device,
            index_capacity,
            render::BufferUsage::index,
            "VICE Runtime packed virtual geometry indices"
        );
        if (!vertices || !indices) {
            result.diagnostics = "VICE Runtime could not allocate packed virtual geometry arena buffers.";
            return result;
        }
        state_->vertices = std::move(vertices);
        state_->indices = std::move(indices);
        state_->vertex_capacity = vertex_capacity;
        state_->index_capacity = index_capacity;
        result.grew = true;
    }

    if (!ordered.empty() && (!state_->vertices || !state_->indices)) {
        result.diagnostics = "Runtime virtual geometry GPU arena backing buffers are unavailable.";
        return result;
    }
    for (std::size_t index = 0U; index < ordered.size(); ++index) {
        const auto* source = ordered[index];
        const auto& layout = next_layout[index];
        if (!state_->vertices->write(
                layout.vertex_offset,
                std::as_bytes(source->vertices)
            ) || !state_->indices->write(
                layout.index_offset,
                std::as_bytes(source->indices)
            )) {
            result.diagnostics = "VICE Runtime could not upload packed virtual geometry arena data.";
            return result;
        }
    }

    if (state_->generation == std::numeric_limits<std::uint64_t>::max()) {
        result.diagnostics = "Runtime virtual geometry GPU arena generation overflowed.";
        return result;
    }
    ++state_->generation;
    state_->layout = std::move(next_layout);
    state_->bindings.clear();
    state_->bindings.reserve(state_->layout.size());
    for (const auto& layout : state_->layout) {
        state_->bindings.push_back(RuntimeVirtualGeometryGpuArenaBinding{
            .resource_id = layout.resource_id,
            .vertex_buffer = state_->vertices.get(),
            .index_buffer = state_->indices.get(),
            .vertex_offset = layout.vertex_offset,
            .vertex_size = layout.vertex_size,
            .index_offset = layout.index_offset,
            .index_size = layout.index_size,
            .index_count = layout.index_count,
            .generation = state_->generation,
        });
    }
    result.repacked = true;
    result.generation = state_->generation;
    return result;
}

std::optional<RuntimeVirtualGeometryGpuArenaBinding> RuntimeVirtualGeometryGpuArena::find(
    const world::ResourceId resource_id
) const noexcept {
    if (state_ == nullptr) return std::nullopt;
    const auto found = std::lower_bound(
        state_->bindings.begin(),
        state_->bindings.end(),
        resource_id,
        [](const auto& binding, const world::ResourceId id) {
            return resource_less(binding.resource_id, id);
        }
    );
    if (found == state_->bindings.end() || found->resource_id != resource_id) {
        return std::nullopt;
    }
    return *found;
}

std::span<const RuntimeVirtualGeometryGpuArenaBinding>
RuntimeVirtualGeometryGpuArena::bindings() const noexcept {
    if (state_ == nullptr) return {};
    return state_->bindings;
}

std::uint64_t RuntimeVirtualGeometryGpuArena::generation() const noexcept {
    return state_ != nullptr ? state_->generation : 0U;
}

} // namespace vice::runtime

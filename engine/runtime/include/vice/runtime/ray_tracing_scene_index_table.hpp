#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

struct RuntimeRayTracingSceneIndexTableResult {
    render::Buffer* indices = nullptr;
    std::uint32_t index_count = 0U;
    std::uint32_t capacity = 0U;

    [[nodiscard]] explicit operator bool() const noexcept {
        return indices != nullptr && index_count != 0U;
    }
};

class RuntimeRayTracingSceneIndexTable final {
public:
    RuntimeRayTracingSceneIndexTable();
    ~RuntimeRayTracingSceneIndexTable();

    RuntimeRayTracingSceneIndexTable(const RuntimeRayTracingSceneIndexTable&) = delete;
    RuntimeRayTracingSceneIndexTable& operator=(const RuntimeRayTracingSceneIndexTable&) = delete;
    RuntimeRayTracingSceneIndexTable(RuntimeRayTracingSceneIndexTable&&) noexcept;
    RuntimeRayTracingSceneIndexTable& operator=(RuntimeRayTracingSceneIndexTable&&) noexcept;

    [[nodiscard]] bool initialize(render::Device& device, std::string* diagnostics = nullptr);

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        std::span<const std::uint32_t> scene_instance_indices,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeRayTracingSceneIndexTableResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime

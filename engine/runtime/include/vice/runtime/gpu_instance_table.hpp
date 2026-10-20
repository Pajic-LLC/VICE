#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string>

namespace vice::runtime {

struct RuntimeGpuInstanceTableResult {
    render::Buffer* models = nullptr;
    std::uint32_t instance_count = 0U;
    std::uint32_t capacity = 0U;
};

class RuntimeGpuInstanceTable final {
public:
    RuntimeGpuInstanceTable();
    ~RuntimeGpuInstanceTable();

    RuntimeGpuInstanceTable(const RuntimeGpuInstanceTable&) = delete;
    RuntimeGpuInstanceTable& operator=(const RuntimeGpuInstanceTable&) = delete;
    RuntimeGpuInstanceTable(RuntimeGpuInstanceTable&&) noexcept;
    RuntimeGpuInstanceTable& operator=(RuntimeGpuInstanceTable&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool upload(
        render::CommandList& commands,
        std::span<const math::Mat4> models,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeGpuInstanceTableResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime

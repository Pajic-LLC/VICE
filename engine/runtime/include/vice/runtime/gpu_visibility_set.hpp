#pragma once

#include <vice/render/rhi.hpp>
#include <vice/runtime/visibility.hpp>

#include <cstdint>
#include <memory>
#include <string>

namespace vice::runtime {

// Produces an all-visible GPU visibility stream in authoritative scene-instance
// order. Supporting passes can feed this directly into RuntimeGpuDrawBatcher
// without introducing a GPU -> CPU visibility readback. A later pass-specific
// culler can replace this source without changing the batching contract.
class RuntimeGpuIdentityVisibility final {
public:
    RuntimeGpuIdentityVisibility();
    ~RuntimeGpuIdentityVisibility();

    RuntimeGpuIdentityVisibility(const RuntimeGpuIdentityVisibility&) = delete;
    RuntimeGpuIdentityVisibility& operator=(const RuntimeGpuIdentityVisibility&) = delete;
    RuntimeGpuIdentityVisibility(RuntimeGpuIdentityVisibility&&) noexcept;
    RuntimeGpuIdentityVisibility& operator=(RuntimeGpuIdentityVisibility&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool prepare(
        render::CommandList& commands,
        std::uint32_t instance_count,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeVisibilityGpuResult result() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime

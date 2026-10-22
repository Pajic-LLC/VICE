#pragma once

#include <vice/render/rhi.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace vice::runtime {

struct RuntimeHiZLevelSize {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;

    [[nodiscard]] friend constexpr bool operator==(
        const RuntimeHiZLevelSize&,
        const RuntimeHiZLevelSize&
    ) noexcept = default;
};

[[nodiscard]] std::vector<RuntimeHiZLevelSize> runtime_hiz_level_sizes(
    std::uint32_t width,
    std::uint32_t height
);

class RuntimeHiZPyramid final {
public:
    RuntimeHiZPyramid();
    ~RuntimeHiZPyramid();

    RuntimeHiZPyramid(const RuntimeHiZPyramid&) = delete;
    RuntimeHiZPyramid& operator=(const RuntimeHiZPyramid&) = delete;
    RuntimeHiZPyramid(RuntimeHiZPyramid&&) noexcept;
    RuntimeHiZPyramid& operator=(RuntimeHiZPyramid&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool resize(
        std::uint32_t width,
        std::uint32_t height,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] bool build(
        render::CommandList& commands,
        render::Texture& camera_depth,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] std::uint32_t level_count() const noexcept;
    [[nodiscard]] RuntimeHiZLevelSize level_size(std::uint32_t level) const noexcept;
    [[nodiscard]] render::Texture* level(std::uint32_t level) const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime

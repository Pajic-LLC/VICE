#pragma once

#include <vice/math/math.hpp>
#include <vice/render/rhi.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cstdint>

namespace vice::runtime {

enum class RuntimePostProcessPass : std::uint8_t {
    temporal_resolve,
    bloom,
    tone_map,
};

struct RuntimePostProcessPassDesc {
    RuntimePostProcessPass pass = RuntimePostProcessPass::tone_map;
    bool enabled = false;
};

using RuntimePostProcessPlan = std::array<RuntimePostProcessPassDesc, 3U>;

[[nodiscard]] constexpr RuntimePostProcessPlan default_runtime_post_process_plan() noexcept {
    return {{
        RuntimePostProcessPassDesc{
            .pass = RuntimePostProcessPass::temporal_resolve,
            .enabled = false,
        },
        RuntimePostProcessPassDesc{
            .pass = RuntimePostProcessPass::bloom,
            .enabled = false,
        },
        RuntimePostProcessPassDesc{
            .pass = RuntimePostProcessPass::tone_map,
            .enabled = true,
        },
    }};
}

struct RuntimeTemporalFrameState {
    std::uint64_t frame_index = 0;
    std::uint64_t generation = 0;
    bool history_valid = false;
    world::PersistentId camera_entity{};
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
};

class RuntimeTemporalHistory final {
public:
    [[nodiscard]] RuntimeTemporalFrameState begin_frame(
        world::PersistentId camera_entity,
        const math::Mat4& current_view_projection
    ) noexcept;

    void commit_frame(
        world::PersistentId camera_entity,
        const math::Mat4& current_view_projection
    ) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t completed_frames() const noexcept { return completed_frames_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    world::PersistentId camera_entity_{};
    math::Mat4 previous_view_projection_{};
    std::uint64_t completed_frames_ = 0;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
};

struct RuntimePostProcessFrame {
    render::Texture* scene_color = nullptr;
    render::Texture* previous_scene_color = nullptr;
    render::Texture* motion_vectors = nullptr;
    render::Texture* depth = nullptr;
    RuntimeTemporalFrameState temporal{};
};

} // namespace vice::runtime

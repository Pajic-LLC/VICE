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
            .enabled = true,
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

inline constexpr std::uint32_t runtime_taa_jitter_period = 8U;
inline constexpr float runtime_taa_camera_cut_distance = 12.0F;
inline constexpr float runtime_taa_camera_cut_forward_dot = 0.5F;

struct RuntimeTemporalJitter {
    float x_pixels = 0.0F;
    float y_pixels = 0.0F;
    float x_ndc = 0.0F;
    float y_ndc = 0.0F;
};

[[nodiscard]] RuntimeTemporalJitter runtime_taa_jitter(
    std::uint64_t frame_index,
    std::uint32_t drawable_width,
    std::uint32_t drawable_height
) noexcept;

[[nodiscard]] math::Mat4 runtime_apply_projection_jitter(
    const math::Mat4& view_projection,
    RuntimeTemporalJitter jitter
) noexcept;

struct RuntimeTemporalFrameState {
    std::uint64_t frame_index = 0;
    std::uint64_t generation = 0;
    bool history_valid = false;
    world::PersistentId camera_entity{};
    math::Mat4 current_view_projection{};
    math::Mat4 previous_view_projection{};
    math::Mat4 current_unjittered_view_projection{};
    math::Mat4 previous_unjittered_view_projection{};
    RuntimeTemporalJitter jitter{};
    RuntimeTemporalJitter previous_jitter{};
};

class RuntimeTemporalHistory final {
public:
    [[nodiscard]] RuntimeTemporalFrameState begin_frame(
        world::PersistentId camera_entity,
        const math::Mat4& current_unjittered_view_projection,
        const math::Mat4& current_view_projection,
        RuntimeTemporalJitter jitter,
        math::Vec3 camera_position,
        math::Vec3 camera_forward
    ) noexcept;

    void commit_frame(
        world::PersistentId camera_entity,
        const math::Mat4& current_unjittered_view_projection,
        const math::Mat4& current_view_projection,
        RuntimeTemporalJitter jitter,
        math::Vec3 camera_position,
        math::Vec3 camera_forward
    ) noexcept;

    void reset() noexcept;

    [[nodiscard]] bool valid() const noexcept { return valid_; }
    [[nodiscard]] std::uint64_t completed_frames() const noexcept { return completed_frames_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

private:
    world::PersistentId camera_entity_{};
    math::Mat4 previous_unjittered_view_projection_{};
    math::Mat4 previous_view_projection_{};
    RuntimeTemporalJitter previous_jitter_{};
    math::Vec3 previous_camera_position_{};
    math::Vec3 previous_camera_forward_{};
    std::uint64_t completed_frames_ = 0;
    std::uint64_t generation_ = 0;
    bool valid_ = false;
};

struct RuntimePostProcessFrame {
    render::Texture* scene_color = nullptr;
    render::Texture* previous_scene_color = nullptr;
    render::Texture* motion_vectors = nullptr;
    render::Texture* depth = nullptr;
    render::Texture* previous_depth = nullptr;
    RuntimeTemporalFrameState temporal{};
};

} // namespace vice::runtime

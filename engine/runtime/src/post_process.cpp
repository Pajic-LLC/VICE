#include <vice/runtime/post_process.hpp>

#include <algorithm>

namespace vice::runtime {
namespace {

[[nodiscard]] float halton(std::uint32_t index, const std::uint32_t base) noexcept {
    float result = 0.0F;
    float fraction = 1.0F;
    while (index > 0U) {
        fraction /= static_cast<float>(base);
        result += fraction * static_cast<float>(index % base);
        index /= base;
    }
    return result;
}

[[nodiscard]] bool camera_discontinuity(
    const math::Vec3 previous_position,
    const math::Vec3 current_position,
    const world::GlobalPosition previous_global_position,
    const world::GlobalPosition current_global_position,
    const math::Vec3 previous_forward,
    const math::Vec3 current_forward
) noexcept {
    const bool has_global_position =
        world::finite(previous_global_position) && world::finite(current_global_position) &&
        (previous_global_position.x != 0.0 || previous_global_position.y != 0.0 ||
         previous_global_position.z != 0.0 || current_global_position.x != 0.0 ||
         current_global_position.y != 0.0 || current_global_position.z != 0.0);

    if (has_global_position) {
        const double dx = current_global_position.x - previous_global_position.x;
        const double dy = current_global_position.y - previous_global_position.y;
        const double dz = current_global_position.z - previous_global_position.z;
        const double distance_squared = dx * dx + dy * dy + dz * dz;
        const double cut_distance = static_cast<double>(runtime_taa_camera_cut_distance);
        if (distance_squared > cut_distance * cut_distance) return true;
    } else {
        const math::Vec3 translation = current_position - previous_position;
        const float distance_squared = math::dot(translation, translation);
        if (distance_squared >
            runtime_taa_camera_cut_distance * runtime_taa_camera_cut_distance) {
            return true;
        }
    }

    const math::Vec3 previous_direction = math::normalize(previous_forward);
    const math::Vec3 current_direction = math::normalize(current_forward);
    return math::dot(previous_direction, current_direction) < runtime_taa_camera_cut_forward_dot;
}

} // namespace

RuntimeTemporalJitter runtime_taa_jitter(
    const std::uint64_t frame_index,
    const std::uint32_t drawable_width,
    const std::uint32_t drawable_height
) noexcept {
    if (drawable_width == 0U || drawable_height == 0U) return {};

    const auto sample_index = static_cast<std::uint32_t>(
        frame_index % runtime_taa_jitter_period
    ) + 1U;
    const float x_pixels = halton(sample_index, 2U) - 0.5F;
    const float y_pixels = halton(sample_index, 3U) - 0.5F;
    return RuntimeTemporalJitter{
        .x_pixels = x_pixels,
        .y_pixels = y_pixels,
        .x_ndc = 2.0F * x_pixels / static_cast<float>(drawable_width),
        .y_ndc = -2.0F * y_pixels / static_cast<float>(drawable_height),
    };
}

math::Mat4 runtime_apply_projection_jitter(
    const math::Mat4& view_projection,
    const RuntimeTemporalJitter jitter
) noexcept {
    auto clip_translation = math::identity();
    clip_translation(0, 3) = jitter.x_ndc;
    clip_translation(1, 3) = jitter.y_ndc;
    return math::multiply(clip_translation, view_projection);
}

RuntimeTemporalFrameState RuntimeTemporalHistory::begin_frame(
    const world::PersistentId camera_entity,
    const math::Mat4& current_unjittered_view_projection,
    const math::Mat4& current_view_projection,
    const RuntimeTemporalJitter jitter,
    const math::Vec3 camera_position,
    const math::Vec3 camera_forward,
    const world::GlobalPosition camera_global_position
) noexcept {
    const bool discontinuity = valid_ && (
        camera_entity != camera_entity_ ||
        camera_discontinuity(
            previous_camera_position_,
            camera_position,
            previous_camera_global_position_,
            camera_global_position,
            previous_camera_forward_,
            camera_forward
        )
    );
    if (discontinuity) {
        valid_ = false;
        ++generation_;
    }

    return RuntimeTemporalFrameState{
        .frame_index = completed_frames_,
        .generation = generation_,
        .history_valid = valid_,
        .camera_entity = camera_entity,
        .current_view_projection = current_view_projection,
        .previous_view_projection = valid_ ? previous_view_projection_ : current_view_projection,
        .current_unjittered_view_projection = current_unjittered_view_projection,
        .previous_unjittered_view_projection = valid_
            ? previous_unjittered_view_projection_
            : current_unjittered_view_projection,
        .jitter = jitter,
        .previous_jitter = valid_ ? previous_jitter_ : jitter,
        .current_camera_global_position = camera_global_position,
        .previous_camera_global_position = valid_
            ? previous_camera_global_position_
            : camera_global_position,
    };
}

void RuntimeTemporalHistory::commit_frame(
    const world::PersistentId camera_entity,
    const math::Mat4& current_unjittered_view_projection,
    const math::Mat4& current_view_projection,
    const RuntimeTemporalJitter jitter,
    const math::Vec3 camera_position,
    const math::Vec3 camera_forward,
    const world::GlobalPosition camera_global_position
) noexcept {
    camera_entity_ = camera_entity;
    previous_unjittered_view_projection_ = current_unjittered_view_projection;
    previous_view_projection_ = current_view_projection;
    previous_jitter_ = jitter;
    previous_camera_position_ = camera_position;
    previous_camera_forward_ = camera_forward;
    previous_camera_global_position_ = camera_global_position;
    valid_ = true;
    ++completed_frames_;
}

void RuntimeTemporalHistory::reset() noexcept {
    camera_entity_ = {};
    previous_unjittered_view_projection_ = {};
    previous_view_projection_ = {};
    previous_jitter_ = {};
    previous_camera_position_ = {};
    previous_camera_forward_ = {};
    previous_camera_global_position_ = {};
    valid_ = false;
    ++generation_;
}

} // namespace vice::runtime

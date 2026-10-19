#include <vice/runtime/post_process.hpp>

namespace vice::runtime {

RuntimeTemporalFrameState RuntimeTemporalHistory::begin_frame(
    const world::PersistentId camera_entity,
    const math::Mat4& current_view_projection
) noexcept {
    if (valid_ && camera_entity != camera_entity_) {
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
    };
}

void RuntimeTemporalHistory::commit_frame(
    const world::PersistentId camera_entity,
    const math::Mat4& current_view_projection
) noexcept {
    camera_entity_ = camera_entity;
    previous_view_projection_ = current_view_projection;
    valid_ = true;
    ++completed_frames_;
}

void RuntimeTemporalHistory::reset() noexcept {
    camera_entity_ = {};
    previous_view_projection_ = {};
    valid_ = false;
    ++generation_;
}

} // namespace vice::runtime

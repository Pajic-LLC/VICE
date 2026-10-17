#include <vice/editor/state.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] bool persistent_id_less(
    const world::PersistentId& left,
    const world::PersistentId& right
) noexcept {
    return left.bytes() < right.bytes();
}

[[nodiscard]] bool relation_less(
    const world::SnapshotRelation& left,
    const world::SnapshotRelation& right
) noexcept {
    if (left.subject != right.subject) {
        return persistent_id_less(left.subject, right.subject);
    }
    if (left.kind != right.kind) {
        return persistent_id_less(left.kind, right.kind);
    }
    return persistent_id_less(left.object, right.object);
}

} // namespace

void SelectionModel::select(const world::PersistentId persistent_id) noexcept {
    if (persistent_id.is_nil()) {
        clear();
        return;
    }
    selected_ = persistent_id;
}

void SelectionModel::clear() noexcept {
    selected_.reset();
}

std::optional<world::PersistentId> SelectionModel::selected() const noexcept {
    return selected_;
}

std::optional<world::EntityId> SelectionModel::resolve(const world::World& world) const noexcept {
    if (!selected_.has_value()) {
        return std::nullopt;
    }
    return world.find(*selected_);
}

std::optional<EntityTransformState> capture_entity_transform(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) {
        return std::nullopt;
    }
    const auto spatial = world.spatial_state(*entity);
    if (!spatial.has_value()) {
        return std::nullopt;
    }
    return EntityTransformState{
        .spatial_state = *spatial,
        .authored_transform = world.authored_transform(*entity),
    };
}

bool apply_entity_transform(
    world::World& world,
    const world::PersistentId persistent_id,
    const EntityTransformState& state
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) {
        return false;
    }
    const auto previous = capture_entity_transform(world, persistent_id);
    if (!previous.has_value() || !world.set_spatial_state(*entity, state.spatial_state)) {
        return false;
    }

    bool authored_ok = true;
    if (state.authored_transform.has_value()) {
        authored_ok = world.set_authored_transform(*entity, *state.authored_transform);
    } else if (world.authored_transform(*entity).has_value()) {
        authored_ok = world.clear_authored_transform(*entity);
    }
    if (authored_ok) {
        return true;
    }

    (void)world.set_spatial_state(*entity, previous->spatial_state);
    if (previous->authored_transform.has_value()) {
        (void)world.set_authored_transform(*entity, *previous->authored_transform);
    } else if (world.authored_transform(*entity).has_value()) {
        (void)world.clear_authored_transform(*entity);
    }
    return false;
}

bool TransformHistory::record(Entry entry) {
    if (entry.persistent_id.is_nil() || entry.before == entry.after) {
        return false;
    }

    if (cursor_ < entries_.size()) {
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(cursor_), entries_.end());
        if (saved_cursor_.has_value() && *saved_cursor_ > cursor_) {
            saved_cursor_.reset();
        }
    }

    entries_.push_back(std::move(entry));
    cursor_ = entries_.size();
    return true;
}

bool TransformHistory::undo(world::World& world) {
    if (!can_undo()) {
        return false;
    }
    const Entry& entry = entries_[cursor_ - 1U];
    if (!apply_entity_transform(world, entry.persistent_id, entry.before)) {
        return false;
    }
    --cursor_;
    return true;
}

bool TransformHistory::redo(world::World& world) {
    if (!can_redo()) {
        return false;
    }
    const Entry& entry = entries_[cursor_];
    if (!apply_entity_transform(world, entry.persistent_id, entry.after)) {
        return false;
    }
    ++cursor_;
    return true;
}

void TransformHistory::clear() noexcept {
    entries_.clear();
    cursor_ = 0U;
    saved_cursor_ = 0U;
}

void TransformHistory::mark_saved() noexcept {
    saved_cursor_ = cursor_;
}

bool TransformHistory::can_undo() const noexcept {
    return cursor_ > 0U;
}

bool TransformHistory::can_redo() const noexcept {
    return cursor_ < entries_.size();
}

bool TransformHistory::dirty() const noexcept {
    return !saved_cursor_.has_value() || *saved_cursor_ != cursor_;
}

std::size_t TransformHistory::size() const noexcept {
    return entries_.size();
}

std::size_t TransformHistory::applied_count() const noexcept {
    return cursor_;
}

std::string TransformHistory::undo_label() const {
    return can_undo() ? entries_[cursor_ - 1U].label : std::string{};
}

std::string TransformHistory::redo_label() const {
    return can_redo() ? entries_[cursor_].label : std::string{};
}

std::vector<HierarchyEntry> build_hierarchy(const world::World& world) {
    const auto snapshot = world.snapshot();
    std::vector<HierarchyEntry> entries;
    entries.reserve(snapshot.entities.size());
    for (const auto& entity : snapshot.entities) {
        entries.push_back(HierarchyEntry{
            .persistent_id = entity.persistent_id,
            .spatial_state = entity.spatial_state,
        });
    }
    std::sort(entries.begin(), entries.end(), [](const auto& left, const auto& right) {
        return persistent_id_less(left.persistent_id, right.persistent_id);
    });
    return entries;
}

std::optional<InspectorData> project_inspector(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) {
        return std::nullopt;
    }

    InspectorData inspector;
    inspector.persistent_id = persistent_id;
    inspector.entity = entity;
    inspector.spatial_state = world.spatial_state(*entity);
    inspector.authored_transform = world.authored_transform(*entity);
    inspector.simulation_island = world.simulation_island(*entity);
    inspector.streaming_cell = world.streaming_cell(*entity);
    if (inspector.spatial_state.has_value()) {
        inspector.region = world.region(inspector.spatial_state->region);
    }

    const auto snapshot = world.snapshot();
    for (const auto& relation : snapshot.relations) {
        if (relation.subject == persistent_id || relation.object == persistent_id) {
            inspector.relations.push_back(relation);
        }
    }
    std::sort(inspector.relations.begin(), inspector.relations.end(), relation_less);
    return inspector;
}

struct ConsoleHistory::State {
    mutable std::mutex mutex;
    std::deque<ConsoleEntry> entries;
    std::size_t capacity = 1000;
    std::uint64_t next_sequence = 1;
};

ConsoleHistory::ConsoleHistory(const std::size_t capacity)
    : state_{std::make_shared<State>()} {
    state_->capacity = std::max<std::size_t>(1, capacity);
}

ConsoleHistory::~ConsoleHistory() {
    detach();
}

bool ConsoleHistory::attach() {
    if (observer_id_ != core::invalid_log_observer_id) {
        return true;
    }

    const std::weak_ptr<State> weak_state{state_};
    observer_id_ = core::add_log_observer([weak_state](const core::LogRecord& record) {
        if (const auto state = weak_state.lock()) {
            std::scoped_lock lock{state->mutex};
            if (state->entries.size() == state->capacity) {
                state->entries.pop_front();
            }
            state->entries.push_back(ConsoleEntry{
                .sequence = state->next_sequence++,
                .level = record.level,
                .message = record.message,
                .source_file = record.source.file_name(),
                .source_line = static_cast<std::uint32_t>(record.source.line()),
            });
        }
    });
    return observer_id_ != core::invalid_log_observer_id;
}

void ConsoleHistory::detach() {
    if (observer_id_ == core::invalid_log_observer_id) {
        return;
    }
    (void)core::remove_log_observer(observer_id_);
    observer_id_ = core::invalid_log_observer_id;
}

bool ConsoleHistory::attached() const noexcept {
    return observer_id_ != core::invalid_log_observer_id;
}

void ConsoleHistory::push(const core::LogRecord& record) {
    std::scoped_lock lock{state_->mutex};
    if (state_->entries.size() == state_->capacity) {
        state_->entries.pop_front();
    }
    state_->entries.push_back(ConsoleEntry{
        .sequence = state_->next_sequence++,
        .level = record.level,
        .message = record.message,
        .source_file = record.source.file_name(),
        .source_line = static_cast<std::uint32_t>(record.source.line()),
    });
}

void ConsoleHistory::clear() {
    std::scoped_lock lock{state_->mutex};
    state_->entries.clear();
}

std::vector<ConsoleEntry> ConsoleHistory::entries(const core::LogLevel minimum) const {
    std::scoped_lock lock{state_->mutex};
    std::vector<ConsoleEntry> filtered;
    filtered.reserve(state_->entries.size());
    for (const auto& entry : state_->entries) {
        if (entry.level >= minimum) {
            filtered.push_back(entry);
        }
    }
    return filtered;
}

std::size_t ConsoleHistory::size() const noexcept {
    std::scoped_lock lock{state_->mutex};
    return state_->entries.size();
}

std::size_t ConsoleHistory::capacity() const noexcept {
    std::scoped_lock lock{state_->mutex};
    return state_->capacity;
}

} // namespace vice::editor

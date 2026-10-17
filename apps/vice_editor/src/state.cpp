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

[[nodiscard]] std::string entry_label(const CommandHistory::Entry& entry) {
    return std::visit([](const auto& command) { return command.label; }, entry);
}

[[nodiscard]] bool create_snapshot_entity(
    world::World& world,
    const CommandHistory::CreateEntityEntry& command
) {
    std::vector<world::ResourceHandle> created_resources;
    created_resources.reserve(command.owned_resources.size());

    const auto rollback_resources = [&]() {
        for (auto iterator = created_resources.rbegin(); iterator != created_resources.rend(); ++iterator) {
            (void)world.destroy_resource(*iterator);
        }
    };

    for (const auto resource_id : command.owned_resources) {
        if (world.find_resource(resource_id).has_value()) {
            continue;
        }
        const auto created = world.create_resource(resource_id);
        if (!created.has_value()) {
            rollback_resources();
            return false;
        }
        created_resources.push_back(*created);
    }

    for (const auto reference : command.entity.resource_references) {
        if (!world.find_resource(reference.resource).has_value()) {
            rollback_resources();
            return false;
        }
    }

    const auto entity = world.create_entity(command.entity.persistent_id);
    if (!entity.has_value()) {
        rollback_resources();
        return false;
    }

    const auto rollback_entity = [&]() {
        (void)world.destroy_entity(*entity);
        rollback_resources();
    };

    if (command.entity.spatial_state.has_value() &&
        !world.set_spatial_state(*entity, *command.entity.spatial_state)) {
        rollback_entity();
        return false;
    }
    if (command.entity.authored_transform.has_value() &&
        !world.set_authored_transform(*entity, *command.entity.authored_transform)) {
        rollback_entity();
        return false;
    }
    for (const auto reference : command.entity.resource_references) {
        if (!world.set_resource_reference(*entity, reference)) {
            rollback_entity();
            return false;
        }
    }
    return true;
}

[[nodiscard]] bool destroy_snapshot_entity(
    world::World& world,
    const CommandHistory::CreateEntityEntry& command
) {
    const auto entity = world.find(command.entity.persistent_id);
    if (!entity.has_value()) {
        return false;
    }

    const auto snapshot = world.snapshot();
    for (const auto owned_resource : command.owned_resources) {
        for (const auto& current_entity : snapshot.entities) {
            if (current_entity.persistent_id == command.entity.persistent_id) {
                continue;
            }
            const bool referenced = std::any_of(
                current_entity.resource_references.begin(),
                current_entity.resource_references.end(),
                [owned_resource](const world::ResourceReference reference) {
                    return reference.resource == owned_resource;
                }
            );
            if (referenced) {
                return false;
            }
        }
    }

    if (!world.destroy_entity(*entity)) {
        return false;
    }
    for (const auto owned_resource : command.owned_resources) {
        if (const auto resource = world.find_resource(owned_resource); resource.has_value() &&
            !world.destroy_resource(*resource)) {
            return false;
        }
    }
    return true;
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

bool CommandHistory::record(TransformEntry entry) {
    if (entry.persistent_id.is_nil() || entry.before == entry.after) {
        return false;
    }
    return record_entry(Entry{std::move(entry)});
}

bool CommandHistory::record(CreateEntityEntry entry) {
    if (entry.entity.persistent_id.is_nil()) {
        return false;
    }
    return record_entry(Entry{std::move(entry)});
}

bool CommandHistory::record_entry(Entry entry) {
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

bool CommandHistory::undo(world::World& world) {
    if (!can_undo()) {
        return false;
    }
    const Entry& entry = entries_[cursor_ - 1U];
    const bool applied = std::visit(
        [&](const auto& command) {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, TransformEntry>) {
                return apply_entity_transform(world, command.persistent_id, command.before);
            } else {
                return destroy_snapshot_entity(world, command);
            }
        },
        entry
    );
    if (!applied) {
        return false;
    }
    --cursor_;
    return true;
}

bool CommandHistory::redo(world::World& world) {
    if (!can_redo()) {
        return false;
    }
    const Entry& entry = entries_[cursor_];
    const bool applied = std::visit(
        [&](const auto& command) {
            using Command = std::decay_t<decltype(command)>;
            if constexpr (std::is_same_v<Command, TransformEntry>) {
                return apply_entity_transform(world, command.persistent_id, command.after);
            } else {
                return create_snapshot_entity(world, command);
            }
        },
        entry
    );
    if (!applied) {
        return false;
    }
    ++cursor_;
    return true;
}

void CommandHistory::clear() noexcept {
    entries_.clear();
    cursor_ = 0U;
    saved_cursor_ = 0U;
}

void CommandHistory::mark_saved() noexcept {
    saved_cursor_ = cursor_;
}

bool CommandHistory::can_undo() const noexcept {
    return cursor_ > 0U;
}

bool CommandHistory::can_redo() const noexcept {
    return cursor_ < entries_.size();
}

bool CommandHistory::dirty() const noexcept {
    return !saved_cursor_.has_value() || *saved_cursor_ != cursor_;
}

std::size_t CommandHistory::size() const noexcept {
    return entries_.size();
}

std::size_t CommandHistory::applied_count() const noexcept {
    return cursor_;
}

std::string CommandHistory::undo_label() const {
    return can_undo() ? entry_label(entries_[cursor_ - 1U]) : std::string{};
}

std::string CommandHistory::redo_label() const {
    return can_redo() ? entry_label(entries_[cursor_]) : std::string{};
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
    const auto references = world.resource_references(*entity);
    inspector.resource_references.assign(references.begin(), references.end());
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

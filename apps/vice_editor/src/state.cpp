#include <vice/editor/state.hpp>

#include <algorithm>
#include <deque>
#include <mutex>
#include <type_traits>
#include <unordered_set>
#include <utility>

namespace vice::editor {
namespace {

[[nodiscard]] bool persistent_id_less(
    const world::PersistentId& left,
    const world::PersistentId& right
) noexcept {
    return left.bytes() < right.bytes();
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
    if (command.entity.camera.has_value() &&
        !world.set_camera(*entity, *command.entity.camera)) {
        rollback_entity();
        return false;
    }
    if (command.entity.light.has_value() &&
        !world.set_light(*entity, *command.entity.light)) {
        rollback_entity();
        return false;
    }
    if (command.entity.reflection_probe && !world.set_reflection_probe(*entity, *command.entity.reflection_probe)) {
        rollback_entity();
        return false;
    }
    if (command.entity.character && !world.set_character(*entity,*command.entity.character)) { rollback_entity(); return false; }
    if (command.entity.dependency && !world.set_dependency(*entity, *command.entity.dependency)) { rollback_entity(); return false; }
    if (command.entity.distance_link && !world.set_distance_link(*entity, *command.entity.distance_link)) {
        rollback_entity(); return false;
    }
    if (command.entity.rigid_box && !world.set_rigid_box(*entity, *command.entity.rigid_box)) {
        rollback_entity();
        return false;
    }
    if (command.entity.vehicle && !world.set_vehicle(*entity,*command.entity.vehicle)) { rollback_entity(); return false; }
    if(command.entity.character_attachment && !world.set_character_attachment(*entity,*command.entity.character_attachment)){rollback_entity();return false;}
    for(const auto& p:command.entity.components)if(!world.set_component(*entity,p)){rollback_entity();return false;}
    if(command.entity.animation&&!world.set_animation(*entity,*command.entity.animation)){rollback_entity();return false;}
    if(command.entity.rig_part&&!world.set_rig_part(*entity,*command.entity.rig_part)){rollback_entity();return false;}
    if(command.entity.audio_emitter&&!world.set_audio_emitter(*entity,*command.entity.audio_emitter)){rollback_entity();return false;}
    if(!world.set_entity_material(*entity,command.entity.material)) {rollback_entity();return false;}
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
    if(!world.relations_for(command.entity.persistent_id).empty()||!world.quantity_accounts(command.entity.persistent_id).empty())return false;
    for (const auto owned_resource : command.owned_resources) {
        if(std::any_of(snapshot.materials.begin(),snapshot.materials.end(),[&](const auto& m){return m.render_material==owned_resource;}))return false;
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

[[nodiscard]] bool apply_camera_states_once(
    world::World& world,
    const std::span<const EntityCameraState> states
) {
    for (const auto& state : states) {
        const auto entity = world.find(state.persistent_id);
        if (!entity.has_value()) return false;
        if (world.camera(*entity).has_value() && !world.clear_camera(*entity)) return false;
    }

    for (const bool primary : {false, true}) {
        for (const auto& state : states) {
            if (!state.camera.has_value() || state.camera->primary != primary) continue;
            const auto entity = world.find(state.persistent_id);
            if (!entity.has_value() || !world.set_camera(*entity, *state.camera)) return false;
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

std::optional<EntityCameraState> capture_entity_camera(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) return std::nullopt;
    return EntityCameraState{
        .persistent_id = persistent_id,
        .camera = world.camera(*entity),
    };
}

bool apply_camera_states(
    world::World& world,
    const std::span<const EntityCameraState> states
) {
    if (states.empty()) return true;

    std::unordered_set<world::PersistentId, world::PersistentIdHash> seen;
    std::size_t primary_count = 0;
    std::vector<EntityCameraState> previous;
    previous.reserve(states.size());
    for (const auto& state : states) {
        if (state.persistent_id.is_nil() || !seen.insert(state.persistent_id).second) return false;
        if (state.camera.has_value()) {
            if (!world::valid(*state.camera)) return false;
            if (state.camera->primary) ++primary_count;
        }
        const auto captured = capture_entity_camera(world, state.persistent_id);
        if (!captured.has_value()) return false;
        previous.push_back(*captured);
    }
    if (primary_count > 1U) return false;

    if (apply_camera_states_once(world, states)) return true;
    (void)apply_camera_states_once(world, previous);
    return false;
}

std::optional<EntityLightState> capture_entity_light(
    const world::World& world,
    const world::PersistentId persistent_id
) {
    const auto entity = world.find(persistent_id);
    if (!entity.has_value()) return std::nullopt;
    return EntityLightState{
        .persistent_id = persistent_id,
        .light = world.light(*entity),
    };
}

bool apply_entity_light(world::World& world, const EntityLightState& state) {
    if (state.persistent_id.is_nil()) return false;
    const auto entity = world.find(state.persistent_id);
    if (!entity.has_value()) return false;
    const auto previous = world.light(*entity);
    if (previous.has_value() && !world.clear_light(*entity)) return false;
    if (!state.light.has_value()) return true;
    if (world.set_light(*entity, *state.light)) return true;
    if (previous.has_value()) (void)world.set_light(*entity, *previous);
    return false;
}

bool CommandHistory::record(TransformEntry entry) {
    if (entry.persistent_id.is_nil() || entry.before == entry.after) {
        return false;
    }
    return record_entry(Entry{std::move(entry)});
}

bool CommandHistory::record(CameraEntry entry) {
    if (entry.before.empty() || entry.before == entry.after) return false;
    return record_entry(Entry{std::move(entry)});
}

bool CommandHistory::record(LightEntry entry) {
    if (entry.before.persistent_id.is_nil() || entry.before.persistent_id != entry.after.persistent_id ||
        entry.before == entry.after) {
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
            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.before);
            } else if constexpr (std::is_same_v<Command, LightEntry>) {
                return apply_entity_light(world, command.before);
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
            } else if constexpr (std::is_same_v<Command, CameraEntry>) {
                return apply_camera_states(world, command.after);
            } else if constexpr (std::is_same_v<Command, LightEntry>) {
                return apply_entity_light(world, command.after);
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

std::optional<world::PersistentId> create_mesh_instance(
    world::World& world,
    CommandHistory& history,
    const world::ResourceId mesh_resource,
    const world::GlobalPosition position,
    std::string* diagnostics
) {
    const auto fail = [&](const std::string& message) -> std::optional<world::PersistentId> {
        if (diagnostics != nullptr) *diagnostics = message;
        return std::nullopt;
    };
    if (!mesh_resource.valid() || !world::finite(position)) {
        return fail("Mesh instance requires a valid resource and finite position.");
    }
    const auto region = world.region_containing(position);
    if (!region.has_value()) {
        return fail("Mesh instance position is outside every registered World region.");
    }

    std::vector<world::ResourceId> owned_resources;
    if (!world.find_resource(mesh_resource).has_value()) {
        if (!world.create_resource(mesh_resource).has_value()) {
            return fail("Could not register the mesh resource in World.");
        }
        owned_resources.push_back(mesh_resource);
    }

    const auto entity = world.create_entity();
    const auto persistent_id = world.persistent_id(entity);
    const world::SpatialState spatial{
        .global_position = position,
        .region = *region,
    };
    const world::ResourceReference reference{
        .role = world::ResourceRole::render_mesh,
        .resource = mesh_resource,
    };
    if (!persistent_id.has_value() || !world.set_spatial_state(entity, spatial) ||
        !world.set_resource_reference(entity, reference)) {
        (void)world.destroy_entity(entity);
        for (const auto resource_id : owned_resources) {
            if (const auto resource = world.find_resource(resource_id); resource.has_value()) {
                (void)world.destroy_resource(*resource);
            }
        }
        return fail("Could not create the World mesh instance.");
    }

    CommandHistory::CreateEntityEntry command;
    command.entity = world::SnapshotEntity{
        .persistent_id = *persistent_id,
        .spatial_state = spatial,
        .resource_references = {reference},
    };
    command.owned_resources = owned_resources;
    command.label = "Add Mesh Instance";
    if (!history.record(std::move(command))) {
        (void)world.destroy_entity(entity);
        for (const auto resource_id : owned_resources) {
            if (const auto resource = world.find_resource(resource_id); resource.has_value()) {
                (void)world.destroy_resource(*resource);
            }
        }
        return fail("Could not record the mesh instance command.");
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return persistent_id;
}

std::vector<HierarchyEntry> build_hierarchy(const world::World& world) {
    const auto snapshot = world.snapshot(world::ComponentPayloadSnapshot::render);
    std::vector<HierarchyEntry> entries;
    entries.reserve(snapshot.entities.size());
    for (const auto& entity : snapshot.entities) {
        entries.push_back(HierarchyEntry{
            .persistent_id = entity.persistent_id,
            .spatial_state = entity.spatial_state,
            .camera = entity.camera,
            .light = entity.light,
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
    inspector.clock=world.world_clock();
    inspector.persistent_id = persistent_id;
    inspector.entity = entity;
    inspector.spatial_state = world.spatial_state(*entity);
    inspector.authored_transform = world.authored_transform(*entity);
    inspector.camera = world.camera(*entity);
    inspector.light = world.light(*entity);
    inspector.material = world.material_surface(*entity);
    inspector.rigid_box = world.rigid_box(*entity);
    inspector.distance_link = world.distance_link(*entity);
    inspector.dependency = world.dependency(*entity);
    inspector.character = world.character(*entity);
    inspector.vehicle = world.vehicle(*entity);
    inspector.components=world.components(*entity);for(const auto& p:inspector.components)if(const auto schema=world.component_type(p.type))inspector.component_schemas.push_back(*schema);
    inspector.animation=world.animation(*entity);inspector.rig_part=world.rig_part(*entity);inspector.audio_emitter=world.audio_emitter(*entity);
    inspector.character_attachment=world.character_attachment(*entity);
    const auto references = world.resource_references(*entity);
    inspector.resource_references.assign(references.begin(), references.end());
    inspector.simulation_island = world.simulation_island(*entity);
    inspector.streaming_cell = world.streaming_cell(*entity);
    if (inspector.spatial_state.has_value()) {
        inspector.region = world.region(inspector.spatial_state->region);
    }

    for(const auto key:world.relations_for(persistent_id))inspector.relations.push_back({key.subject,key.kind,key.object,world.relation_components(key)});
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

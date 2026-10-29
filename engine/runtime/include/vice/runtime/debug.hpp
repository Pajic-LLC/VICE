#pragma once

#include <vice/world/persistent_id.hpp>
#include <vice/world/spatial.hpp>
#include <vice/world/world.hpp>

#include <cstddef>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace vice::rigid_body { class AuthoredSimulation; }
namespace vice::presentation { class System; }

namespace vice::runtime {
class RuntimeSceneRenderer;

enum class RuntimeDebugEntityState : std::uint8_t {
    available,
    not_found,
    invalid_request,
};

struct RuntimeDebugRegionSnapshot {
    world::RegionId id{};
    world::SimulationIslandId simulation_island{};
    world::GlobalPosition origin{};
    world::StreamingCellId streaming_cell{};
    world::RegionFidelity fidelity = world::RegionFidelity::inactive;
};

struct CharacterRuntimeDebug {
    std::string ground_state;
    world::PersistentId supporting_entity{};
    math::Vec3 ground_normal{}, ground_velocity{};
};

struct VehicleWheelRuntimeDebug {
    bool grounded=false;
    world::PersistentId supporting_entity{},material{};
    world::GlobalPosition point{};
    math::Vec3 normal{};
    float suspension_length=0,normal_force=0,longitudinal_force=0,lateral_force=0;
};
struct VehicleRuntimeDebug {
    float forward_speed=0;
    std::array<VehicleWheelRuntimeDebug,4> wheels{};
};
struct RuntimeDebugEntitySnapshot {
    RuntimeDebugEntityState state = RuntimeDebugEntityState::invalid_request;
    world::PersistentId requested_id{};
    std::optional<world::SpatialState> spatial;
    std::optional<world::AuthoredTransform> authored_transform;
    world::CausalContext causal_history;
    world::WorldClock clock;
    world::SignalContext signals;
    world::ScheduleContext schedules;
    world::RuleInfo rules;
    world::RulePage rule_page;
    std::vector<world::QuantityAccount> quantity_accounts;
    std::vector<world::QuantityTypeDesc> quantity_types;
    std::optional<world::MaterialSurface> material;
    std::optional<float> native_mass;
    std::optional<world::AnimationPlayer> animation;
    std::optional<world::RigPart> rig_part;
    std::optional<world::AudioEmitter> audio_emitter;
    std::vector<world::ComponentPayload> components;
    std::vector<world::RelationState> relation_states;
    std::vector<world::ComponentTypeDesc> component_schemas;
    std::string animation_state = "null", audio_state = "null", skin_render = "null";
    std::optional<world::Vehicle> vehicle;
    std::optional<world::CharacterAttachment> character_attachment;
    std::optional<VehicleRuntimeDebug> vehicle_state;
    std::optional<world::Character> character;
    std::optional<CharacterRuntimeDebug> character_state;
    std::optional<world::RigidBox> rigid_box;
    std::optional<world::DistanceLink> distance_link;
    std::optional<world::PoweredHinge> powered_hinge;
    std::string powered_hinge_state = "null";
    std::optional<world::PoweredSwingTwist> swing_twist_joint;
    std::string swing_twist_state = "null";
    std::optional<world::DependencyState> dependency;
    std::optional<RuntimeDebugRegionSnapshot> region;
    bool camera = false;
    bool light = false;
    bool light_enabled = false;
    bool light_supplied = false;
    bool reflection_probe = false;
    std::size_t resource_reference_count = 0U;
    std::size_t outgoing_relation_count = 0U;
    std::size_t incoming_relation_count = 0U;
};

[[nodiscard]] RuntimeDebugEntitySnapshot
sample_runtime_debug_entity(const world::World &world, world::PersistentId persistent_id,
                            const rigid_body::AuthoredSimulation *physics = nullptr,
                            const presentation::System *presentation = nullptr,
                            const RuntimeSceneRenderer *renderer = nullptr);

[[nodiscard]] std::string serialize_runtime_debug_entity_snapshot(
    const RuntimeDebugEntitySnapshot& snapshot,
    std::string_view session_id
);

} // namespace vice::runtime

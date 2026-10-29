#pragma once

#include <cstddef>
#include <cstdint>
#include <new>

#if defined(_WIN32)
#define VICE_GAME_MODULE_EXPORT __declspec(dllexport)
#else
#define VICE_GAME_MODULE_EXPORT __attribute__((visibility("default")))
#endif

namespace vice::runtime::module {

inline constexpr std::uint32_t abi_version = 1U;
inline constexpr const char* entrypoint_name = "vice_game_module_load";

enum class LogLevel : std::uint32_t {
    debug,
    info,
    warning,
    error,
};

enum class EventKind : std::uint32_t {
    unknown,
    quit_requested,
    window_close_requested,
    window_metrics_changed,
    window_focus_changed,
    key,
    text_input,
    mouse_move,
    mouse_button,
    mouse_wheel,
};

enum class Key : std::uint32_t {
    unknown,
    escape,
    enter,
    tab,
    backspace,
    space,
    left,
    right,
    up,
    down,
    home,
    end,
    page_up,
    page_down,
    insert,
    delete_key,
    a,
    b,
    c,
    d,
    e,
    f,
    g,
    h,
    i,
    j,
    k,
    l,
    m,
    n,
    o,
    p,
    q,
    r,
    s,
    t,
    u,
    v,
    w,
    x,
    y,
    z,
    digit_0,
    digit_1,
    digit_2,
    digit_3,
    digit_4,
    digit_5,
    digit_6,
    digit_7,
    digit_8,
    digit_9,
    f1,
    f2,
    f3,
    f4,
    f5,
    f6,
    f7,
    f8,
    f9,
    f10,
    f11,
    f12,
};

struct Event {
    std::uint32_t size = sizeof(Event);
    EventKind kind = EventKind::unknown;
    std::uint32_t window = 0;
    std::uint32_t code = 0;
    std::uint32_t modifiers = 0;
    std::uint32_t flags = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t drawable_width = 0;
    std::uint32_t drawable_height = 0;
    float scale_factor = 1.0F;
    double x = 0.0;
    double y = 0.0;
    double delta_x = 0.0;
    double delta_y = 0.0;
};

struct Color {
    float red = 0.0F;
    float green = 0.0F;
    float blue = 0.0F;
    float alpha = 1.0F;
};

struct Frame {
    std::uint32_t size = sizeof(Frame);
    Color clear_color{};
};

// ABI records contain values only, never engine handles or solver types.
struct EntityId { std::uint8_t bytes[16]{}; };
struct Vector3 { float x = 0, y = 0, z = 0; };
enum class Result : std::uint32_t {
    success,
    unavailable,
    invalid_argument,
    not_found,
    no_body,
    static_body,
    inactive_region,
    reset_required,
    wrong_phase,
    no_link,
    no_dependency,
    no_character,
    not_grounded,
    history_mismatch,
    event_pruned,
    sequence_exhausted,
    no_vehicle,
    attached,
    blocked,
    out_of_range,
    no_attachment,
    no_camera,
    no_animation,
    no_audio,
    no_component,
    schema_conflict,
    no_relation,
    invalid_parent,
};
enum class Motion : std::uint32_t { static_body, kinematic_body, dynamic_body };
struct EntitySnapshot {
    std::uint32_t size = sizeof(EntitySnapshot);
    std::uint32_t flags = 0; // bit 0: spatial, bit 1: rigid box, bit 2: capsule character
    EntityId id{};
    std::uint64_t region = 0;
    std::uint32_t fidelity = 0; // 0 inactive, 1 reduced, 2 full
    Motion motion = Motion::static_body;
    double position[3]{};
    float rotation[4]{0, 0, 0, 1};
    float scale[3]{1, 1, 1};
    Vector3 half_extents{};
    Vector3 linear_velocity{};
    Vector3 angular_velocity{};
};

struct SupportSnapshot {
    std::uint32_t size = sizeof(SupportSnapshot);
    std::uint32_t supported = 0;
    EntityId supporting_entity{};
    double point[3]{};
    Vector3 normal{};
    Vector3 velocity{};
};

struct DistanceLinkSnapshot {
    std::uint32_t size = sizeof(DistanceLinkSnapshot);
    std::uint32_t broken = 0;
    EntityId first{}, second{};
    float min_distance = 0, max_distance = 0, frequency_hz = 0, damping_ratio = 0;
    float break_threshold = 0, last_break_impulse = 0;
    std::uint64_t break_step = 0;
};

enum class DependencyReason : std::uint32_t { available, disabled, link_broken, upstream_unavailable };
struct DependencySnapshot {
    std::uint32_t size = sizeof(DependencySnapshot);
    std::uint32_t available = 0;
    DependencyReason reason = DependencyReason::available;
    DependencyReason root_reason = DependencyReason::available;
    EntityId provider{}, gate{}, root_cause{};
    float break_impulse = 0;
    std::uint32_t path_length = 0;
    std::uint64_t break_step = 0;
};

enum class CharacterGroundState : std::uint32_t { grounded, steep_ground, unsupported, airborne };
struct CharacterSnapshot {
    std::uint32_t size = sizeof(CharacterSnapshot);
    CharacterGroundState ground_state = CharacterGroundState::airborne;
    double position[3]{};
    float height=0, radius=0, mass=0, max_strength=0, max_slope_angle=0, step_height=0, stick_to_floor_distance=0;
    Vector3 velocity{}, horizontal_velocity{}, ground_normal{}, ground_velocity{};
    float pending_jump_speed = 0;
    EntityId supporting_entity{};
};

enum class CausalKind : std::uint32_t {
    capture_started,
    distance_link_changed,
    distance_link_removed,
    constraint_broken,
    dependency_configured,
    dependency_removed,
    dependency_enabled,
    dependency_state_changed,
    world_event
};
enum class CausalOrigin : std::uint32_t { runtime, authoring, physics, gameplay, derived };
enum class CausalQuantity : std::uint32_t { none, linear_impulse };
struct CausalHistorySnapshot {
    std::uint32_t size=sizeof(CausalHistorySnapshot), capturing=0;
    EntityId history_id{}, capture_id{};
    std::uint64_t first_sequence=1,next_sequence=1;
    std::uint32_t retained_count=0,capacity=0;
};
struct CausalEventSnapshot {
    std::uint32_t size=sizeof(CausalEventSnapshot);
    CausalKind kind=CausalKind::capture_started;
    CausalOrigin origin=CausalOrigin::runtime;
    std::uint32_t value=0;
    CausalQuantity quantity_kind=CausalQuantity::none;
    DependencyReason reason=DependencyReason::available,root_reason=DependencyReason::available;
    std::uint32_t parent_retained=0;
    std::uint64_t sequence=0,parent_sequence=0,solver_step=0,region=0;
    EntityId id{},parent_id{},capture_id{},subject{},first{},second{},root_entity{};
    float quantity=0;
};

struct MaterialSnapshot {
    std::uint32_t size=sizeof(MaterialSnapshot), has_native_mass=0;
    EntityId id{}, render_material{};
    float density=1000, friction=0.5F, restitution=0, native_mass=0;
    double volume=0, mass=0;
};
struct CausalMaterialSnapshot {
    std::uint32_t size=sizeof(CausalMaterialSnapshot);
    EntityId event_id{}, first_material{}, second_material{};
};
struct VehicleWheelSnapshot {
    std::uint32_t grounded=0;
    EntityId supporting_entity{},material{};
    double point[3]{};
    Vector3 normal{};
    float suspension_length=0,normal_force=0,longitudinal_force=0,lateral_force=0;
};
struct VehicleSnapshot {
    std::uint32_t size=sizeof(VehicleSnapshot);
    float throttle=0,steering=0,brake=0,forward_speed=0;
    double distance=0;
    VehicleWheelSnapshot wheels[4]{};
};
struct CharacterAttachmentSnapshot {
    std::uint32_t size=sizeof(CharacterAttachmentSnapshot),attached=0;
    EntityId carrier{};
    Vector3 offset{};
};
struct CameraPose {
    std::uint32_t size=sizeof(CameraPose);
    double position[3]{};
    float rotation[4]{0,0,0,1};
};
struct AnimationSnapshot {
    std::uint32_t size=sizeof(AnimationSnapshot),clip=0,playing=0,looping=0,node_count=0,part_count=0;
    float rate=1;double time=0,duration=0;char clip_name[129]{};
};
struct AudioSnapshot {
    std::uint32_t size=sizeof(AudioSnapshot),playing=0,looping=0,output_active=0;
    float gain=0,pitch=1,reference_distance=1,max_distance=50,left_gain=0,right_gain=0,distance=0;
    double time=0,duration=0;std::uint64_t delivered_frames=0,underrun_frames=0;
};
struct Quaternion {float x=0,y=0,z=0,w=1;};
// Fixed-layout wire values. No STL or game-native object memory crosses the module boundary.
enum class ComponentKind : std::uint32_t {
    boolean = 1,
    integer = 2,
    real = 3,
    text = 4,
    identifier = 5
};
inline constexpr std::uint32_t component_field_capacity = 16;
struct ComponentFieldSchema {
    std::uint32_t id = 0;
    ComponentKind kind = ComponentKind::boolean;
    char name[65]{};
};
struct ComponentSchema {
    std::uint32_t size = sizeof(ComponentSchema), version = 1, field_count = 0;
    EntityId type{};
    char name[129]{};
    ComponentFieldSchema fields[component_field_capacity]{};
};
struct ComponentValue {
    std::uint32_t id = 0;
    ComponentKind kind = ComponentKind::boolean;
    std::uint32_t boolean = 0;
    std::int64_t integer = 0;
    double real = 0;
    EntityId identifier{};
    char text[129]{};
};
struct ComponentData {
    std::uint32_t size = sizeof(ComponentData), version = 1, field_count = 0;
    EntityId type{};
    ComponentValue fields[component_field_capacity]{};
};
struct RelationKey {
    EntityId subject{}, kind{}, object{};
};
struct RelationInfo {
    std::uint32_t size = sizeof(RelationInfo), component_count = 0;
    std::uint64_t revision = 0;
    RelationKey key;
};
inline constexpr std::uint32_t relation_page_capacity = 64;
struct RelationPage {
    std::uint32_t size = sizeof(RelationPage), count = 0, has_more = 0, cursor_valid = 0;
    // Zero requests a new view; retained tokens are invalidated by relation edits/restore.
    std::uint64_t revision = 0;
    RelationKey after{};
    RelationKey entries[relation_page_capacity]{};
};
enum class RelationDirection : std::uint32_t { outgoing = 1, incoming = 2, both = 3 };
struct WorldClockSnapshot {std::uint32_t size=sizeof(WorldClockSnapshot),rate_milli=1000,rate_remainder=0,paused=0,automatic=1;std::uint64_t ticks=0,ticks_per_second=1'000'000;double input_fraction=0;};
struct EventRef {
    EntityId history{}, id{};
    std::uint64_t sequence = 0;
};
struct WorldEventRequest {
    std::uint32_t size = sizeof(WorldEventRequest), retained = 1, parent_count = 0;
    EntityId subject{}, target{}, object{};
    std::uint64_t region = 0;
    ComponentData payload;
    EventRef parents[4]{};
};
struct WorldEventSnapshot {
    std::uint32_t size = sizeof(WorldEventSnapshot), kind = 0, origin = 0, has_payload = 0,
                  parent_count = 0, retained = 1;
    EventRef ref;
    std::uint64_t world_ticks = 0, region = 0;
    EntityId subject{}, target{}, object{};
    ComponentData payload;
    EventRef parents[4]{};
    std::uint32_t parent_retained[4]{};
};
struct SignalWindowSnapshot {
    std::uint32_t size = sizeof(SignalWindowSnapshot), retained_count = 0, capacity = 256;
    EntityId stream{};
    std::uint64_t first_sequence = 1, next_sequence = 1, lifetime_ticks = 1'000'000;
};
struct HostApi {
    std::uint32_t abi_version = module::abi_version;
    std::uint32_t size = sizeof(HostApi);
    void* context = nullptr;
    std::size_t (*world_entity_count)(void* context) noexcept = nullptr;
    std::size_t (*world_resource_count)(void* context) noexcept = nullptr;
    void (*log)(void* context, LogLevel level, const char* message) noexcept = nullptr;
    // Append-only ABI 1 extension. Check size before accessing these slots.
    Result (*read_entity)(void* context, EntityId id, EntitySnapshot* output) noexcept = nullptr;
    Result (*set_body_velocity)(void* context, EntityId id, Vector3 linear, Vector3 angular) noexcept = nullptr;
    Result (*read_body_support)(void* context, EntityId id, Vector3 up, float min_up_dot, SupportSnapshot* output) noexcept = nullptr;
    Result (*read_distance_link)(void* context, EntityId id, DistanceLinkSnapshot* output) noexcept = nullptr;
    Result (*read_dependency)(void* context, EntityId id, DependencySnapshot* output) noexcept = nullptr;
    Result (*read_character)(void* context, EntityId id, CharacterSnapshot* output) noexcept = nullptr;
    Result (*control_character)(void* context, EntityId id, Vector3 horizontal, float jump_speed) noexcept = nullptr;
    Result (*read_causal_history)(void* context,CausalHistorySnapshot* output) noexcept = nullptr;
    Result (*read_causal_event)(void* context,EntityId history_id,std::uint64_t sequence,CausalEventSnapshot* output) noexcept = nullptr;
    Result (*set_dependency_enabled)(void* context,EntityId id,std::uint32_t enabled) noexcept = nullptr;
    Result (*read_material)(void* context,EntityId entity,MaterialSnapshot* output) noexcept = nullptr;
    Result (*read_causal_materials)(void* context,EntityId history,std::uint64_t sequence,CausalMaterialSnapshot* output) noexcept = nullptr;
    Result (*read_vehicle)(void* context,EntityId id,VehicleSnapshot* output) noexcept = nullptr;
    Result (*control_vehicle)(void* context,EntityId id,float throttle,float steering,float brake) noexcept = nullptr;
    Result (*read_character_attachment)(void* context,EntityId id,CharacterAttachmentSnapshot* output) noexcept = nullptr;
    Result (*attach_character)(void* context,EntityId character,EntityId carrier,Vector3 offset,float max_distance) noexcept = nullptr;
    Result (*detach_character)(void* context,EntityId character,Vector3 offset,float max_speed,float max_drop) noexcept = nullptr;
    Result (*read_camera_pose)(void* context,EntityId camera,CameraPose* output) noexcept = nullptr;
    Result (*set_camera_pose)(void* context,EntityId camera,const CameraPose* pose) noexcept = nullptr;
    Result (*read_animation)(void* context,EntityId id,AnimationSnapshot* output) noexcept = nullptr;
    Result (*control_animation)(void* context,EntityId id,std::uint32_t clip,float rate,std::uint32_t playing,std::uint32_t looping,std::uint32_t restart) noexcept = nullptr;
    Result (*read_audio)(void* context,EntityId id,AudioSnapshot* output) noexcept = nullptr;
    Result (*control_audio)(void* context,EntityId id,float gain,float pitch,std::uint32_t playing,std::uint32_t restart) noexcept = nullptr;
    Result (*set_character_facing)(void* context,EntityId id,Quaternion facing) noexcept = nullptr;
    Result (*register_component_schema)(void*,const ComponentSchema*) noexcept=nullptr;
    Result (*read_component_schema)(void*,EntityId type,ComponentSchema*) noexcept=nullptr;
    Result (*read_component)(void*,EntityId owner,EntityId type,ComponentData*) noexcept=nullptr;
    Result (*set_component)(void*,EntityId owner,const ComponentData*) noexcept=nullptr;
    Result (*remove_component)(void*,EntityId owner,EntityId type) noexcept=nullptr;

    Result (*read_relation)(void*,RelationKey,RelationInfo*) noexcept=nullptr;
    Result (*read_relations)(void*,EntityId endpoint,RelationDirection,EntityId kind,RelationPage*) noexcept=nullptr;
    Result (*add_relation)(void*,RelationKey) noexcept=nullptr;
    Result (*remove_relation)(void*,RelationKey) noexcept=nullptr;
    Result (*read_relation_component)(void*,RelationKey,EntityId type,ComponentData*) noexcept=nullptr;
    Result (*set_relation_component)(void*,RelationKey,const ComponentData*) noexcept=nullptr;
    Result (*remove_relation_component)(void*,RelationKey,EntityId type) noexcept=nullptr;

    Result (*read_world_clock)(void*,WorldClockSnapshot*) noexcept=nullptr;
    Result (*configure_world_clock)(void*,std::uint32_t paused,std::uint32_t rate,std::uint32_t automatic) noexcept=nullptr;
    Result (*step_world_clock)(void*,std::uint64_t ticks) noexcept=nullptr;

    Result (*emit_world_event)(void *, const WorldEventRequest *, EventRef *) noexcept = nullptr;
    Result (*read_world_event)(void *, EntityId history, std::uint64_t sequence,
                               WorldEventSnapshot *) noexcept = nullptr;
    Result (*read_signal_window)(void *, SignalWindowSnapshot *) noexcept = nullptr;
    Result (*read_world_signal)(void *, EntityId stream, std::uint64_t sequence,
                                WorldEventSnapshot *) noexcept = nullptr;
};

inline constexpr std::size_t host_api_v1_size = offsetof(HostApi, read_entity);

struct ModuleApi {
    std::uint32_t abi_version = module::abi_version;
    std::uint32_t size = sizeof(ModuleApi);
    void* state = nullptr;
    bool (*start)(void* state, const HostApi* host) noexcept = nullptr;
    void (*event)(void* state, const HostApi* host, const Event* event) noexcept = nullptr;
    void (*update)(void* state, const HostApi* host, double delta_seconds) noexcept = nullptr;
    void (*frame)(void* state, const HostApi* host, Frame* frame) noexcept = nullptr;
    void (*stop)(void* state, const HostApi* host) noexcept = nullptr;
    void (*destroy)(void* state) noexcept = nullptr;
};

using LoadFunction = bool (*)(
    std::uint32_t host_abi_version,
    const HostApi* host,
    ModuleApi* module
) noexcept;

class Context final {
public:
    explicit Context(const HostApi* host) noexcept : host_{host} {}

    [[nodiscard]] std::size_t entity_count() const noexcept {
        return host_ != nullptr && host_->world_entity_count != nullptr
            ? host_->world_entity_count(host_->context)
            : 0U;
    }

    [[nodiscard]] std::size_t resource_count() const noexcept {
        return host_ != nullptr && host_->world_resource_count != nullptr
            ? host_->world_resource_count(host_->context)
            : 0U;
    }

    [[nodiscard]] Result read_entity(EntityId id, EntitySnapshot& output) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, read_entity) + sizeof(host_->read_entity) || !host_->read_entity)
            return Result::unavailable;
        return host_->read_entity(host_->context, id, &output);
    }

    [[nodiscard]] Result set_body_velocity(EntityId id, Vector3 linear, Vector3 angular = {}) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, set_body_velocity) + sizeof(host_->set_body_velocity) || !host_->set_body_velocity)
            return Result::unavailable;
        return host_->set_body_velocity(host_->context, id, linear, angular);
    }

    [[nodiscard]] Result read_body_support(EntityId id, SupportSnapshot& output, Vector3 up = {0, 1, 0}, float min_up_dot = 0.7F) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, read_body_support) + sizeof(host_->read_body_support) || !host_->read_body_support)
            return Result::unavailable;
        return host_->read_body_support(host_->context, id, up, min_up_dot, &output);
    }

    [[nodiscard]] Result read_distance_link(EntityId id, DistanceLinkSnapshot& output) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, read_distance_link) + sizeof(host_->read_distance_link) || !host_->read_distance_link)
            return Result::unavailable;
        return host_->read_distance_link(host_->context, id, &output);
    }

    [[nodiscard]] Result read_dependency(EntityId id, DependencySnapshot& output) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, read_dependency) + sizeof(host_->read_dependency) || !host_->read_dependency)
            return Result::unavailable;
        return host_->read_dependency(host_->context, id, &output);
    }

    [[nodiscard]] Result read_character(EntityId id, CharacterSnapshot& output) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, read_character) + sizeof(host_->read_character) || !host_->read_character) return Result::unavailable;
        return host_->read_character(host_->context,id,&output);
    }
    [[nodiscard]] Result control_character(EntityId id, Vector3 horizontal, float jump_speed = 0) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, control_character) + sizeof(host_->control_character) || !host_->control_character) return Result::unavailable;
        return host_->control_character(host_->context,id,horizontal,jump_speed);
    }

    [[nodiscard]] Result read_causal_history(CausalHistorySnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_causal_history)+sizeof(host_->read_causal_history) || !host_->read_causal_history) return Result::unavailable;
        return host_->read_causal_history(host_->context,&output);
    }
    [[nodiscard]] Result read_causal_event(EntityId history_id,std::uint64_t sequence,CausalEventSnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_causal_event)+sizeof(host_->read_causal_event) || !host_->read_causal_event) return Result::unavailable;
        return host_->read_causal_event(host_->context,history_id,sequence,&output);
    }
    [[nodiscard]] Result set_dependency_enabled(EntityId id,bool enabled) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,set_dependency_enabled)+sizeof(host_->set_dependency_enabled) || !host_->set_dependency_enabled) return Result::unavailable;
        return host_->set_dependency_enabled(host_->context,id,enabled?1U:0U);
    }

    [[nodiscard]] Result read_material(EntityId entity,MaterialSnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_material)+sizeof(host_->read_material) || !host_->read_material)return Result::unavailable;
        return host_->read_material(host_->context,entity,&output);
    }
    [[nodiscard]] Result read_causal_materials(EntityId history,std::uint64_t sequence,CausalMaterialSnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_causal_materials)+sizeof(host_->read_causal_materials) || !host_->read_causal_materials)return Result::unavailable;
        return host_->read_causal_materials(host_->context,history,sequence,&output);
    }
    [[nodiscard]] Result read_vehicle(EntityId id,VehicleSnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_vehicle)+sizeof(host_->read_vehicle) || !host_->read_vehicle)return Result::unavailable;
        return host_->read_vehicle(host_->context,id,&output);
    }
    [[nodiscard]] Result control_vehicle(EntityId id,float throttle,float steering,float brake) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,control_vehicle)+sizeof(host_->control_vehicle) || !host_->control_vehicle)return Result::unavailable;
        return host_->control_vehicle(host_->context,id,throttle,steering,brake);
    }
    [[nodiscard]] Result read_character_attachment(EntityId id,CharacterAttachmentSnapshot& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_character_attachment)+sizeof(host_->read_character_attachment)||!host_->read_character_attachment)return Result::unavailable;
        return host_->read_character_attachment(host_->context,id,&output);
    }
    [[nodiscard]] Result attach_character(EntityId id,EntityId carrier,Vector3 offset,float max_distance) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,attach_character)+sizeof(host_->attach_character)||!host_->attach_character)return Result::unavailable;
        return host_->attach_character(host_->context,id,carrier,offset,max_distance);
    }
    [[nodiscard]] Result detach_character(EntityId id,Vector3 offset,float max_speed,float max_drop) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,detach_character)+sizeof(host_->detach_character)||!host_->detach_character)return Result::unavailable;
        return host_->detach_character(host_->context,id,offset,max_speed,max_drop);
    }
    [[nodiscard]] Result read_camera_pose(EntityId id,CameraPose& output) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,read_camera_pose)+sizeof(host_->read_camera_pose)||!host_->read_camera_pose)return Result::unavailable;
        return host_->read_camera_pose(host_->context,id,&output);
    }
    [[nodiscard]] Result set_camera_pose(EntityId id,const CameraPose& pose) const noexcept {
        if(!host_ || host_->size<offsetof(HostApi,set_camera_pose)+sizeof(host_->set_camera_pose)||!host_->set_camera_pose)return Result::unavailable;
        return host_->set_camera_pose(host_->context,id,&pose);
    }
    [[nodiscard]] Result read_animation(EntityId id,AnimationSnapshot& output)const noexcept {
        if(!host_||host_->size<offsetof(HostApi,read_animation)+sizeof(host_->read_animation)||!host_->read_animation)return Result::unavailable;
        return host_->read_animation(host_->context,id,&output);
    }
    [[nodiscard]] Result control_animation(EntityId id,std::uint32_t clip,float rate=1,bool playing=true,bool looping=true,bool restart=false)const noexcept {
        if(!host_||host_->size<offsetof(HostApi,control_animation)+sizeof(host_->control_animation)||!host_->control_animation)return Result::unavailable;
        return host_->control_animation(host_->context,id,clip,rate,playing?1U:0U,looping?1U:0U,restart?1U:0U);
    }
    [[nodiscard]] Result read_audio(EntityId id,AudioSnapshot& output)const noexcept {
        if(!host_||host_->size<offsetof(HostApi,read_audio)+sizeof(host_->read_audio)||!host_->read_audio)return Result::unavailable;
        return host_->read_audio(host_->context,id,&output);
    }
    [[nodiscard]] Result control_audio(EntityId id,float gain,float pitch,bool playing,bool restart=false)const noexcept {
        if(!host_||host_->size<offsetof(HostApi,control_audio)+sizeof(host_->control_audio)||!host_->control_audio)return Result::unavailable;
        return host_->control_audio(host_->context,id,gain,pitch,playing?1U:0U,restart?1U:0U);
    }
    [[nodiscard]] Result set_character_facing(EntityId id,Quaternion facing)const noexcept {
        if(!host_||host_->size<offsetof(HostApi,set_character_facing)+sizeof(host_->set_character_facing)||!host_->set_character_facing)return Result::unavailable;
        return host_->set_character_facing(host_->context,id,facing);
    }
    [[nodiscard]] Result register_component_schema(const ComponentSchema &schema) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, register_component_schema) +
                              sizeof(host_->register_component_schema) ||
            !host_->register_component_schema)
            return Result::unavailable;
        return host_->register_component_schema(host_->context, &schema);
    }
    [[nodiscard]] Result read_component_schema(EntityId type,
                                               ComponentSchema &output) const noexcept {
        if (!host_ ||
            host_->size <
                offsetof(HostApi, read_component_schema) + sizeof(host_->read_component_schema) ||
            !host_->read_component_schema)
            return Result::unavailable;
        return host_->read_component_schema(host_->context, type, &output);
    }
    [[nodiscard]] Result read_component(EntityId owner, EntityId type,
                                        ComponentData &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_component) + sizeof(host_->read_component) ||
            !host_->read_component)
            return Result::unavailable;
        return host_->read_component(host_->context, owner, type, &output);
    }
    [[nodiscard]] Result set_component(EntityId owner, const ComponentData &data) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, set_component) + sizeof(host_->set_component) ||
            !host_->set_component)
            return Result::unavailable;
        return host_->set_component(host_->context, owner, &data);
    }
    [[nodiscard]] Result remove_component(EntityId owner, EntityId type) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, remove_component) + sizeof(host_->remove_component) ||
            !host_->remove_component)
            return Result::unavailable;
        return host_->remove_component(host_->context, owner, type);
    }
    [[nodiscard]] Result read_relation(RelationKey key, RelationInfo &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_relation) + sizeof(host_->read_relation) ||
            !host_->read_relation)
            return Result::unavailable;
        return host_->read_relation(host_->context, key, &output);
    }
    [[nodiscard]] Result read_relations(EntityId endpoint, RelationDirection direction,
                                        EntityId kind, RelationPage &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_relations) + sizeof(host_->read_relations) ||
            !host_->read_relations)
            return Result::unavailable;
        return host_->read_relations(host_->context, endpoint, direction, kind, &output);
    }
    [[nodiscard]] Result add_relation(RelationKey key) const noexcept {
        if (!host_ || host_->size < offsetof(HostApi, add_relation) + sizeof(host_->add_relation) ||
            !host_->add_relation)
            return Result::unavailable;
        return host_->add_relation(host_->context, key);
    }
    [[nodiscard]] Result remove_relation(RelationKey key) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, remove_relation) + sizeof(host_->remove_relation) ||
            !host_->remove_relation)
            return Result::unavailable;
        return host_->remove_relation(host_->context, key);
    }
    [[nodiscard]] Result read_relation_component(RelationKey key, EntityId type,
                                                 ComponentData &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_relation_component) +
                              sizeof(host_->read_relation_component) ||
            !host_->read_relation_component)
            return Result::unavailable;
        return host_->read_relation_component(host_->context, key, type, &output);
    }
    [[nodiscard]] Result set_relation_component(RelationKey key,
                                                const ComponentData &data) const noexcept {
        if (!host_ ||
            host_->size <
                offsetof(HostApi, set_relation_component) + sizeof(host_->set_relation_component) ||
            !host_->set_relation_component)
            return Result::unavailable;
        return host_->set_relation_component(host_->context, key, &data);
    }
    [[nodiscard]] Result remove_relation_component(RelationKey key, EntityId type) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, remove_relation_component) +
                              sizeof(host_->remove_relation_component) ||
            !host_->remove_relation_component)
            return Result::unavailable;
        return host_->remove_relation_component(host_->context, key, type);
    }
    [[nodiscard]] Result read_world_clock(WorldClockSnapshot& output)const noexcept {if(!host_||host_->size<offsetof(HostApi,read_world_clock)+sizeof(host_->read_world_clock)||!host_->read_world_clock)return Result::unavailable;return host_->read_world_clock(host_->context,&output);}
    [[nodiscard]] Result configure_world_clock(bool paused,std::uint32_t rate_milli=1000,bool automatic=true)const noexcept {if(!host_||host_->size<offsetof(HostApi,configure_world_clock)+sizeof(host_->configure_world_clock)||!host_->configure_world_clock)return Result::unavailable;return host_->configure_world_clock(host_->context,paused?1U:0U,rate_milli,automatic?1U:0U);}
    [[nodiscard]] Result step_world_clock(std::uint64_t ticks)const noexcept {if(!host_||host_->size<offsetof(HostApi,step_world_clock)+sizeof(host_->step_world_clock)||!host_->step_world_clock)return Result::unavailable;return host_->step_world_clock(host_->context,ticks);}
    [[nodiscard]] Result emit_world_event(const WorldEventRequest &request,
                                          EventRef &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, emit_world_event) + sizeof(host_->emit_world_event) ||
            !host_->emit_world_event)
            return Result::unavailable;
        return host_->emit_world_event(host_->context, &request, &output);
    }
    [[nodiscard]] Result read_world_event(EntityId history, std::uint64_t sequence,
                                          WorldEventSnapshot &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_world_event) + sizeof(host_->read_world_event) ||
            !host_->read_world_event)
            return Result::unavailable;
        return host_->read_world_event(host_->context, history, sequence, &output);
    }
    [[nodiscard]] Result read_signal_window(SignalWindowSnapshot &output) const noexcept {
        if (!host_ ||
            host_->size <
                offsetof(HostApi, read_signal_window) + sizeof(host_->read_signal_window) ||
            !host_->read_signal_window)
            return Result::unavailable;
        return host_->read_signal_window(host_->context, &output);
    }
    [[nodiscard]] Result read_world_signal(EntityId stream, std::uint64_t sequence,
                                           WorldEventSnapshot &output) const noexcept {
        if (!host_ ||
            host_->size < offsetof(HostApi, read_world_signal) + sizeof(host_->read_world_signal) ||
            !host_->read_world_signal)
            return Result::unavailable;
        return host_->read_world_signal(host_->context, stream, sequence, &output);
    }
    void log(const LogLevel level, const char* const message) const noexcept {
        if (host_ != nullptr && host_->log != nullptr) {
            host_->log(host_->context, level, message != nullptr ? message : "");
        }
    }

private:
    const HostApi* host_ = nullptr;
};

class Game {
public:
    virtual ~Game() = default;

    [[nodiscard]] virtual bool on_start(Context&) { return true; }
    virtual void on_event(Context&, const Event&) {}
    virtual void on_update(Context&, double) {}
    virtual void on_frame(Context&, Frame&) {}
    virtual void on_stop(Context&) {}
};

namespace detail {

template <typename GameType>
[[nodiscard]] bool start(void* const state, const HostApi* const host) noexcept {
    if (state == nullptr || host == nullptr) return false;
    Context context{host};
    try {
        return static_cast<GameType*>(state)->on_start(context);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_start.");
        return false;
    }
}

template <typename GameType>
void event(
    void* const state,
    const HostApi* const host,
    const Event* const value
) noexcept {
    if (state == nullptr || host == nullptr || value == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_event(context, *value);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_event.");
    }
}

template <typename GameType>
void update(
    void* const state,
    const HostApi* const host,
    const double delta_seconds
) noexcept {
    if (state == nullptr || host == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_update(context, delta_seconds);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_update.");
    }
}

template <typename GameType>
void frame(
    void* const state,
    const HostApi* const host,
    Frame* const value
) noexcept {
    if (state == nullptr || host == nullptr || value == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_frame(context, *value);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_frame.");
    }
}

template <typename GameType>
void stop(void* const state, const HostApi* const host) noexcept {
    if (state == nullptr || host == nullptr) return;
    Context context{host};
    try {
        static_cast<GameType*>(state)->on_stop(context);
    } catch (...) {
        context.log(LogLevel::error, "Unhandled exception in game module on_stop.");
    }
}

template <typename GameType>
void destroy(void* const state) noexcept {
    delete static_cast<GameType*>(state);
}

template <typename GameType>
[[nodiscard]] bool load(
    const std::uint32_t host_abi_version,
    const HostApi* const host,
    ModuleApi* const output
) noexcept {
    if (host_abi_version != abi_version || host == nullptr || output == nullptr ||
        host->abi_version != abi_version || host->size < host_api_v1_size) {
        return false;
    }

    GameType* const game = new (std::nothrow) GameType{};
    if (game == nullptr) return false;

    *output = ModuleApi{
        .abi_version = abi_version,
        .size = sizeof(ModuleApi),
        .state = game,
        .start = &detail::start<GameType>,
        .event = &detail::event<GameType>,
        .update = &detail::update<GameType>,
        .frame = &detail::frame<GameType>,
        .stop = &detail::stop<GameType>,
        .destroy = &detail::destroy<GameType>,
    };
    return true;
}

} // namespace detail

} // namespace vice::runtime::module

#define VICE_GAME_MODULE(GameType) \
    extern "C" VICE_GAME_MODULE_EXPORT bool vice_game_module_load( \
        const std::uint32_t host_abi_version, \
        const ::vice::runtime::module::HostApi* const host, \
        ::vice::runtime::module::ModuleApi* const module \
    ) noexcept { \
        return ::vice::runtime::module::detail::load<GameType>(host_abi_version, host, module); \
    }

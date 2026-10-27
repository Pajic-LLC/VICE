# Authored four-wheel vehicles

## Decision and motivation

Add an optional World Vehicle component to an existing dynamic solid RigidBox.
Shared AuthoredSimulation evaluates four suspension rays and applies impulses to
the real chassis before each fixed solver step. The initial driver is CRASHOUT's
need for a persistent drivable object. The engine component contains physical
geometry, force parameters, normalized input and travelled distance. Ownership,
registration, police, traffic rules and other game policy remain game code.

This is an accessible physical prototype foundation. It is not a production tyre,
drivetrain or component-damage simulation. It does not use velocity teleportation
to make a car follow a desired heading.

## Physical contract

The chassis frame is Y-up, +Z forward and +X right. Front/rear axles are at
+/- wheelbase/2; left/right wheels are at +/- track/2. Mount height, wheel radius,
travel and axle geometry use meters independently of AuthoredTransform scale.
Chassis collision dimensions, density, mass and inertia follow existing authored
box and shared-material rules, including collider scale. No second vehicle mass
can contradict the solver's actual chassis mass.

Each wheel casts down the rotated chassis Y axis, excluding its chassis and all
triggers. The first solid hit must face the suspension with normal dot up >= 0.2.
Suspension length is clamp(hit distance - radius, 0, travel). A spring-damper uses
relative point velocity, including angular velocity of chassis and supporting
body. Load is clamp(spring * compression - damping * relative up velocity,
0, 2 * spring * travel). There is no pulling force on an extended suspension.

Front wheels steer by normalized input * max steer. Their forward axis is
projected onto the contact plane. Rear wheels stay aligned with the chassis.
Each wheel receives one quarter of the total configured drive/brake force.
Braking opposes relative longitudinal velocity and cancels drive at rest up to
its force limit. Lateral stiffness opposes relative sideways velocity, capped by
the per-wheel mass/time-step stopping response. The combined longitudinal/lateral
force fits a friction circle bounded by load * tire friction * road friction
(with coefficient capped at 10). Road friction comes from the contacted entity's
resolved World material, falling back to its legacy inline surface. Chassis
friction remains the body's collision property, not the tyre grip source.

Normal and tyre impulses apply at the contact point, producing real pitch, roll
and yaw. A dynamic supporting body receives the opposite impulse at that point.
Kinematic supports retain their externally controlled motion. An airborne wheel
provides no suspension, drive, brake or steering force. Chassis contacts, CCD,
collisions, constraints and ordinary solver damping continue to operate normally.

The replaceable rigid-body boundary gains apply_body_impulse with copied VICE
vectors. It accepts finite impulses/points on dynamic bodies and wakes them when
the impulse is nonzero. No native handle/type reaches World, Runtime or game SDK.

This first model has no wheel rotational inertia, gear ratios, engine torque/RPM,
rolling resistance law, speed governor, anti-roll bar, tyre heat, deformation,
component damage, fuel, audio or wheel rendering. Grip differences demonstrate
material interaction; they do not imply an implemented rain/weather system.

## Authority and guards

World owns vehicle inputs and odometer, alongside body pose/orientation/velocities.
Vehicle controls publish immediately, so a checkpoint taken before the next tick
preserves the command. Each successful active physical step integrates the length
of post-step chassis velocity * fixed dt into double-precision travelled meters.
This includes vertical movement and represents integrated travel, not engine wear
or a vehicle registration mileage policy.

Profiles and command scalars are finite and bounded. Vehicles require a spatial,
dynamic, solid chassis and cannot coexist with a capsule. Removing chassis or
spatial state clears its dependent vehicle. Changing a live chassis to static,
kinematic or trigger is rejected until the vehicle is explicitly removed.

An opaque process-unique vehicle generation detects additions, removals, external
profile/state edits and World replacement, including equal-sized replacements.
Same-value writes are no-ops. A physical reset is required after external edits;
commands, reads and stepping reject stale configuration. Existing material/body
pose/shape guards remain in effect. Reads also reject stale supporting bodies.
Generation values and backend caches never enter a save.

Reset constructs replacement derived state before publication. Failure to apply
or publish a physical step latches reset-required, preventing a partially applied
step from silently advancing again. Gameplay vehicle controls are valid only on
the owning callback thread in a writable lifecycle phase.

## Fidelity and boundaries

Each physical region evaluates only its own existing domain. Full and reduced
regions currently use the same bounded four-ray model and solver rate; reduced
is not a new analytical traffic approximation. Inactive regions skip rays, tyre
forces and odometer integration and reject controls. Contacts are reported
unavailable/airborne after an inactive step. Cost is four broad-phase-supported
ray queries per active vehicle and O(1) indexed entity/material/body lookups.
There is no full World snapshot or material catalog scan in vehicle stepping.

Existing ready-region chassis handoff preserves the owning identity, body motion,
vehicle inputs and odometer. Wheel contacts are derived and rebuilt on subsequent
steps. Cross-domain wheel queries and dynamic region activation remain separate
work; adjacent collision geometry must be represented in the active domain.
Production traffic scale and vehicle performance budgets have not been accepted.

## Persistence and authoring

Cooked format 13 appends a sparse table after the v12 material bindings:
uint32 count, then count records of entity UUID (16 bytes), fifteen float32 values
and float64 travelled distance (84 bytes per record). Float order is wheelbase,
track, mount height, wheel radius, travel, spring, damping, drive force, brake
force, lateral stiffness, max steer, tire friction, throttle, steering, brake.
Counts cannot exceed entity count. Duplicate/missing entities, truncated records,
invalid scalars and invalid chassis associations fail before World replacement.
Versions 1 through 12 remain readable and do not invent Vehicle components.

Source format remains 1, adding `vehicle` followed by the same sixteen values to
an entity. `set_vehicle UUID <values>` and `clear_vehicle UUID` use complete-candidate
transactions, revision checks, dry-run, impact reporting and atomic publication.
Canonical source serialization retains double precision for travelled distance.
CLI World inspect exposes all profile/input/state values. Editor entity recreation
restores the chassis before its vehicle; authored Inspector shows profile/state.

ABI 1 appends optional read_vehicle/control_vehicle HostApi slots, adds separate
size-guarded VehicleSnapshot/WheelSnapshot DTOs and appends no_vehicle to Result.
Previously published DTO layouts and existing numeric Result values stay intact.
Older hosts return unavailable through slot-size checks. Backend-disabled hosts
can author/cook/save/inspect vehicles and return unavailable for live physics.

Runtime debug exposes vehicle profile/state and four contact records: supporting
entity, material UUID, global point, normal, suspension length and applied modeled
normal/longitudinal/lateral force. These describe the last force evaluation, not a
post-solver contact manifold or replay record. Forward speed is read from current
native chassis motion. Editor preview uses the same simulation and SDK module;
its normal physics overlay shows wheel contact rings/load vectors and live
Inspector shows speed, distance and per-wheel supporting/material identity.

A checkpoint preserves the published World, controls, body state and existing
causal history. It does not save native contacts, module globals or input-device
state. Restart reconstructs suspension on stepping. This supports behavioral
resume, not a deterministic replay claim. Vehicle forces can break existing
constraints and trigger existing supply history; no new general collision or
vehicle-damage journal is introduced.

## Alternatives and acceptance

A kinematic bicycle/velocity controller was rejected because it bypasses chassis
contacts and material traction. A native backend-specific vehicle object was
rejected for this milestone to keep four-ray policy, material lookup and World
publication shared and inspectable. A richer replaceable vehicle backend remains
possible when rotational wheels/drivetrain and representative driving acceptance
justify its complexity.

Tests cover real suspension equilibrium at large global origins, acceleration,
braking/reverse, steering/yaw, rotated frames, dry/slippery/zero grip, friction-circle
bounds, airborne wheels, dynamic support reaction, wall collision, SDK thread/size/
phase/old-host guards, save/resume, Editor parity/source isolation/undo, invalid
configuration, stale edits/replacement, optional-backend behavior and composed
vehicle-to-tether-to-supply failure with preserved causal history.

The runnable vehicle-playground and validate_vehicle_workflow.py additionally
exercise revision-safe authoring, staging an SDK-only module, actual standalone
Runtime wheel/material evidence, acceleration, braking, checkpoint export and
restart. Native graphical execution is separate from hosted headless CI. This
milestone does not claim CRASHOUT's complete vehicle system or engine readiness.

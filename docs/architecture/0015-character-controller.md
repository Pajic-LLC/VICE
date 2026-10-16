# Character controller foundation

## Decision

VICE owns the character controller contract.

The first backend uses Jolt 5.6 `CharacterVirtual` inside a rigid simulation domain. Jolt character IDs, shapes, filters, contacts, allocators, and controller objects remain private implementation details.

Public gameplay code uses VICE `CharacterId`, `CharacterDesc`, `CharacterState`, and `Domain` methods only.

## Character identity

`CharacterId` is a stable VICE-owned identifier inside one rigid domain.

Zero is invalid. The current Jolt backend maps the VICE value to a deterministic native character ID internally.

A character ID must be unique among live characters in the same rigid domain.

Characters are stepped in ascending `CharacterId` order so VICE does not depend on insertion order for controller update order.

## Coordinates and shape

This first controller is Y-up.

`CharacterState::position` is the character foot position in the local rigid-domain coordinate frame. It is not the capsule center.

`CharacterDesc::height` is total standing capsule height. `radius` is the capsule radius. Height must be greater than twice the radius.

The backend creates the capsule around its own center and offsets it upward so the bottom of the capsule sits at the VICE foot position.

Characters are local simulation objects. Large-world character transfer between rigid islands is not part of this foundation.

## Fixed-step order

The rigid domain completes the normal rigid-body solver update first. Constraint break evaluation then completes. Character controllers update after that work in the same VICE fixed step.

This means a character sees current rigid-body transforms and ground velocity from the completed solver step.

The controller uses the coordinator fixed delta. It does not run from render frame time.

## Movement

Gameplay supplies horizontal target velocity through `set_character_horizontal_velocity`.

The input vector is expressed in local-domain meters per second and must have zero Y component. VICE does not apply an acceleration curve, input dead zone, camera-relative conversion, or gameplay speed cap in this layer.

When grounded and not already moving away from the supporting surface, the controller starts from the ground velocity. This allows moving rigid bodies to carry a character. Requested horizontal velocity is then added.

When airborne, the existing vertical velocity is preserved and the requested horizontal velocity is applied directly.

Gravity is applied once per fixed controller update.

## Jumping

`request_character_jump` stores a one-shot upward speed request in meters per second.

The request is consumed on the next character update. It only adds upward speed when the character is on normal walkable ground at that update. A request made while airborne does not stay buffered until landing.

Jump policy such as coyote time, jump buffering, variable jump height, double jump, stamina, and animation state belongs above this foundation.

## Ground state

VICE exposes four ground states:

- `grounded`
- `steep_ground`
- `unsupported`
- `airborne`

`grounded` means the supporting surface is inside the configured walkable slope limit.

`steep_ground` means the character is supported by a surface whose angle is above the walkable limit. The controller can continue to collide with it but should not treat it as normal walkable ground.

`unsupported` means the character is touching geometry but the contact does not support its weight.

`airborne` means the character has no supporting contact.

State also reports ground normal and ground velocity. When the supporting body is a VICE rigid participant, state reports its `ParticipantId` and its shared `MaterialId` when present.

## Slopes

`CharacterDesc::max_slope_angle` is expressed in radians and must be between zero and 90 degrees inclusive.

The Tier 1 regression sets a 50 degree walkable limit. A 30 degree surface must report `grounded`, while a 70 degree surface must report `steep_ground` at the tested contact pose.

VICE does not hide the distinction between normal and steep ground. Gameplay can decide whether steep ground should slide, trigger an animation, or use another movement policy.

## Stair stepping and floor adhesion

`step_height` controls the upward step distance used by the extended character update.

`stick_to_floor_distance` controls how far the controller can project downward to remain attached to nearby walkable ground while moving over small drops and changes in elevation.

Both values are expressed in meters and can be zero to disable that behavior.

The Tier 1 regression uses a 40 centimeter step height. The character must clear a 30 centimeter obstacle and must not climb a 65 centimeter obstacle.

## Collision filtering

`CharacterCollisionFilter` controls whether the character considers static bodies, dynamic bodies, and trigger bodies.

At least one of static or dynamic collision must be enabled.

Triggers are excluded by default. This keeps sensor volumes from blocking ordinary character movement. The backend evaluates the filter through VICE rigid-body metadata instead of exposing native body layers or body IDs.

The current foundation does not publish a separate VICE character-contact event stream. Character movement state is the public result of the controller update.

## Teleporting

`set_character_position` moves the foot position directly and refreshes contacts against the current rigid world.

This is a teleport operation. It does not sweep the character from the old position to the new one.

## Current boundaries

This foundation intentionally does not define:

- character versus character collision
- crouching or runtime capsule resizing
- arbitrary up vectors or planetary gravity
- character large-world island handoff
- network prediction, rollback, or reconciliation
- animation root motion policy
- ledge grabbing, ladders, swimming, or climbing
- a gameplay acceleration or friction model
- character contact events

Those features can build on the VICE identity, state, filtering, and fixed-step contract without exposing Jolt.

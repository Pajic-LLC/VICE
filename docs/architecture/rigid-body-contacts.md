# Rigid-body contacts and material interactions

VICE exposes rigid-body contact state through VICE-owned types. Jolt remains an implementation detail behind `VICE::RigidBody`.

## Contact events

`rigid_body::Domain::contact_events()` returns the events produced by the most recent fixed simulation step.

Each `ContactEvent` contains:

- contact phase: began, persisted, or ended
- contact kind: collision or trigger
- both `ParticipantId` values
- both `MaterialId` values
- one representative contact point in local simulation coordinates
- a normal oriented from the first reported participant toward the second
- penetration depth
- closing speed before contact resolution
- estimated normal impulse
- combined friction and restitution used for the contact
- an optional material interaction label

Participants are ordered by `ParticipantId` before events are published. Events are sorted after Jolt finishes the update so callback scheduling on worker threads does not define the public event order.

The estimated normal impulse is not an exact solver impulse. VICE uses Jolt's pre-solve collision response estimator. It is suitable for impact strength decisions such as audio, particles, damage thresholds, or telemetry, but code that requires an exact post-solve impulse needs a later solver feedback path.

Ended events reuse the last known point, normal, materials, and interaction label for the contact. Penetration, closing speed, and estimated impulse are zero for the ended phase.

## Triggers

A rigid body can set `BoxBodyDesc::trigger`.

Trigger bodies still participate in overlap detection and produce began, persisted, and ended events, but they do not create a physical collision response. Trigger events always report an estimated normal impulse of zero.

Trigger state is part of the VICE-owned body creation description. The large-world rigid-body bridge therefore carries it when a body moves between simulation islands.

## Material identity

`simulation::MaterialId` is shared simulation identity. It does not belong to Jolt.

A rigid domain can register a shared `simulation::Material` against a `MaterialId`. The rigid backend currently reads these properties:

- owner `rigid_body`, property `friction`
- owner `rigid_body`, property `restitution`

Both values accept `double` or `int64` material properties and must be finite and nonnegative.

If a body references a registered material, those properties override the fallback friction and restitution in `BoxBodyDesc`. A body with no material keeps the existing fallback behavior.

A material must be registered in every rigid domain that needs to simulate a body using that material. This keeps material ownership outside the solver and makes island handoff failure explicit if a target simulation island is not ready for that material.

## Material pair rules

`MaterialInteraction` defines an order-independent rule for a pair of material IDs.

A rule can override:

- combined friction
- combined restitution

It can also provide a short interaction label such as `steel_on_concrete`, `checkpoint`, or another project-defined meaning.

The label is returned on contact events. VICE does not interpret it. Gameplay, audio, rendering effects, AI perception, or another simulation domain can decide what the label means.

Pair rules are symmetric. Setting a rule for A and B is the same pair as B and A.

## Backend boundary

Jolt contact callbacks can execute on several worker threads. The backend copies all required data into VICE types while the callback is valid, protects the shared callback queue, and publishes a deterministic VICE event list only after `PhysicsSystem::Update` returns.

No `JPH::Body`, `BodyID`, `ContactManifold`, `SubShapeIDPair`, or other Jolt type appears in the public VICE API.

The first implementation supports box bodies. The event contract is designed to survive later shape assets and compound shapes without making Jolt identifiers part of gameplay state.

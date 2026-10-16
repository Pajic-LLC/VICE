# Continuous collision policy

## Decision

VICE owns rigid-body motion quality through `rigid_body::MotionQuality`.

The public modes are:

- `discrete`
- `continuous`

Discrete is the default. Continuous is opt-in per dynamic body and can be changed at runtime.

A static body cannot use continuous motion quality. Static geometry does not move through the simulation, so a continuous sweep policy has no meaning for it.

## Continuous semantics

`continuous` means VICE requests swept collision handling for the body's linear travel during a physics step.

With the current Jolt 5.6 backend, VICE maps this to Jolt linear-cast motion quality. Jolt casts the collision shape from the start position toward the integrated destination using the rotation from the start of the step.

The backend type does not appear in VICE public headers. Another rigid-body backend can implement the same VICE contract without matching Jolt's type names.

Continuous motion quality is intended for bodies such as:

- projectiles
- fast thrown objects
- high-speed vehicle parts
- small bodies that can travel farther than nearby collision thickness during one fixed step

It should not be enabled on every dynamic body by default. Swept collision handling costs more than discrete collision detection.

## What continuous does not guarantee

Continuous mode is not full rotational continuous collision detection.

A long, thin body with high angular velocity can still tunnel because the current backend sweeps linear translation using the starting rotation. Gameplay that depends on extreme angular motion needs a separate rotational collision policy or a different representation.

Continuous mode also does not promise that a body will complete its full requested displacement after a high-speed impact. The current backend can consume the remaining motion time at the first swept collision and resolve the response on a later integration step.

## Runtime changes

`Domain::set_body_motion_quality` changes the policy for an existing dynamic body.

The current policy is visible through `BodyState::motion_quality`.

Changing a static body to `continuous` fails. Setting a static body to `discrete` is a valid no-op.

Invalid motion-quality values are rejected rather than silently mapped to a backend default.

## Large-world handoff

`WorldBridge` transfers the live motion quality when a managed box moves between rigid simulation islands.

The bridge reads the current `BodyState` from the source domain and applies that motion quality to the recreated body in the target domain. This means a runtime change made after spawn survives island handoff.

The stored bridge creation record is updated after a successful handoff so later transfers continue from the latest policy.

## Contact events

The current Jolt backend can report contact candidates while processing multiple continuous bodies. In an edge case, a candidate contact can later become irrelevant because another swept collision occurs earlier in the same step. That can produce a short-lived began or persisted event followed by removal.

VICE keeps contact event types and ordering under its own API, but this foundation does not yet reconstruct a separate post-solve contact graph to hide that candidate behavior.

Gameplay should use completed body state as the authoritative physical result. Systems that require stronger event guarantees should treat contact events as step observations until a post-solve contact contract is added.

## Regression contract

The Tier 1 regression uses a small dynamic projectile moving at 240 meters per second toward a 5 centimeter thick static wall with a 120 Hz fixed step.

At that speed the projectile travels about 2 meters in one fixed step. The continuous case must not tunnel through the wall. The discrete case is kept as a control but VICE does not define tunneling as required discrete behavior. A future backend may improve discrete collision detection without violating the VICE contract.

The regression also verifies:

- discrete is the default body motion quality
- continuous can be selected at body creation
- continuous can be enabled and disabled at runtime
- static continuous bodies are rejected
- the registered-shape body creation path honors continuous mode
- live motion quality survives large-world island handoff

## Backend boundary

Jolt `EMotionQuality` and `LinearCast` remain private implementation details.

Public gameplay code depends only on VICE `MotionQuality`, `BodyDesc`, `BoxBodyDesc`, `BodyState`, and `Domain` methods.

# Authored capsule characters

## Decision

World owns an optional `Character` component. It configures a Y-up capsule with
height, radius, mass, maximum pushing strength, maximum walkable slope in radians,
step height and floor-stick distance. It also owns current velocity, desired
horizontal velocity and an optional pending one-shot jump speed. Spatial position
is the capsule's feet in global double coordinates. Capsule dimensions are
physical meters; AuthoredTransform controls the visual mesh independently and
does not rescale or tilt the physical controller.

A character requires spatial state and excludes a RigidBox on the same entity.
Height must exceed twice the positive radius; mass is positive; strength, step
and stick distances are nonnegative; slope is between zero and pi/2. Motion is
finite, desired Y is exactly zero, and a present jump speed is positive. Both
snapshot validation and direct World setters enforce these rules. Clearing spatial
state clears the character. Derived revisions and preflight comparisons detect
live controller additions, removal, profile edits or external motion/pose edits
before advancing simulation; structural edits require reset.

AuthoredSimulation creates physical domains for regions containing bodies or
characters. Bodies are created first, then constraints and characters. It maps
persistent character identities to derived domain IDs and uses the existing VICE
character backend and WorldBridge. The solver steps at 120 Hz, then shared bridge
orchestration publishes global foot positions and motion back into World. Ready
bounded regions use existing automatic handoff. Contact state is refreshed after
reset and transfer. Unavailable destinations produce existing activation requests;
this does not add dynamic domain creation or a complete streaming policy.

## Gameplay contract

An append-only optional ABI1 read callback copies capsule settings, global feet,
actual and desired velocity, pending jump speed, ground classification, ground
normal/velocity and supporting body's persistent identity. An appended command
callback accepts desired horizontal velocity and an optional jump speed (zero
means no new jump). Grounded, steep-ground, unsupported and airborne states are
kept distinct. Derived domain IDs and backend types do not cross the SDK.

Commands require the owning writable lifecycle callback. Frame reads are allowed;
frame mutation, retained/off-thread calls, invalid records, missing entities,
missing characters, inactive regions and stale simulation state are explicit.
Older hosts report unavailable through size-checked capability discovery. A host
without the optional physical backend can still author/cook the data, but cannot
run the controller and reports capability unavailability.

A positive jump request requires walkable ground, no queued jump, and no upward
separation from the support. Otherwise `not_grounded` rejects the whole command,
including its horizontal change. Validation precedes native mutation. An accepted
command updates desired motion and pending jump in World immediately. Jump is
consumed once by the next fixed controller update; if support is lost before that
update, the backend does not buffer the request until landing. Gameplay may retry
movement with zero jump after a jump rejection. Coyote time, jump buffering,
acceleration, analog input, camera-relative movement and locomotion animation
remain gameplay policy.

## Persistence and diagnostics

Cooked World format 10 adds the optional character after Dependency, and continues
reading formats 1–9. Source format 1 gains the character record; typed set/clear
transactions retain full candidate validation, revision checks, dry-run, atomic
publication and physical impact reporting. Editor inspector/entity recreation
preserves the authored record.

Snapshots save current velocity, desired horizontal velocity and pending jump,
not contact caches. Reset rebuilds geometric contacts after restoring bodies and
motion. A queued grounded jump and an airborne trajectory survive save/reload.
Runtime debug reports authored/published character data and optional live ground
state through a borrowed physical service pointer valid during the observation
callback. That pointer is not stored by the player observer. A missing physical
service produces null live state, not fabricated grounding. World inspect exposes
configuration/motion without claiming live contacts. Preview's normal debug
snapshot includes authored capsules and region events, and rendered capsule meshes
use live preview World positions.

## Acceptance and remaining scope

Tests exercise direct authored bootstrapping at a global origin of 10^12 meters
and floor y=50, stair and slope policy, moving support, guarded SDK commands,
queued/airborne save-resume, ready-region transfer, stale additions/edits, preview
module parity/isolation and old format reads. A runnable SDK-only project combines
capsule movement with breakable links and supplied lights. Native acceptance
edits the profile, builds/cooks/stages the project and visual mesh, climbs a stair,
stops at a tall wall, jumps/lands, and observes local physical supply failure.

This is the existing solid-body capsule controller exposed through authored
applications. It does not add crouching/shape changes, character-peer collision
policy, animation, navigation, dynamic domain streaming, networking or body-joint
attachment to characters. Ground caches are reconstructed, so this is behavioral
resume within tested tolerances rather than a bit-identical solver checkpoint.

# Character carriers and gameplay cameras

## Decision

World gains an optional CharacterAttachment containing a persistent rigid-carrier
UUID and a carrier-local foot offset. An attached character keeps its identity,
capsule profile and published spatial/motion state, but has no independently
stepped native capsule. Shared AuthoredSimulation exposes guarded attach/detach
operations. Game code chooses vehicle seats, reach, permitted exit speed and exit
locations. The engine mechanism also works for other solid rigid carriers; it
contains no car ownership, driver, door or seat-selection policy.

ABI 1 also appends read_camera_pose/set_camera_pose. These operate on existing
nonphysical camera entities using double-precision global positions and a valid
unit quaternion. Authored lens settings and primary-camera selection remain the
existing World Camera contract. Following/orbit behavior belongs to the SDK game
module. Runtime and Editor preview consume the same resulting World camera pose.

## Authority and lifetime

Attachments require a spatial capsule and a distinct spatial solid rigid body.
The offset is finite and at most 100 meters from the carrier origin, independent
of authored visual scale. A body cannot simultaneously be a character, so this
relationship cannot form a carrier cycle. Multiple carried characters are allowed;
seat occupancy is game policy. Carrier destruction, collider/spatial removal and
conversion to a trigger are rejected while attachments reference it. Removing a
character clears its own attachment and releases the carrier reference.

The rotated local offset determines the attached foot position. Velocity is the
carrier's linear velocity plus angular velocity cross rotated offset. Desired
walking velocity and pending jump are cleared on attachment. Carrier pose/motion
publish first after solver stepping; attached World character pose/velocity then
follow. A direct SDK body-velocity command immediately republishes carried
velocity. Carrier region handoff moves the attached identity into the same region;
no independent capsule handoff is required. Detachment creates a walking capsule
in that current physical domain.

The anchor defines position, not a seated animation or character orientation.
The character's existing visual representation remains a game/content concern.
An authored attachment restores into this representation without briefly stepping
an independent capsule. World restore/authoring canonicalizes attached pose and
motion from the carrier. The source format can retain a character's explicit
spatial/profile fields, but the attachment determines its carried pose/velocity.

Opaque process-unique attachment generations invalidate additions, removals,
external attachment edits and World replacement. Existing character, material,
body-pose and shape guards remain. Attached reads also validate the carrier.
Commands and stepping reject stale state. Cached generations/native handles are
never persisted. Attachment work is O(1) per carried character per step; rare
transition preflight checks all authored bodies/controllers for stale state.
Carrier deletion checks scan users. Worlds without attachments do not allocate
a full entity lookup during attachment validation.

## Live transitions and exit safety

Attach checks valid IDs/offset/reach, current solver state, an active shared
physical region and distance from the actual character foot to the requested
anchor. Ordinary rejection happens before any mutation. It removes the native
walking controller through WorldBridge and publishes the attachment immediately.
Walking commands return attached until the character leaves its carrier.

Detach takes a carrier-local candidate foot offset, allowed carrier point speed
and maximum ground drop. All are finite and bounded (offset/speed <= 100 and drop
<= 5 meters). It checks current state, an active physical domain and carried
point speed; casts down from candidate foot + 5 cm to find walkable solid ground;
places the foot 1 cm above that contact; requires the destination to remain in
its current bounded region; tests the whole standing capsule against solid bodies;
and sweeps the capsule from its carried position to the candidate, excluding
only the carrier. An obstruction before the final 1 cm rejects the transition.
More than 5 mm of destination penetration is rejected. Triggers do not block it.
The query shape is temporary derived domain state and is always released.

This is conservative placement against authored rigid-body geometry. It does not
add virtual-character peer collision, navigation, door animation, arbitrary-region
exit selection or a general character teleport API. Ground/clearance failures and
excess speed leave World and the carried representation unchanged. If native
allocation/publication fails after successful preflight, a reset-required latch
prevents continuing from partial derived state; this is not a rollback guarantee
for resource exhaustion. No general deterministic replay claim is made.

## SDK and observability

HostApi extensions are append-only optional size-checked slots. Separate
CharacterAttachmentSnapshot and CameraPose DTOs preserve all previous record
layouts. New Result values append to the existing enum. Owning-thread and callback
phase checks apply; frame callbacks can query but cannot attach/detach/move a
camera. Older hosts return unavailable. Backend-disabled builds can author,
inspect, cook and save attachments, and read/move gameplay cameras; live physical
transitions return unavailable.

Camera input validates both records before publication and rejects camera entities
with a physical body/controller. Existing entity reads expose camera position and
quaternion, so the same World truth feeds rendering, debug and persistence. The
pose API does not add camera collision avoidance, smoothing, input capture or a
production camera rig. Runtime debug exposes carrier UUID/offset. Editor Inspector
shows the authored attachment; preview identifies carried characters and suppresses
an independent physical-capsule debug overlay for them.

## Persistence and authoring

Cooked format 14 appends a sparse attachment table after the format 13 vehicle
section: uint32 count, then owner UUID + carrier UUID + float32 offset xyz
(44 bytes per record). Count is bounded by entity count. Duplicate owners,
missing/non-solid/non-rigid carriers, non-character owners, invalid offsets and
truncated data fail before World replacement. Versions 1–13 remain readable
without inventing attachments. Camera poses use the existing transform/spatial
records. World checkpoints retain attachments, character/carrier identities,
vehicle motion, controls, distance and camera pose; module globals, held device
input and native caches remain outside checkpoint scope.

Source format 1 adds `character_attachment CARRIER_UUID offset_x offset_y offset_z`.
Revision-checked `set_character_attachment CHARACTER CARRIER x y z` and
`clear_character_attachment CHARACTER` use complete-candidate transactions,
dry-run, atomic publication and owner/carrier/region impact reporting. World
restoration installs attachments only after all entities/bodies exist. Editor
entity recreation likewise requires the referenced carrier to exist.

## Acceptance and alternatives

Tests cover typed authoring, source/binary round trips, version 13 loading,
reference deletion guards, bad restores, stale edits/carriers, entry reach,
blocked/high-speed/unsupported/inside-body exits, an intervening wall despite a
clear destination, successful alternate exit, immediate carried velocity updates,
region handoff plus exit into the destination domain, SDK size/thread/phase/old-
host guards, camera mutation, mounted checkpoint/restart and Editor parity/source
isolation. Full actor/vehicle/camera Runtime execution is additionally accepted by
the interaction-playground packaged workflow.

Moving a live capsule inside a car was rejected: its collision/grounding response
would disagree with its carried state. A second invisible character identity or
game-owned unsaved seated flag was rejected because it would lose continuity on
checkpoint/restart. A general arbitrary graph of physical attachments was deferred;
this bounded capsule-to-rigid-carrier relation has an explicit lifetime and solver
representation and can later inform a broader attachment system.

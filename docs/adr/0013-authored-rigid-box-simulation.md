# Authored rigid box simulation

## Decision

World owns an optional VICE-defined `RigidBox` record on spatial entities. It
contains motion mode, local collider half extents, linear/angular velocity,
friction, restitution, continuous-collision selection and trigger state. Pose
comes from the existing authoritative spatial/transform data. This keeps solver
handles and third-party types out of authoring, persistence and gameplay data.

The first shape is a box, with static, kinematic and dynamic motion. Kinematic
bodies use the existing moving collision layer and do not receive gravity.
Existing `include_dynamic` query filters include these kinematic movers.
This is a general primitive, not a vehicle or character convention.

Cooked World format 7 adds the optional record and continues reading versions 1
through 6. The authored source format adds a `rigid_box` entity record. Typed
transactions add `set_rigid_box` and `clear_rigid_box`, using the same validation
and publication service as other authored edits.

`RigidBodyWorld::AuthoredSimulation` derives domains and participant bindings
from persistent World records. Runtime and isolated Editor preview use this same
service. Domains are created only for regions containing authored bodies. Entity
IDs and Lab-specific asset names do not select physics behavior.

## State and execution

Half extents use meters before authored scale. Linear velocity uses meters per
second; angular velocity uses radians per second. Extents must be positive and
all numeric fields finite. Friction and restitution are nonnegative; restitution
above one remains available for deliberate gameplay amplification. Continuous
collision is initially limited to dynamic authored boxes.

Each fixed step publishes position, orientation and current velocities back to
World. Saving a running World therefore preserves meaningful moving-body state,
not solver handles. Resume rebuilds domains; it does not promise preservation of
solver contact caches, sleeping state or bit-identical replay.

The fixed interval is 1/120 second. Each advance processes at most eight steps,
clamps incoming wall time to 0.25 second and drops excess catch-up backlog. This
matches the existing preview budget and prevents an unbounded catch-up loop.
Inactive regions remain off until explicitly activated. Reduced fidelity retains
the current Rigid3D implementation; a distinct cheaper solver policy is future
work. This slice does not automatically promote boundary activation requests.

Physics-enabled regions require distinct simulation islands under the existing
WorldBridge contract. Handoff can target other instantiated physical regions;
streaming new physics regions is separate work. Structural edits or external pose
changes after reset require a reset/relaunch. Live runtime authoring is not added.

## Editor and capability boundaries

Authored projects use the shared service when physical records exist. Legacy Lab
preview remains an integration fixture for documents without such records.
Preview clones World; playing cannot rewrite the authored document or source.
Inspector state, body debugging and entity undo/redo preserve the new record.

Rigid3D remains optional at build time. A Runtime built without it rejects a World
requiring authored rigid boxes instead of silently ignoring physics. Empty and
nonphysical projects do not create physics jobs or domains.

## Alternatives and follow-on work

Editor-only physics initialization leaves standalone games with different rules.
Inferring collision from mesh names makes genre/demo conventions authoritative.
Storing Jolt body settings would make a backend own persistent project formats.

Additional shapes, authored constraints, material-resource integration, gameplay
module mutation access, scalable region residency and richer fidelity policies
should extend these shared contracts with explicit format migrations.

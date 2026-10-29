# ADR 0034 — Persistent swing/twist articulation

Status: accepted

## Decision and purpose

Add `PoweredSwingTwist` as a generic World primitive alongside `PoweredHinge`. Two body-local COM reference frames define a circular swing cone and independent twist limits. Targets are unit quaternions in constraint space, whose X axis is twist and whose Y axis is the frame normal. Games can command position, velocity, both, or passive behavior independently for swing and twist. Frequency, damping and independent torque caps are explicit. The swing cap applies separately to the two swing motor axes.

The solver remains a replaceable adapter. World owns UUIDs, frames, limits, target, velocities, caps and enablement; native constraints and warm-start impulses are disposable derived state. This enables shoulders, suspensions, robotic assemblies and other mechanisms without defining anatomy or genres.

## Invariants and rejected alternatives

One logical owner entity has at most one powered joint type. Endpoints must be distinct persistent authored rigid bodies in the same region, with at least one dynamic body. The owner cannot be an endpoint. Reference axes are orthonormal; anchors are metres in COM coordinates after authored scale. Targets outside the authored cone or twist limits are rejected before publication, rather than silently clamped by the backend. The swing half-angle is limited to 3.13 radians to avoid the 180-degree swing/twist decomposition singularity. A circular cone is deliberate; elliptical cones are deferred until a concrete authoring requirement justifies the larger contract.

A tagged extension of the hinge record was rejected: unused fields would make validation ambiguous and could accidentally reinterpret legacy hinge intent. Unconstrained ball sockets and direct transform teleportation cannot provide bounded orientation actuation. Neither animation nor game modules receive native solver objects.

Connected-pair collision exclusions use the existing reference-counted body identity mechanism. Other constraints sharing the pair retain their exclusion until the last reference is removed. Environmental collisions remain enabled.

## Persistence and failure semantics

World snapshot v24 adds a bounded sparse owner-UUID/record table after the v23 hinge table and before the clock. Empty cost is four bytes, with no new per-entity snapshot payload. Readers retain versions 1–23. Records use a version-1, locale-independent textual representation with round-trip float precision, deterministic UUID order and bounded decoder allocation. World source uses `swing_twist_joint`; typed revision-safe edits use `set_swing_twist_joint` and `clear_swing_twist_joint`.

World restore validates all records before swapping state. Missing owners, missing bodies, duplicates, conflicting joint types, bad frames, unsafe limits and invalid targets fail atomically. Endpoint destruction, body removal, motion changes and cross-region handoff are guarded by the shared endpoint index. Region handoff for a connected assembly currently requires explicit teardown/rebuild; there is no hidden relocation of joints.

Runtime checkpoints and Editor Play clones carry the same table and physical body state. A cold reset reconstructs the original reference frames at the saved body poses; it does not redefine zero at load. Solver caches are not serialized, so bit-identical cold continuation is not promised. Legacy entity-create undo refuses to discard newly attached sparse joint state; typed source transactions remain the edit path.

Drive/enable edits are applied at the next fixed step. Structural changes increment the shared powered-joint generation and require reset. Reads and controls reject stale topology or externally edited body state. Invalid commands do not mutate World. Failed native publication latches reset-required before another step.

## Inspection and module boundary

Runtime inspection reports the full persistent record, actual relative quaternion, shortest angular error in radians, signed motor impulses for twist/swing Y/swing Z, signed limit impulses, linear constraint impulse and active status. Impulses are last-step solver observations, not persistent control intent. Editor Inspector and source CLI expose the authored record.

HostApi ABI 1 appends size-checked `read_swing_twist_joint` and `control_swing_twist_joint` callbacks with fixed-layout copied values. Existing hinge layouts remain intact. Writes use the established writable owner-thread lifecycle guard; frame and background calls are rejected. Missing owner, missing joint, inactive region and stale binding remain distinct results.

## Fidelity and performance

State is sparse. World lookup and drive changes are expected O(1); endpoint guards inspect only incident joints. Combined powered-joint capacity is 4096. Reset and each fixed step visit bound joints O(J), and canonical serialization sorts records O(J log J). The sparse record stores two UUIDs, two COM frames, limits and drive; endpoint indexes add two owner handles per joint plus container overhead. No process-local handles enter persistent truth.

Full and reduced regions currently use the same fixed physics cadence; reduced articulation is not yet cheaper. Inactive regions retain World state and stop native integration. Approximate distant articulated simulation and safe fidelity transitions remain a later gate. This first slice does not claim biped balance, support planning, stepping, recovery, animation blending or an Euphoria-equivalent character controller.

Revisit this decision when measured needs require elliptical limits, articulated islands across regions, aggregate swing torque budgets, full-body coordination or cheaper distant simulation. Those extensions must preserve reference-frame and persistence meaning.

# 0033: Persistent powered hinges and stable joint reference frames

Status: Implemented first articulation slice, October 29, 2026.

## Decision and scope

Add a generic World-owned powered hinge between two persistent rigid-body entity IDs. This is a one-axis actuator primitive for mechanisms and articulated bodies. It does not define a character, anatomy, behavior, balance controller or genre. Coupled hinges can share bodies; joints are logical entities with persistent identity. Multi-axis anatomical joints, physical rig coupling, balance, stepping and fall recovery remain subsequent slices.

Use the existing Jolt adapter and torque-limited hinge motors. Add optional body-local COM frames to the native descriptor while preserving the existing world-frame default for old callers. Local anchors, hinge axes and reference normals preserve the angular zero when current body poses are reconstructed. Recomputing both reference frames from the saved world pose would incorrectly turn that pose into zero.

## World truth, validation and lifetime

World owns endpoints, two orthonormal local frames, angular limits, drive mode, target angle/angular velocity, spring frequency/damping, torque cap and enabled flag. Units are metres, radians, seconds and Nm. Anchors are in the body's post-scale COM frame; they are not multiplied by authored mesh scale a second time. Existing authored boxes have their COM at the box origin. Future compound shapes require explicit COM conversion.

Frame axes must be finite, unit and perpendicular. Limits contain zero within [-pi, pi]. Drive values are finite; frequency is 0–120 Hz and damping 0–10, with positive frequency for position drive and positive torque for active drive modes. Target angle stays inside the limits. Endpoints exist, are spatial rigid boxes, share a region and include at least one dynamic body. The joint owner cannot be its own endpoint. At most 4096 authored joints exist in a World.

Endpoint reference indexes reject deletion, removal of physics/spatial state, static/static conversion and splitting a joint across regions. Explicit joint removal permits subsequent endpoint removal. Disabled joints retain their topology. Configuration/restore changes advance a process-local generation; the simulation rejects stale/additional/removed joints before stepping. Drive/enabled edits are permitted through controlled APIs at a fixed-step boundary; they do not recreate constraints. Runtime handles and generation counters are never persistent truth.

Directly connected World joint bodies do not collide with one another. Other pairs retain normal collision behavior. The native hinge descriptor makes this optional and preserves old callers' collision behavior. A per-domain pair exclusion table is reference counted by live constraints, read only during solver callbacks, and contact caches are invalidated on insertion/removal. Destroying one of multiple constraints cannot prematurely restore collision. Exclusions remain while a constraint is disabled and disappear on destruction. No second gameplay database is created.

## Persistence and authoring

World snapshot v23 adds a bounded table of owner UUID plus versioned structured joint text before the unchanged World-clock tail. Empty Worlds gain four bytes. Readers retain v1–22 compatibility. Version-1 joint records use deterministic ordered scalar fields and round-trip float precision. Duplicate owners, missing endpoints, bad frames/limits/drives, unsafe counts and truncation reject the replacement. World restore constructs and validates a replacement before committing. The source token is `powered_hinge`; typed revision-safe set/clear edits report affected endpoint IDs and regions.

Runtime checkpoints, normal save/load and Editor Play cloning use the same World snapshot. Body pose and velocities plus persistent local frames reconstruct the current joint angle; drives are restored as intent. Solver warm-start impulses are disposable: resumed trajectories are not promised to be bit-identical to an uninterrupted warm solver. A fresh pose must remain coherent without silently rebasing its angular target. Failed reset preserves the previously initialized simulation.

## Fidelity, cost and inspection

Full/reduced/off use the existing physical-domain cadence and sleeping strategy; joint drive application skips unchanged intent. Off regions retain World state and do not advance physics. They reject live module drive commands. Whole-articulation region migration is not implemented: current endpoint guards keep a connected assembly in one region until topology is explicitly removed.

Endpoint lookup uses UUID hash indexes. Native reconstruction and per-step joint intent checks are O(joints), bounded to 4096 World joints; this is not a thousand-character performance guarantee. Decoder ownership lookup is indexed rather than a joint-by-entity scan. Authoritative joint records are stored sparsely, so ordinary entity slots do not gain an unused joint payload. Memory includes two frames, drive intent, endpoint index entries, derived native constraints and bounded pair exclusions. SnapshotEntity retains a value-based optional record and therefore incurs temporary per-entity snapshot storage; this should be revisited with the existing broader snapshot projection costs when profiling large scenes. Worlds without joints skip endpoint-index construction during validation. Population scaling needs active-assembly indexes, measured solver budgets and fidelity policies above this primitive.

Runtime inspection includes the persistent record, target, cap, actual angle, signed target error, completed-step motor/limit/linear impulses and active state. Impulses are Ns/Nms, not forces/torques; compare the motor impulse to torque multiplied by the actual solver timestep. Inactive observations retain last completed-step evidence. The Editor Inspector displays joint intent and both endpoint identities. Append-only, size-checked game-module DTOs allow state reads and controlled drive updates with the existing owner-thread/lifecycle guards. Old hosts return unavailable; frame callbacks can read but cannot drive.

## Alternatives and next gates

Rejected: saved native constraint handles, world-frame-only reconstruction, a separate skeletal physics scene graph, unbounded perfect-position actuation, and treating a pose-holding hinge as a balance controller. A gravity-loaded spring drive has finite compliance and may sag; weak actuators must fail to hold loads they cannot support.

The first correctness gate measures actual pose error, torque saturation, a controlled disturbance and recovery, cold checkpoint reconstruction, coupled joints, weak/off drives, topology invalidation and Editor clone/reset isolation. A primitive-geometry lab exposes the same behavior in standalone Runtime. Next extend the joint model with swing/twist or other bounded multi-axis articulation, then couple physical bodies to the visual rig. Support sensing, balance, contact-aware motion and recovery require their own measured controllers. Superseding Euphoria remains an empirical product target, not a claim established by this slice.

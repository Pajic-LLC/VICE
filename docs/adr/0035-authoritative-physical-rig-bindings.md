# ADR 0035 — Authoritative physical rig bindings

Status: accepted

## Decision

Store sparse, body-owned `PhysicalRigBinding` values in World. A binding names an animation root UUID and rig node, with a rigid COM-to-node translation and quaternion. A body drives at most one node; one physical source may drive each node. Animated descendants can represent additional visual detail. Physical roots and bodies share a region. Root scale is unit; collider scale is deliberately ignored. Offset translations are COM-local metres, not scaled collider coordinates.

The shared `sample_world_pose` evaluator samples animation, derives its local poses, overrides physical nodes with current body poses in root space, and recomposes unbound descendants beneath their new parents. Animation supplies scale and unbound local motion. Physical nodes have complete position/orientation authority; this slice does not implement partial-weight animation/physics blending. Presentation never writes a pose back into a source body.

Both rigid visual rig parts and Runtime/Editor weighted skinning use this evaluator. Pausing or stopping a clip does not freeze physical deformation. A moving root is inverted before computing rig-space results. Global positions are subtracted in double precision before conversion to float; local deltas above one million metres fail explicitly. Outputs remain unchanged on failed evaluation.

## Why and alternatives

A renderer-only bone map would hide gameplay truth from persistence and inspection. Native solver handles cannot survive reload or backend replacement. Adding offsets to every existing RigPart or entity would charge ordinary entities for unused physical animation. A second skeletal scene graph would create competing transform authority. Those alternatives are rejected.

A sparse World table provides explicit identity, endpoint guards and indexed root queries while preserving ordinary entity layout. This is a binding between existing World bodies and presentation asset nodes, not a second simulation. Native physics remains behind VICE interfaces. No anatomy, character role or game meaning enters core contracts.

## Validation and persistence

World enforces valid body/root UUIDs, a spatial rigid body, a spatial animated root, same-region ownership, unique body/node bindings, finite offsets within 10 km, unit quaternions and unit root scale. Asset-aware reset additionally checks node availability and evaluable physical poses before replacing the current presentation service. Invalid restore is transactional.

World snapshot v25 appends a bounded, deterministic owner-UUID/record table before the clock; previous versions 1–24 remain readable. Each record has its own version 1. Text uses the classic locale and round-trip float precision. Empty cost is four bytes. Derived palettes, vertices, native handles and warm-start caches are not serialized.

Snapshot, Runtime checkpoint, save/load and Editor Play clone/reset carry identical bindings, body state and animation intent. Cold reconstruction rebuilds derived poses and geometry. Root removal, animation removal, root scaling, body-state removal and cross-region handoff are guarded. Body destruction removes its owned binding. Root/self bindings require explicit binding removal before root destruction. Legacy create undo refuses to discard a subsequently attached physical binding. Migration of a connected physical rig across regions requires explicit teardown/rebuild for now.

World source uses `physical_rig RECORD`; revision-safe edits use `set_physical_rig BODY RECORD` and `clear_physical_rig BODY`. Topology edits advance the existing presentation generation and require service reset. Body motion does not change that generation; the skin cache must observe it independently of animation time.

## Inspection and module boundary

Body inspection exposes its binding; root Runtime inspection lists physical source body UUIDs, nodes and offsets. Existing physics and skin-render observations show body poses, joint decisions and uploaded weighted geometry. The Editor Inspector reports source root/node and root source count. The append-only ABI-1 `read_physical_rig` operation returns a copied, size-checked value and uses existing owner-thread/lifecycle guards. Existing DTO layouts remain unchanged. Structural mutation uses authored World/edit contracts; live module topology construction is deferred.

## Fidelity and costs

Body lookup and root presence checks are expected O(1). Mutation checks only a root's bindings, bounded by 128 distinct asset nodes. Root iteration uses a sparse index and returns deterministic node order. World capacity is 4096 bindings. Pose evaluation is O(N + B log B) for N nodes and B physical bindings, independent of total entity count; vertices deform only when the resulting palette changes. Ordinary entities and SnapshotEntity do not gain a binding payload. On ARM64 the binding is 48 bytes, owned snapshot record 64 bytes, excluding hash/index overhead.

Full and reduced physical regions still use the existing equal solver cadence. Reduced animation is sampled at 15 Hz, while physical poses remain current even with a paused clock. Inactive physics retains body state without integration; unchanged palettes reuse geometry. This is not yet a cheaper articulated simulation or a crowd-performance guarantee. Existing skinning instance/vertex budgets still apply. The current CPU reference path measured about 0.44 ms for one 3,762-vertex instance and 14.43 ms for 32 changing instances on an M2 Pro, excluding physics and GPU upload; unchanged palettes deform zero vertices. This measured bottleneck requires GPU skinning and/or stronger fidelity budgets before large character populations.

## Boundaries and change triggers

The suspended mannequin lab validates eleven bodies, ten swing/twist joints, eleven bone sources and 3,762 uploaded weighted vertices with animation paused, including cold checkpoint reconstruction. A fixed pelvis isolates this bridge from balance. The visible lab uses near-origin coordinates because the existing legacy absolute-float raster path loses geometry precision at 1e12; the physical binding evaluator itself is tested there. Full far-coordinate raster correction remains separate work. It does not demonstrate walking, stepping, contact-aware recovery or Euphoria-level control.

Revisit the design for weighted blending, multiple bone outputs per body, non-unit root scale, cross-region assemblies, render interpolation, shared per-root pose caches or measured population budgets. Each extension must preserve explicit physical authority and cold-save meaning. Support-aware whole-body balance is a subsequent controller layer, not a renderer feature.

# Physical bodies driving visual rigs

`PhysicalRigBinding` binds a World rigid body's COM frame to one node of a game-selected rig. The binding stores a root UUID, node index, translation and x/y/z/w quaternion. It is sparse persistent World state. Body position and orientation remain authoritative; animation provides unbound motion and visual scale.

Use `World::set_physical_rig_binding`, `physical_rig_binding`, `physical_rig_bindings(root)` and `clear_physical_rig_binding`. Root queries use an index, not an entity scan. World source appends `physical_rig 1 ROOT NODE tx ty tz qx qy qz qw` to the body. Typed edits use `set_physical_rig BODY RECORD` and `clear_physical_rig BODY`. Root scale must be one; offsets are metres in body-local COM coordinates, independent of collider scale. Each body drives one node and each node has one body source. The root must be animated and share the body's region.

`presentation::sample_world_pose` combines sampled animation with authoritative physical poses. It preserves local animation of unbound descendants, so a physically driven upper link can carry animated detail below it. It is shared by visual rig-part updates and Runtime/Editor skinning. Paused clips still react to physics. Palettes are compared before deforming vertices, so unchanged bodies reuse geometry and settle motion history.

World v25 persists bindings through normal saves, Runtime checkpoints and Editor Play clone/reset; readers retain older versions. Asset-aware service reset validates node indices and poses. Structural edits require reset, while body movement is observed each render evaluation. Single-endpoint region handoff and root rescaling are rejected while bindings exist.

Inspect a source body for `physical_rig`; inspect the root in Runtime for `physical_rig_sources`, physics state and `skin_render`. The SDK's size-checked `read_physical_rig` returns copied root/node/offset data. The Editor Inspector shows binding identity and source counts.

Build `examples/physical-rig-playground/physical-rig.viceproj`. Animation is paused; eleven rigid bodies drive the weighted mannequin. Space applies angular disturbances and R switches motors off/on. The pelvis is fixed to separate articulation/skinning from balance. Run `scripts/validate_physical_rig_workflow.py --vice PATH --run-runtime` for transactional authoring, staging, GPU upload and fresh-process checkpoint checks. [ADR 0035](../adr/0035-authoritative-physical-rig-bindings.md) records costs, invariants and current limits.

The lab is placed near the render origin. The new World-to-rig evaluator and physical tests cover `1e12` coordinates; the existing legacy absolute-float raster path still loses visible geometry precision at that distance. That renderer path needs a separate camera-relative correction. GPU upload inspection alone does not prove visible large-coordinate rendering.

# Character and structural capability candidate

This integration builds on the headless playback, lit fallback, multi-axis joint and physical-rig candidates. It does not replace their existing World ownership or physics interfaces.

## Animation intent

`AnimationPlayer` now stores a secondary clip, its clock and a blend weight. Snapshot format 26 and source `animation_mix` preserve these values; older snapshots default to an unmixed player. The two clips are sampled into local TRS poses, blended with shortest-arc quaternion interpolation, then composed through the hierarchy. Physical rig bindings still override the resulting intent. CPU skinning caches include both clocks, clips and weight.

The append-only module host callbacks `control_animation_mix` and `read_rig_node` retain ABI version 1 and guard host size, thread, phase, IDs, ranges and output layout. The query can sample animation intent or the physically overridden pose and returns a world-space node transform. It enables game-owned actuator targets without maintaining another pose database. It does not itself supply balance, terrain IK or a locomotion planner.

## Structural failure

`PoweredSwingTwist` source record version 2 adds an optional linear impulse threshold and persistent broken/impulse/solver-step state. The reader retains version 1. The native solver's existing constraint break event publishes the failure into World, disables the connection and records a `constraint_broken` causal fact. Cold reconstruction leaves the connection disabled. No collision name, distance trigger or game-specific damage counter causes the break.

A dependency gate may reference a distance link or a swing/twist connection. Its derived failure state carries the same measured impulse and solver step. An attached dependent prevents removing the connection identity. This supports circuits and other generic supplies through physical connections; it is not a deformable material or plastic strain model. Thresholds remain authored effective connection properties rather than automatic derivation from a full finite-element material model.

## Surface and package integration

`vice_surface_v3.hlsl` combines normal/ORM/emissive material inputs, the existing environment/probe path, direct GGX lighting, directional cascaded shadows and atmospheric aerial perspective. Vertex normals use an inverse-transpose basis under nonuniform scale. Stock material contracts also accept the runtime's optional AO bridge binding. Metal Shader Converter's `Sampler` resource spelling is normalized alongside `SMP`. Converted inline ray queries now bind the GPU address of a converter acceleration-structure header, containing the native resource ID and instance-contribution pointer. Binding the resource ID directly produced missed hits in the existing inline-query and reflection execution tests; those tests cover the corrected path. The header and contribution buffer share the TLAS lifetime and are explicitly resident during dispatch. Native MSL acceleration-structure binding is unchanged.

Native game packages copy cooked renderer kernels under their project content's `runtime-shaders` directory. Render-thread pass initialization selects these packaged kernels before development build paths. This avoids requiring a packaged executable to open the developer's checkout and preserves the development fallback for tests without a packaged kernel directory.

## Required validation

Check animation local-space length preservation, invalid-input transactionality, clock/checkpoint round trips, old-host and frame/thread guards, solver-driven structural break, dependency propagation and cold wreck reconstruction. Run the existing engine suite and the game's actual walk/drive/contact/save/restart scenario. Native screenshots and measured frame telemetry must be evaluated separately from headless acceptance.

## Equipment/controller collision relationships

A symmetric World relation with kind `e8000000-0000-4000-8000-000000000001` excludes a rigid body from one character controller's collision queries. Other bodies and characters still collide with it. The authored bridge derives native participant exclusions from persistent identities at reset and rejects live structural edits before solver advance. Reconstructed controllers and exit overlap/sweep preflights preserve the exclusions. This prevents a moving piece of physical equipment from feeding back into its wearer's coarse capsule without disabling its road contacts. The regression checks an overlapping equipment body, grounded stability, cold reconstruction and stale-filter rejection.

## Camera depth agreement

Direct project materials now create a camera-depth pipeline using their exact cooked vertex module. A separate GPU batch assignment omits those instances from the generic native depth kernel; GPU-scene materials and fallback meshes keep their batched path. This prevents cross-compiler rounding or legal project vertex transforms from rejecting fragments against a different prepass. The GPU regression compares the full color target with and without a prepass, including an intentional vertex-depth offset that the old generic prepass could not reproduce.

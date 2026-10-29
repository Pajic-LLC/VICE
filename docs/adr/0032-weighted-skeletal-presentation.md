# 0032: Weighted skeletal presentation from persistent intent

Status: Implemented foundation, October 29, 2026.

## Decision and invariants

Extend the existing rig asset and presentation service with bounded linear blend skinning and cubic animation curves. World continues to own only persistent animation intent and root/part/resource identities. Bind meshes, weights, inverse-bind matrices and clips are immutable assets. Palettes, posed vertices, GPU streams, bounds and runtime mesh aliases are disposable derived state. No new World format, game module ABI, gameplay scene graph or third-party animation runtime is introduced.

The initial renderer uses the shared CPU reference deformation implementation. This gives Editor and Runtime one correctness baseline and lets existing rendering, depth, shadows, motion and ray geometry consume identical posed vertices. It is a correctness gate, not a production crowd throughput claim. A GPU implementation should replace this evaluation/upload stage once profiled, preserving the asset and World contracts and comparing against the reference implementation.

## Representation and validation

Rig text version 2 adds typed skin bindings, up to eight influences per vertex, joint node references, affine nonsingular inverse-bind matrices and cubic key tangents. Rigid LINEAR/STEP rigs continue to serialize identically as version 1. Readers accept both versions and reject unsupported versions, truncation and invalid references before publication. Existing parent-before-child, positive uniform TRS and file/key budgets remain. The embedded glTF/GLB adapter accepts JOINTS_0/1 and WEIGHTS_0/1, normalized unsigned weights or floating weights, and CUBICSPLINE. It rejects excess influences, invalid/duplicate joints, invalid matrices and unsupported input explicitly, without publishing partial output. See the [glTF 2.0 specification](https://registry.khronos.org/glTF/specs/2.0/glTF-2.0.html) for source conventions.

Joint palettes are global joint poses multiplied by inverse-bind matrices. Skin output is in rig space; the mesh node transform does not get applied a second time. The persistent World root supplies the final wrapper transform. Normals use the inverse transpose of the blended linear transform, with bounded fallback to joint normals when a blend becomes singular. Cubic Hermite evaluation scales tangents by the key interval and normalizes quaternion output. Nonuniform node scale, node matrices, morph targets and textured/translucent material import remain outside this importer profile.

## Persistence, fidelity and failure

Save/checkpoint/Editor Play clone preserve existing AnimationPlayer and RigPart state. Restoring reconstructs geometry from immutable assets and saved clip clocks. A World presentation revision invalidates previous-frame deformation, preventing restored geometry from inheriting old motion. Derived alias IDs never enter snapshots or authored sources. Per-part caches permit independent animation of shared assets and are evicted when parts stop rendering.

Full regions evaluate the requested pose. Reduced regions sample at 15 Hz; unchanged joint palettes reuse geometry without a vertex pass. Inactive presentation follows existing World residency policy. Cache limits are explicit: 256 parts, 500,000 retained vertices and 200,000 deformed vertices per frame; violations report failure. These are safety bounds, not measured frame-rate guarantees. Rigs allow 128 joints per binding, 128 bindings and 100,000 aggregate weighted vertices, within the existing 16 MiB rig limit.

Current and previous streams use persistent GPU allocations. Current vertices update when their fingerprint changes; the previous-position buffer is refreshed from cached history each rendered frame. Motion consumes previous local positions and previous model transforms. Deformed bounds drive culling and Editor picking. Bind-pose meshlets/LODs are disabled for derived meshes; ray BLAS keys follow the deformed fingerprint. Previous streams are included in residency accounting. The existing renderer waits for submission completion before buffer reuse; a future asynchronous renderer must preserve this lifetime contract.

## Complexity and inspection

Pose sampling remains bounded by rig tracks/nodes. Palette construction is linear in joints. Deformation is O(vertices × influences), with at most eight influences and no per-vertex heap allocation. Cached palette comparison is O(joints), stream update O(vertices), and bounds calculation O(vertices). Rig/entity cache lookup uses ordered maps with O(log n) lookup; resource-alias lookup scans at most 256 entries. Admission/eviction currently compares cached entities against render instances, O(cached parts × visible instances); the finite alias pool is reused after eviction. Population scaling should index that membership test. Per-vertex storage includes 48 bytes of source weights, 24-byte reference position/normal, immutable mesh attributes and current/previous render vertices; exact render buffer residency is accounted by the existing residency service. Overall allocator overhead and GPU alignment are implementation dependent.

Runtime inspection exposes skin/joint/weighted-vertex counts, evaluated vertices, sample times, reduced cadence, persistent source identity, transient alias identity, current/previous fingerprints and GPU upload confirmation. Tests cover mathematical deformation, eight influences, cubic sampling, old/new serialization, bad inputs, independent instances, large coordinates, checkpoint/restore, reduced fidelity, cache eviction and Editor Play reset. The standalone skinning playground adds deterministic import and actual native walk/ride/save/restart evidence using procedural validation art.

## Alternatives and next gates

Rejected: serializing posed vertices or GPU handles, moving bones into a second gameplay database, importing a genre-specific character framework, silently dropping influence sets, and rendering only rigid pieces as a skinning substitute. A complete animation framework is deferred: GPU skinning, clip blending/state transitions, retargeting, root motion, IK, interaction alignment, animation LOD and production character assets need focused subsequent slices. Revisit CPU evaluation once representative character counts establish its budget, and revisit uniform TRS only with a complete shear/normal/persistence design.

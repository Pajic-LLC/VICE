# 0024: Imported rigid-node animation and spatial audio

Status: Implemented as a bounded first-character gate, October 28, 2026.

Historical scope: [ADR 0032](0032-weighted-skeletal-presentation.md) subsequently adds weighted skins and cubic curves. The restrictions below describe this original slice.

CRASHOUT's playable walk/drive/save loop needs a visible animated character and spatial sound before broader game content can be authored. A complete skinning renderer and production audio engine exceed this gate. We import glTF 2.0 rigid nodes and mono WAV through narrow adapters and keep authoritative presentation intent in World.

## Ownership and data

`AnimationPlayer` holds rig UUID, clip, time, rate and play/loop flags. `RigPart` links an ordinary nonphysical spatial/mesh entity to a root UUID and node index. `AudioEmitter` holds sound UUID, time, gain, pitch, attenuation distances and play/loop flags. Resource deletion and root deletion are guarded. Pure visual parts cannot independently become physical. Root/node scale must be uniform, avoiding unrepresented shear.

World v15 appends a sparse presentation table after the unchanged v14 attachment table. Each record contains an entity UUID and bounded component text encoded by the same strict field reader as source authoring. Empty Worlds add four bytes. Duplicate/unknown entity records, malformed values and invalid references fail before restore. Versions 1–14 remain readable. Source serialization, typed revision-safe edits, CLI inspection, undo/recreation and Inspector preserve the fields.

`.vrig` and cooked `.vrigc` use version 1 text: parent-before-child uniform TRS nodes and named clips with LINEAR/STEP translation, quaternion rotation and uniform scale tracks. Rotation uses normalized shortest-arc slerp. `.vsound` resolves a quoted relative WAV path within the asset root; mono 48 kHz PCM16 and float32 WAV are converted to `VICEPCM1` assets containing UUID/rate/count and finite little-endian float samples. Cook/build and preview reject missing rigs/clips, invalid node/mesh bindings and absent sounds before launch; packages include the cooked files and notices.

## Runtime and Editor

The same presentation service loads cooked assets, evaluates clocks and poses after physics, and publishes normal World transforms. Full/reduced regions currently use the same pose cadence; inactive regions freeze clocks and mute emitters. World structural changes invalidate cached services and require reset. Scalar SDK controls preserve identities. A checkpoint stores clip/audio clocks; a fresh service evaluates them without resetting saved time. Asset edits are not hot reloaded.

ABI 1 adds size-checked DTOs/callbacks after existing callbacks: read/control animation, read/control audio and character visual facing. Old DTOs retain their layout. Writes are rejected in frame callbacks and from non-owner threads. No foreign library types cross SDK or World boundaries.

The stereo mixer interpolates mono samples, applies pitch, equal-power pan relative to the primary camera right vector, distance attenuation and cutoff, then clamps the sum. Native miniaudio output uses a bounded SPSC float ring with no callback allocations or locks. Runtime starts output after its first rendered frame to avoid initial shader-compilation starvation. Editor output follows play/focus and stops on pause. Device availability does not affect World clocks/offline mixing. Delivered and underrun frame counters expose device behavior; long rendering stalls still produce silence rather than block the callback.

## Bounds and dependency isolation

A rig has at most 128 nodes, 64 clips, 384 tracks per clip, 4096 keys per track and 262144 total keys. Each asset is at most 16 MiB. A service accepts at most 256 animated roots, 4096 parts, 128 emitters, and 512 presentation assets totaling 64 MiB on disk. These bounds prevent unbounded input; they are not measured crowd performance targets. Pose evaluation currently scans parts per root and allocates per-frame work buffers. Population scaling needs separate profiling and indexing work.

The importer pins cgltf v1.15; its allocator is bounded to 64 MiB. Only embedded glTF/GLB buffers are accepted (32 MiB total). Normals are required. Weighted skins, morphs, matrices, nonuniform scales, CUBICSPLINE, compressed meshes and textured/translucent base materials fail explicitly. miniaudio 0.11.23 is pinned behind an adapter and can be excluded with `VICE_ENABLE_AUDIO_OUTPUT=OFF`; decoding remains our cooker. Dependency notices ship with packages.

## Evidence and remaining gates

Tests cover real import determinism, invalid inputs/no partial import publication, quaternion sampling, actual stereo PCM pan/falloff/silence, World source/typed edit/cooked round trips, v14 compatibility, structural invalidation, SDK phase/thread/size checks, shared Editor evaluation and saved continuation. A native workflow exercises walk/ride/idle, spatial engine output, mounted checkpoint/restart and exit without changing source assets. This gate does not imply production engine readiness: weighted skinning/blending, asset browser import UI, full audio streaming/occlusion, combat, AI/navigation, large-map streaming and a CRASHOUT-specific vertical slice remain separate work.

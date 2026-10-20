# Compute execution foundation

## Decision

VICE uses one backend-neutral compute execution model for Metal and Direct3D 12.

Compute is an engine capability, not a rendering feature by itself. GPU visibility, indirect-draw preparation, hierarchical depth work, image processing, global illumination, denoising and future reconstruction systems can build on the same contract without exposing Metal or Direct3D 12 types to World, project or game code.

The first contract is intentionally small. A compute pipeline owns one compute shader and a dispatch is expressed as three non-zero thread-group counts.

## Pipeline identity

`ComputePipelineDesc` references exactly one `ShaderModule` whose stage is `ShaderStage::compute`.

Vertex or fragment shader modules are invalid compute pipelines. Backend implementations must reject invalid descriptors before native pipeline creation.

Compute shaders continue to use the existing VICE resource reflection model:

- constant buffers use `constant_buffer`
- read-only buffers and textures use `shader_resource`
- writable buffers and textures use `unordered_access`
- samplers use `sampler`

This keeps project shader cooking and backend resource binding on one namespace model.

## Dispatch dimensions

`ComputeDispatchDesc` contains X, Y and Z thread-group counts.

All three counts must be greater than zero. VICE rejects a dispatch with any zero dimension instead of relying on backend-specific behavior.

Thread-group size remains shader-authored. `ComputeThreadGroupSize` belongs to the compute pipeline and records the shader's threads-per-group dimensions. For cooked HLSL, the shader cooker reflects DXC `NumThreads=(x,y,z)` metadata from the authored `[numthreads(x,y,z)]` declaration, writes it into the cooked shader manifest, and the loader exposes the same value to pipeline creation.

The dispatch contract describes only the number of groups submitted by the engine. Backends must not infer authored group dimensions from hardware properties such as Metal `threadExecutionWidth`. Direct3D 12 validates the authored dimensions against shader-model limits; Metal validates the total thread count against the created compute pipeline's supported maximum.

## Command state

Compute execution must stay explicit.

A command list must not dispatch while a graphics render pass is active. Backend implementations should close or reject incompatible encoder state rather than silently switching command domains.

Graphics and compute pipelines have separate binding intent even when the native backend can reuse descriptor storage. Setting graphics state must not implicitly configure compute state, and setting compute state must not mutate the active graphics contract.

## Resource transitions

VICE owns resource-state correctness.

Read-only compute resources transition to the backend state required for shader reads. Writable storage resources transition to unordered-access or shader-write state. A later graphics, copy or compute use must transition through the normal VICE resource tracking path.

The public API does not expose native barriers, resource states, descriptor heaps, argument buffers or command encoders.

## Tier 1 backends

Metal and Direct3D 12 remain equal Tier 1 implementations.

The Metal path should use a compute pipeline state and compute command encoder behind the VICE interface. The Direct3D 12 path should use a compute pipeline state, the existing VICE-owned root-signature/resource-layout machinery, and `Dispatch` behind the same interface.

Backend differences such as Metal thread execution width or Direct3D 12 descriptor-table layout remain implementation details. They may constrain a pipeline, but they do not redefine shader-authored execution semantics.

## First validation target

The first native smoke path should:

1. create a small GPU-only storage buffer
2. dispatch a deterministic compute shader that writes known values
3. copy the result to a readback buffer
4. wait for submission completion
5. verify the expected values on the CPU

That test proves pipeline creation, resource binding, dispatch, synchronization, storage writes and readback without coupling the milestone to a larger renderer feature.

Both Tier 1 primitive paths should prove this behavior independently before the same scenario is promoted to a public-RHI regression. This keeps backend failures isolated while the public adapter is introduced.

## Stage C handoff

Once native compute execution is green on both Tier 1 backends, #159 Stage C can build the first GPU-driven scene slice on top of it.

The intended sequence is:

1. GPU frustum visibility for render instances
2. compact visible draw data
3. indirect indexed drawing
4. hierarchical depth and occlusion
5. meshlet or cluster representation
6. residency and large-world streaming integration

The compute foundation does not define those policies. It only provides the portable execution path they require.

## Current boundaries

This milestone does not define:

- indirect draw argument layout
- mesh shaders
- GPU scene data ownership
- visibility algorithms
- hierarchical Z representation
- ray tracing
- global illumination algorithms
- async compute queues
- queue-family ownership or explicit public barriers
- neural reconstruction

Those features can extend the compute foundation without changing its basic pipeline and dispatch semantics.

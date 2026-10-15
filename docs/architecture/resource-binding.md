# Resource binding

VICE separates logical shader bindings from backend runtime locations.

## Logical identity

A shader resource is identified by:

- register class
- binding index
- register space
- descriptor count or unbounded state

The register classes map to HLSL registers:

- constant buffer: `b`
- shader resource: `t`
- unordered access: `u`
- sampler: `s`

The shader cooker gets this information from DXC reflection. Cooked shader modules copy and own the logical binding metadata when the binary is loaded. Game code does not assign Direct3D 12 descriptor handles or Metal argument-buffer offsets.

A draw-time resource uses `ShaderBindingKey`, which adds an array element to the logical register identity. Array elements are therefore addressed without exposing a native descriptor index.

Development source shaders currently have an empty reflected binding list. This keeps runtime source compilation available as a fallback without inventing reflection outside the production cooker.

## Pipeline layout

`compile_graphics_pipeline_layout` combines reflected vertex and fragment resources into one deterministic layout.

Identical logical bindings used by more than one stage are merged and keep a stage-visibility mask. Different declarations for the same logical binding are rejected.

Overlapping ranges in the same register class and register space are rejected. An unbounded range therefore cannot be followed by another range in the same register namespace and space.

The compiled layout is sorted by register class, register space, and binding index. Pipeline layout ordering does not depend on shader declaration order.

Both Tier 1 backends compile and validate this layout before native graphics pipeline creation. A conflict therefore fails at the same VICE boundary on Metal and Direct3D 12.

## Command-list binding

The graphics command list exposes logical binding calls for:

- constant buffers
- read-only buffers
- read-write buffers
- sampled textures
- storage textures
- samplers

A binding call must match a resource declared by the active graphics pipeline. Register class, binding index, register space, and array bounds are checked before a native descriptor is created.

`BufferViewDesc` describes a buffer byte range. A zero structure stride means a raw buffer view. A nonzero stride means a structured buffer view. A zero view size means the remaining logical buffer bytes.

A draw fails if a required reflected binding has not been supplied. Missing bindings are not silently replaced with null descriptors.

## Direct3D 12

Direct3D 12 root-signature generation consumes the compiled logical layout.

Bounded descriptor ranges that share the same shader visibility are grouped to reduce root-parameter cost. Samplers use separate descriptor tables because Direct3D 12 does not allow sampler ranges in the same table as CBV, SRV, or UAV ranges.

Each unbounded range gets its own descriptor table. This avoids ambiguous table offsets after an unbounded range and keeps bindless ranges explicit.

Stage visibility maps as follows:

- vertex only: `D3D12_SHADER_VISIBILITY_VERTEX`
- fragment only: `D3D12_SHADER_VISIBILITY_PIXEL`
- shared vertex and fragment bindings: `D3D12_SHADER_VISIBILITY_ALL`

Graphics pipeline creation serializes the compiled VICE layout and creates the native Direct3D 12 root signature from that blob before creating the PSO.

The serialized layout also records each logical binding's root parameter and table offset. Command lists use this mapping when materializing descriptor tables.

Descriptor materialization happens when a draw needs the current binding set. VICE creates shader-visible CBV/SRV/UAV and sampler heaps, writes native descriptors, calls `SetDescriptorHeaps`, then sets each root descriptor table.

Bounded ranges allocate their reflected descriptor count. An unbounded range allocates only through the highest array element used by the current binding set. The bound elements must form a contiguous range starting at zero. VICE does not impose an arbitrary fixed bindless count at this layer.

Resource state transitions are issued before the descriptors are used. Constant buffers use the Direct3D 12 256-byte alignment requirement, while VICE keeps their public logical size unchanged.

Windows tests deserialize generated root-signature blobs and inspect the native descriptor ranges and logical binding locations. This does not require a GPU device.

## Metal

Shaders produced by Metal Shader Converter use a top-level argument buffer for converted resource binding.

VICE does not assume that an HLSL register index is a Metal argument-buffer offset. Metal Shader Converter writes `shader.metal.json` during a Metal cook. The shader cooker normalizes `TopLevelArgumentBuffer` entries against DXC logical reflection and stores class, binding, register space, byte offset, and byte size in the VICE shader manifest.

If converter reflection does not provide an authoritative location for every logical resource, cooking fails. VICE does not infer a byte offset from the converter's `Resources.slot` field.

Cooked Metal shader modules copy the normalized locations into `ShaderBinaryDesc`. Vertex and fragment locations remain separate because independently converted stages may use different top-level offsets for the same logical HLSL resource.

Before each resource-using draw, the command list:

1. verifies every bounded reflected resource is bound
2. allocates a shared top-level argument-buffer snapshot for each used graphics stage
3. writes Metal Shader Converter's documented 24-byte descriptor entries
4. binds each top-level argument buffer at converter bind point 2
5. declares every indirectly accessed buffer and texture with `useResource`, the correct read or read-write usage, and the correct render stage
6. retains argument buffers and sampler states until command submission completes

Buffer descriptors store GPU virtual address plus byte offset and the logical byte length. Texture descriptors store the texture GPU resource ID. Sampler descriptors store the sampler GPU resource ID. Samplers are created with argument-buffer support enabled.

Automatic converter layout is bounded-only in VICE. An unbounded resource causes Metal shader creation or pipeline creation to fail with a clear diagnostic. Bindless Metal resources will use the explicit VICE root-signature conversion path instead of assigning an arbitrary maximum array size.

The current command submission path waits synchronously, so per-draw shared argument-buffer allocations have unambiguous lifetime. A later frames-in-flight allocator can replace these temporary allocations without changing the public resource-binding API.

## Long-term Metal layout

The automatic layout path gives VICE a correct bounded resource path while preserving the same logical API as Direct3D 12.

For bindless resources and larger production workloads, VICE will translate its D3D12-style root layout explicitly through Metal Shader Converter and query converter or runtime resource locations. The logical HLSL identity remains stable while native offsets remain backend-owned.

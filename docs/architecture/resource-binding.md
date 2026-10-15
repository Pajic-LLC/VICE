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

Development source shaders currently have an empty reflected binding list. This keeps runtime source compilation available as a fallback without inventing reflection outside the production cooker.

## Pipeline layout

`compile_graphics_pipeline_layout` combines reflected vertex and fragment resources into one deterministic layout.

Identical logical bindings used by more than one stage are merged and keep a stage-visibility mask. Different declarations for the same logical binding are rejected.

Overlapping ranges in the same register class and register space are rejected. An unbounded range therefore cannot be followed by another range in the same register namespace and space.

The compiled layout is sorted by register class, register space, and binding index. Pipeline layout ordering does not depend on shader declaration order.

Both Tier 1 backends compile and validate this layout before native graphics pipeline creation. A conflict therefore fails at the same VICE boundary on Metal and Direct3D 12.

## Direct3D 12

Direct3D 12 root-signature generation consumes the compiled logical layout.

Bounded descriptor ranges that share the same shader visibility are grouped to reduce root-parameter cost. Samplers use separate descriptor tables because Direct3D 12 does not allow sampler ranges in the same table as CBV, SRV, or UAV ranges.

Each unbounded range gets its own descriptor table. This avoids ambiguous table offsets after an unbounded range and keeps bindless ranges explicit.

Stage visibility maps as follows:

- vertex only: `D3D12_SHADER_VISIBILITY_VERTEX`
- fragment only: `D3D12_SHADER_VISIBILITY_PIXEL`
- shared vertex and fragment bindings: `D3D12_SHADER_VISIBILITY_ALL`

Graphics pipeline creation now serializes the compiled VICE layout and creates the native Direct3D 12 root signature from that blob before creating the PSO.

Windows tests deserialize generated root-signature blobs and inspect the native descriptor ranges. This does not require a GPU device.

## Metal

Shaders produced by Metal Shader Converter use argument buffers for resource binding.

VICE does not assume that an HLSL register index is a Metal argument-buffer offset. Automatic Metal Shader Converter layout is implementation-defined, so the converter reflection output is the authority for top-level argument-buffer resource locations.

Metal graphics pipeline creation already performs the same logical layout validation as Direct3D 12. It does not yet turn that layout into argument-buffer offsets because those offsets must come from Metal Shader Converter reflection.

The Metal asset path will store those converter-derived locations next to the same logical VICE binding identity. Runtime Metal binding will use the converter locations and make indirectly accessed resources resident before draw or dispatch.

This keeps HLSL register identity stable across backends without pretending that the native binding layouts are identical.

## Next runtime layer

The next layer will allocate and populate native resource tables, then expose command-list binding operations through VICE types.

Direct3D 12 will use descriptor heaps and the generated root signature. Metal will use the top-level argument buffer layout reported by Metal Shader Converter.

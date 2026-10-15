# Mesh input

VICE describes mesh input in backend-neutral terms.

The public mesh contract defines:

- vertex buffer slots
- vertex buffer stride
- per-vertex or per-instance stepping
- vertex attributes
- HLSL semantic name and semantic index
- logical shader location
- portable vertex formats
- 16-bit and 32-bit index formats

## Attribute identity

A vertex attribute carries both an HLSL semantic and a logical shader location.

The semantic name and semantic index are used by the Direct3D 12 input layout. The shader location gives VICE a stable numeric attribute identity for backends where HLSL semantics are not the native vertex-fetch key.

Shader locations must be unique across the full graphics pipeline vertex layout.

## Portable limits

VICE currently allows up to 16 vertex buffer layouts and 16 logical vertex attribute locations per graphics pipeline.

These limits map cleanly to both Tier 1 backends and leave the Metal Shader Converter reserved vertex indices untouched.

## Direct3D 12 mapping

The native Direct3D 12 implementation will translate each VICE attribute into `D3D12_INPUT_ELEMENT_DESC`.

The translation uses:

- `semantic` for `SemanticName`
- `semantic_index` for `SemanticIndex`
- the VICE vertex format for `Format`
- the VICE vertex buffer slot for `InputSlot`
- attribute byte offset for `AlignedByteOffset`
- step mode for `InputSlotClass`
- step rate for `InstanceDataStepRate`

## Metal mapping

Metal Shader Converter reserves the lower vertex buffer and attribute indices for converted shader infrastructure.

For the default Metal vertex-fetch path, VICE will map:

- logical vertex buffer slot `N` to Metal vertex buffer index `6 + N`
- logical shader location `N` to Metal vertex attribute index `11 + N`

Game code never sees those native offsets.

The first implementation will use normal `MTLVertexDescriptor` fetch. The separate synthesized stage-in function path remains available later for vertex conversions that normal Metal vertex fetch cannot represent.

## Index input

VICE exposes unsigned 16-bit and unsigned 32-bit index formats.

Index-buffer state and indexed draw recording belong to the command list. Native offset and bounds validation stay inside the backend implementation.

## Delivery sequence

This first slice defines and validates the public mesh contract without changing native draw behavior.

The next slice implements the Metal and Direct3D 12 translations, binds real vertex and index buffers, adds indexed draws, and moves VICE Lab off the vertex-ID demonstration triangle.

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

The Direct3D 12 backend translates each VICE attribute into `D3D12_INPUT_ELEMENT_DESC` during graphics pipeline creation.

The translation uses:

- `semantic` for `SemanticName`
- `semantic_index` for `SemanticIndex`
- the VICE vertex format for `Format`
- the VICE vertex buffer slot for `InputSlot`
- attribute byte offset for `AlignedByteOffset`
- step mode for `InputSlotClass`
- step rate for `InstanceDataStepRate`

Vertex buffers are recorded with `IASetVertexBuffers`. Index buffers use `IASetIndexBuffer` with either `DXGI_FORMAT_R16_UINT` or `DXGI_FORMAT_R32_UINT`. Indexed draws use `DrawIndexedInstanced`.

The command list validates buffer usage, offsets, index alignment, required streams, and the capacity needed for non-indexed vertex and instance ranges before recording a draw.

## Metal mapping

Metal Shader Converter reserves the lower vertex buffer and attribute indices for converted shader infrastructure.

VICE maps:

- logical vertex buffer slot `N` to Metal vertex buffer index `6 + N`
- logical shader location `N` to Metal vertex attribute index `11 + N`

Game code never sees those native offsets.

Graphics pipeline creation builds an `MTLVertexDescriptor` from the VICE layout. Vertex formats, byte offsets, stride, per-vertex stepping, and per-instance stepping are translated there.

Vertex buffers are bound directly on the render encoder at the converter-compatible buffer indices. The command list retains the logical slot model and validates the required streams before drawing.

The normal `MTLVertexDescriptor` path is the current vertex-fetch implementation. A synthesized stage-in function path can be added later for conversions that normal Metal vertex fetch cannot represent.

## Index input

VICE exposes unsigned 16-bit and unsigned 32-bit index formats.

The Direct3D 12 backend stores a native index-buffer view. The Metal backend keeps the validated index buffer, format, and byte offset until the indexed draw is recorded.

Indexed draws support instance count, first index, base vertex, and first instance through the same VICE command-list method on both backends.

## VICE Lab proof

VICE Lab no longer creates its triangle from vertex ID values in the shader.

The Lab triangle uses:

- one interleaved 20-byte vertex stream
- `POSITION0` as `float32x2`
- `COLOR0` as `float32x3`
- a `uint16` index buffer
- the same VICE vertex layout on Metal and Direct3D 12

The HLSL asset consumes normal vertex input and is still cooked through the existing shader pipeline. The development Metal fallback declares the corresponding converter-compatible attribute indices.

This sample is intentionally small. Its job is to prove that mesh data, pipeline layout, native vertex fetch, index binding, and indexed drawing all cross the RHI boundary correctly before VICE moves to transforms, depth, and real 3D geometry.

# Coordinates and depth

VICE uses one coordinate convention across the engine and both Tier 1 graphics backends.

## World and view space

VICE uses a right-handed coordinate system.

The conventional camera looks down the negative Z axis. Positive Y is up. Positive X is right.

The math module stores 4x4 matrices in column-major order and treats vectors as column vectors. Transform composition therefore reads from right to left. A model point reaches clip space through:

`projection * view * model * position`

The current HLSL path declares transform matrices as `column_major` so cooked shader storage matches the engine representation directly.

## Clip space

VICE uses a normalized depth range of `0..1`.

This matches the native depth convention used by Direct3D 12 and Metal. The engine does not insert a backend-specific depth remap between them.

`perspective_rh_zo` builds a right-handed perspective matrix for this convention.

## Depth attachments

A graphics pipeline declares depth behavior with `DepthStateDesc`.

A pipeline can specify:

- depth format
- whether depth testing is enabled
- whether depth writes are enabled
- the depth comparison operation

The first supported depth format is `depth32_float`.

The render pass owns the actual depth attachment for a frame. `RenderPassDesc` supplies the texture, clear value, and clear behavior.

Pipeline state and render-pass resources are intentionally separate. A pipeline describes how depth behaves. A render pass provides the texture that receives the depth values.

## Direct3D 12

A VICE depth texture with `depth_attachment` usage owns a native DSV. The command list transitions the texture to `D3D12_RESOURCE_STATE_DEPTH_WRITE`, binds the DSV with the color target, and clears it when requested.

The graphics PSO receives the matching DSV format, comparison function, and write state.

## Metal

A VICE depth texture maps to a Metal render-target texture using `MTLPixelFormatDepth32Float`.

The render pass assigns that texture to its depth attachment and controls its load, store, and clear behavior.

The graphics pipeline declares the same depth pixel format and owns an `MTLDepthStencilState` built from the portable VICE comparison and write settings.

## VICE Lab

VICE Lab currently proves the convention with an indexed cube.

The cooked vertex shader receives a model/view/projection matrix through `b0, space0`. The same 64 bytes are consumed by the Direct3D 12 and converted Metal paths.

The development source fallback does not pretend to have reflection. It uses a fixed transform inside the source shader while still exercising the same vertex, index, depth, pipeline, and render-pass paths.

The Lab recreates its depth texture when the drawable size changes so the color and depth targets always agree.

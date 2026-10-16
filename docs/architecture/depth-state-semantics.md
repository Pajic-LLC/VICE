# Depth state semantics

`DepthStateDesc` separates depth attachment format, testing, and writing.

A pipeline can use a `depth32_float` attachment while depth testing is disabled. In that case fragments must not be rejected by the configured compare operation. Depth writes still follow `write_enabled`.

Direct3D 12 implements this with `DepthEnable = FALSE`. Metal implements the same behavior with `MTLCompareFunctionAlways` while preserving `depthWriteEnabled`.

This keeps the public VICE contract identical across Tier 1 backends.

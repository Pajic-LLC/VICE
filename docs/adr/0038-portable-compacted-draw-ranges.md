# 0038: Portable compacted draw ranges and immutable Metal arguments

Date: 2026-10-31
Status: Proposed in the CRASHOUT capability candidate

## Context

Converted HLSL `SV_InstanceID` is draw-relative. Native Metal `instance_id`
includes the draw's base instance. Using a compacted-list prefix as indirect
`first_instance` therefore selects different identities across these paths.
The production PBR vertex stage also reads CPU column-major model matrices from
a structured buffer, while the shader cooker defaults to row-major packing.

Many meshlet draws reuse material descriptor arrays but change their compacted
instance slice. Rebuilding every descriptor array and allocating a native Metal
buffer for every root table makes this path impractically expensive. Draws must
retain immutable argument bytes through completion.

## Decision

Emit zero `first_instance` for every runtime GPU batch. Expose each batch's
CPU-known prefix and maximum capacity in `RuntimeGpuDrawBatchResult::ranges`.
Consumers bind that subview of the compacted index buffer before drawing. The GPU
still computes visibility, compaction and actual indirect instance counts; this
protocol requires no readback. A zero-capacity range must be skipped.

The returned range span belongs to the batcher and is valid until the next
`build`, `initialize`, destruction, or move assignment. Offsets and capacities
count uint32 indices, not bytes. Cluster remapping preserves this range layout.
The default, material, shadow, motion and reflection-guide consumers all follow
the same convention.

Declare column-major layout explicitly for CPU matrix payloads in cooked HLSL.
Use the canonical PBR vertex module and the color pass's selected batch geometry
for its camera depth prepass. Shadow geometry remains independently selected.
Enable Metal Shader Converter position invariance for cooked vertex programs and
include that policy in the cache key. Group the expanded prepass-requirement
predicate so caller conditions retain their meaning.

The early depth image serves ray-lighting consumers. Once those passes have
encoded their reads, clear depth for the forward color pass and build visible
occlusion afresh with ordinary depth tests and writes. Even the same vertex
module plus invariance did not eliminate native inter-pipeline depth rejection
in the full scene. Reusing that early depth rejected valid surface pixels. A
numeric depth bias would conceal the disagreement and can leak occluded surfaces;
independent forward depth avoids that compromise. This gives up prepass-based
early-Z savings and repeats depth writes. Current motion/surface-guide coverage
and production character quality still require further integration work.

On Metal, allocate converted root and descriptor tables from command-owned,
256-byte-aligned arena blocks. Cache unchanged descriptor arrays per pipeline;
resource replacement invalidates the affected array and root encoding. Never
rewrite previously encoded argument bytes. Keep blocks and samplers alive until
command completion. Drain temporary Objective-C objects within native encoding
operations, including draws; a C++ native handle getter can otherwise extend a
resource's autoreleased lifetime beyond its command list.

## Consequences and validation

This changes the batch result contract. External consumers must bind range
subviews instead of relying on indirect base instance. Resource buffer formats
and shader register bindings remain unchanged.

GPU regressions verify identity preservation, zero-based arguments, nonzero
buffer-view offsets, nonidentity matrix layout, matching depth/color coverage,
unchanged-binding reuse, resource replacement and arena rollover. Native scene
soak measurements separately check memory boundedness and practical frame cost.
These fixes establish correctness and resource lifetime; they do not establish
production character quality or a frame-rate target.

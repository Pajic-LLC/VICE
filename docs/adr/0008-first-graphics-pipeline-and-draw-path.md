# ADR 0008: First graphics pipeline and draw path

Status: Accepted

## Context

The RHI can create resources and submit copy commands, but a game engine also needs a portable way to create shaders, build graphics pipelines, record render passes, issue draws, and present through the same command stream.

The production shader cooker is not ready yet. The first draw path still needs to prove the runtime contract on Metal and Direct3D 12 without making the temporary compiler path part of game architecture.

## Decision

VICE owns shader modules, graphics pipelines, render passes, draw commands, and presentation commands in the RHI.

The first graphics pipeline supports a vertex shader, fragment shader, one color target, triangle-list topology, culling, and front-face selection. It does not add vertex layouts or resource binding yet.

Development builds may create shader modules from MSL on Metal and HLSL on Direct3D 12. This source path is a bootstrap only. Issue #16 tracks the production cooker, which will author HLSL once and produce backend-ready DXIL and metallib artifacts.

A swapchain exposes its VICE color format. Command lists begin a render pass against a swapchain, bind a graphics pipeline, issue draws, end the pass, and schedule presentation. Presentation is therefore part of command recording instead of a separate game-facing rendering path.

The first VICE Lab draw uses vertex IDs to generate triangle positions in the shader. This deliberately avoids choosing vertex-layout and binding APIs before mesh and material requirements exist.

## Consequences

VICE now has one portable draw contract across Metal and Direct3D 12.

The current source compiler is useful for development but is not suitable for shipping asset pipelines, deterministic caches, or large projects.

Submission remains synchronous for now. The command API is designed so frame scheduling and multiple frames in flight can change that later without changing how draw commands are recorded.

Resource binding, vertex layouts, indexed draws, texture copies, compute pipelines, and the production shader cooker remain separate work.

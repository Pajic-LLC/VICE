# ADR 0001: Foundation platforms and language

Status: accepted

## Decision

VICE engine code starts in C++23.

macOS ARM64 and Windows x64 are first-class development targets. The initial native graphics APIs are Metal on macOS and Direct3D 12 on Windows.

CMake is the project build system. CI must compile and run tests on both target platforms for pull requests.

## Why

VICE needs direct access to native graphics APIs, mature game development tooling, predictable data layout, explicit control over performance-sensitive systems, and access to a broad C and C++ middleware ecosystem.

macOS is the primary local development environment. Windows support starts immediately so platform assumptions are found while systems are still small.

## Cost

C++ requires strict ownership, lifetime, concurrency, and API rules. Supporting two native graphics backends costs more engineering time than using one cross-platform graphics API.

## Revisit when

Revisit the language decision if another language can meet the engine's low-level interoperability, performance, tooling, and platform requirements without increasing integration risk.

Revisit the graphics backend plan if a future API provides first-class support on both target platforms without giving up required native capabilities.

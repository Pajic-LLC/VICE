# Architecture overview

VICE is split into layers so that games can use the parts they need without adopting one genre or one simulation model.

## Core layers

### Core

Owns basic engine services that do not depend on a renderer, physics solver, game framework, or operating system API. This includes versioning, identifiers, common data structures, diagnostics, time, and later the job system and memory services.

### Platform

Owns operating system integration. macOS and Windows implementations live behind common interfaces. Window creation, input, files, clocks, process services, and platform graphics setup belong here.

### World

Owns entities, components, relationships, spatial state, serialization boundaries, and authoritative world data. It must not assume a specific game genre.

Authoritative spatial state uses double-precision global positions. Regions provide local float coordinate frames for simulation, and rendering derives camera-relative float positions from global world truth. Region, streaming-cell, and simulation-island ownership do not replace persistent entity identity.

### Simulation

Owns world rules, materials, interactions, simulation scheduling, fidelity policy, and communication between simulation domains.

A simulation domain may use a specialized solver. Rigid 3D, rigid 2D, structural, fluid, thermal, aerodynamic, and custom domains do not need to share one numerical implementation.

### Rendering

Owns the rendering hardware interface, render graph, scene extraction, materials, lighting, geometry, post processing, and GPU resource management.

The first native backends are Metal on macOS and Direct3D 12 on Windows.

### Audio

Owns playback, mixing, spatial audio, acoustic scene data, and propagation results. Acoustic simulation must be usable by gameplay and NPC perception, not only by the speaker output path.

### Agents

Owns reusable agent concepts such as perception, memory, beliefs, goals, decision making, and action intent. It must not assume every agent is a humanoid NPC.

### Animation

Owns pose generation, motion selection, inverse kinematics, contact handling, procedural layers, and the handoff between intent and physical motion.

### Networking

Owns replication, authority, snapshots, prediction, rollback support, interest management, and simulation ownership. Transport is a separate concern.

### Streaming

Owns data residency and movement between storage, CPU memory, and GPU memory. World streaming and asset streaming share scheduling information but remain separate concerns.

## Dependency direction

Higher-level systems may depend on lower-level contracts. Core must not depend on game frameworks. Platform implementations must not leak operating system types into unrelated public APIs. Third-party implementations stay behind VICE contracts when replacement is a realistic future need.

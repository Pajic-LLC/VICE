# VICE Constitution

This file defines the rules that should survive individual features and games.

## 1. VICE models interactions, not genres

Core engine code must describe general concepts such as entities, materials, forces, fields, signals, relationships, resources, and state. A shooter, racing game, platformer, strategy game, card game, or simulation should be built from those concepts without requiring genre assumptions in the core.

## 2. Gameplay has final authority

VICE provides physically grounded behavior where it helps. A game may replace, bias, simplify, exaggerate, or disable that behavior. Realism is a capability, not a requirement.

## 3. There is one authoritative world state

Rendering, physics, audio, AI, animation, navigation, and gameplay must not maintain conflicting versions of the same fact. Systems may cache or approximate data for performance, but ownership and synchronization must be explicit.

## 4. Expensive features must scale

Every expensive system needs a fidelity strategy. Where practical, it must support lower-cost representations or an off state. Hardware capability should change fidelity before it changes the rules of the game.

## 5. macOS and Windows are first-class platforms

The engine must build and test on both platforms continuously. Platform-specific code stays behind defined interfaces. macOS is not a compatibility port and Windows is not an afterthought.

## 6. Engine boundaries are replaceable

Third-party libraries may accelerate development, but they must stay behind VICE interfaces when they implement replaceable engine services. Public VICE APIs must not depend on a third-party type unless there is a strong, documented reason.

## 7. Simple games must stay simple

A developer making a small 2D game should not need to understand structural simulation, acoustic propagation, or large-world streaming. Complexity is available when the project needs it.

## 8. Performance is part of correctness

A feature that works but cannot meet its intended frame, memory, streaming, or simulation budget is not finished.

## 9. Debuggability is a feature

Simulation state, ownership, timing, memory, and decisions should be inspectable. Reproducible tests and replayable state are preferred over bugs that can only be observed live.

## 10. Documentation records decisions

Architecture decisions must state what was chosen, why it was chosen, what was rejected, and what could cause the decision to change.

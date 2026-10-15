# ADR 0003: Systems share authoritative world truth

Status: accepted

## Decision

VICE systems must not independently own conflicting values for the same world fact.

The world model defines ownership for authoritative state. Rendering, physics, audio, AI, navigation, animation, networking, and tools may derive cached or lower-fidelity representations, but those representations must have a defined source and synchronization rule.

## Why

VICE is intended to connect physical behavior, visuals, sound, agents, and gameplay. That only works when systems agree on what happened.

A door cannot be open for navigation, closed for acoustics, destroyed for physics, and intact for rendering without an explicit transitional state explaining the difference.

## Cost

State ownership needs to be designed instead of allowing each subsystem to invent its own copy. Some high-performance systems will need snapshots or extracted data, so synchronization has to be measurable and debuggable.

## Revisit when

The ownership model should be refined as real systems are implemented. The principle should only be replaced if a different model can preserve cross-system consistency with lower complexity.

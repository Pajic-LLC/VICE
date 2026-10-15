# ADR 0002: Simulation is split into pluggable domains

Status: accepted

## Decision

VICE will not define one universal numerical solver for every kind of physics.

VICE owns the shared concepts around world rules, materials, interactions, simulation time, fidelity, state transfer, and debugging. Specialized simulation domains may use different implementations for rigid 3D, rigid 2D, structural behavior, fluids, cloth, thermal systems, aerodynamics, and game-specific rules.

A domain implementation must stay behind a VICE interface when replacement is expected to be possible.

## Why

Different simulation problems need different data structures, solvers, time steps, precision, and performance strategies. Forcing all of them into one solver would make simple games pay for complexity they do not need and would make specialized simulation harder to optimize.

The engine's value is the consistent world and interaction model between domains, not a requirement that every calculation use the same algorithm.

## Cost

Cross-domain interaction needs explicit contracts. Scheduling becomes more complex because domains may use different update rates and fidelity levels. Debugging tools must show when data crosses domain boundaries.

## Revisit when

Revisit this decision if domain boundaries create measurable correctness or performance problems that cannot be solved with better scheduling or data exchange.

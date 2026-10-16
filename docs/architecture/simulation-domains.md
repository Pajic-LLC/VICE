# Simulation domains

VICE treats simulation as a set of engine-owned domains instead of one global physics backend.

A simulation domain can represent rigid bodies, fluids, structures, aerodynamics, thermal behavior, gameplay simulation, or another solver. The public VICE contract does not expose solver-specific types.

## Domain identity

Every domain has a nonzero `DomainId` and a human-readable name.

The coordinator rejects duplicate IDs. Domain IDs are runtime simulation identities. They are not entity IDs and they are not a persistence format.

## Fixed steps

The coordinator owns one explicit fixed timestep. Each active domain receives the same `fixed_delta_seconds` value and the same step index for a coordinator step.

This first slice does not include a wall-clock accumulator. Higher-level engine code can decide when to request fixed steps without changing the domain contract.

## Fidelity

Domains expose three fidelity states:

- `off` means the domain is not stepped
- `reduced` means the domain still participates but may use a cheaper internal model
- `full` means the domain uses its normal simulation path

VICE does not prescribe what reduced fidelity means for a particular solver.

## Interaction exchange

Domains publish interactions through a VICE-owned emitter.

An interaction contains:

- the source domain
- an optional target domain
- a string kind
- an opaque byte payload owned by the event

A missing target means broadcast.

Interactions emitted during one coordinator step become visible during the next coordinator step. This prevents domain registration order from changing whether another domain sees an event in the same step.

The payload is intentionally opaque at this layer. Follow-up work can add typed interaction schemas for shared engine concepts after real simulation requirements exist.

## Materials

`Material` is an extensible property container. Every property has an explicit owner and name.

For example, a material can contain a density property owned by a physics system, an absorption property owned by acoustics, and a surface classification owned by rendering without forcing those systems into one fixed structure.

The first property value set supports booleans, signed integers, doubles, and strings. More structured value types should be added only when an engine subsystem has a concrete requirement for them.

## Debug data

The coordinator can report domain debug snapshots with fidelity, completed step count, received interaction count, and emitted interaction count.

These counters are diagnostic data. They are not authoritative simulation state.

## Not decided here

This slice does not choose:

- a rigid-body solver
- an ECS layout
- broad-phase collision structures
- contact or constraint schemas
- fluid or aerodynamic methods
- multithreaded domain execution
- rollback or network prediction rules
- serialization of solver state
- large-world simulation island ownership

Those decisions should build on this boundary instead of replacing it.

# World and simulation bindings

World identity and simulation state have separate owners.

`VICE::World` owns entity lifetime, runtime generation, persistent identity, and relationships. It does not store solver state or simulation domain data.

`VICE::Simulation` owns domain coordination and participant identity. A `ParticipantId` is an opaque VICE runtime handle. A domain can translate that handle to a solver-native object internally, but the solver handle does not cross the VICE API boundary.

## Binding model

`VICE::SimulationWorld` provides `EntityBindings`.

One binding contains:

- one generational `EntityId`
- one `DomainId`
- one `ParticipantId`

An entity can have one binding per domain and can participate in several domains at the same time. For example, one entity can have separate rigid, thermal, and aerodynamic participants.

A participant ID must be unique inside its domain. The same numeric participant value can be used by another domain because participant identity is domain-scoped.

## Validation

A new binding is accepted only when:

- the world entity is alive
- the simulation domain is registered
- the participant ID is nonzero
- the entity does not already have a binding for that domain
- the participant ID is not already bound to another entity in that domain

Lookup also checks current world and coordinator state. A stale entity or removed domain therefore cannot resolve a binding even before cleanup runs.

## Entity generations

Bindings use the complete `EntityId`, including its generation.

When the world reuses an entity slot, the new generation does not inherit the old generation's simulation binding. `prune` removes bindings whose entity is no longer alive or whose domain is no longer registered.

## Cleanup

Callers can remove one binding, all bindings for one entity, or all bindings for one domain. `prune` is available for lifecycle boundaries where world entities or simulation domains may have disappeared since the last cleanup pass.

The registry does not destroy solver objects. Domain implementations remain responsible for their own participant and solver-object lifetime.

## Dependency direction

`VICE::World` does not depend on simulation.

The bridge target depends on both `VICE::World` and `VICE::Simulation`. This keeps basic world and simulation code reusable independently and prevents physics or solver assumptions from entering entity identity.

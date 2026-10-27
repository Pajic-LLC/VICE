# Supply dependencies and current failure explanations

## Decision

World owns an optional `Dependency` on an entity. A nil provider denotes a source;
otherwise the provider must have a Dependency. A nil gate denotes an ungated
connection; otherwise the gate must own a DistanceLink. `enabled` expresses local
author intent. Provider graphs are acyclic, single-provider chains with fanout,
bounded to 256 nodes. Self references and dangling components are rejected even
when disabled. These are availability requirements, not electrical circuit
quantities: voltage, current, load, storage, capacity, fuses and flow solving are
outside this contract.

Availability is derived on demand from the authoritative World. A disabled node
fails locally. Otherwise a broken local gate fails the node. Otherwise its
provider determines availability. This precedence selects one deterministic
current explanation when several conditions fail. A consumer reports its local
reason and root reason, the root entity identity, an ordered path, and physical
break impulse/step when the root is a broken link. The path ends at the disabled
node or broken gate; it does not continue through irrelevant providers. An intact
path ends at its source. There is no duplicate persisted powered flag or required
per-frame global solve. Rendering uses an allocation-free bounded predicate;
SDK/debug queries materialize the bounded explanation path.

The model works without a loaded physics backend and ignores region fidelity for
logical evaluation. It performs no implicit activation or physical stepping.
Reduced/inactive regions therefore expose their current saved physical state;
new physical failures still require that physical simulation to run. This is the
initial low-cost policy, not a full cross-domain fidelity system.

## Persistence and ownership

Cooked World format 9 appends the optional provider/gate/enabled record to each
entity after DistanceLink and reads formats 1–8 unchanged. Source format 1 gains
`dependency <provider-id|none> <gate-id|none> <enabled|disabled>`. Typed transactions
add set/clear operations and retain dry-run, revision checks, candidate validation
and atomic publication. Impact reports conservatively include downstream consumers
and their regions from both the old and proposed graph. Restore validates the complete graph and installs it only
after all entities and physical links exist, so source order is irrelevant.

Reverse indexes protect provider components and gate components/identities from
silent removal. Explicitly clear or rewire dependents before removing a provider
or gate. Updating a DistanceLink, including publishing its native break, preserves
the gate identity and is allowed. Saving configuration and existing link evidence
is sufficient to reconstruct availability and its explanation on resume. A link's
solver step remains local to its observing reset, not a global timestamp.

## Application surfaces

An appended optional SDK read callback copies a fixed-size snapshot with local and
root reasons, identities, path length and physical evidence. The ABI1 prefix and
existing result values remain unchanged. Invalid records, missing entities,
missing components and calls outside the owning callback thread are explicit.
World inspect and Runtime debug share a JSON explanation representation. Editor
inspector state and entity recreation preserve authored dependencies.

Both standalone and preview resident-render scene collection omit unsupplied
lights. The existing collection helper uses the same predicate. Authored light
`enabled` stays unchanged; debug reports authored enabled and supplied enabled
separately. This rule opts in only lights carrying a Dependency. Meshes, sounds
and arbitrary devices can use the SDK query; no genre-specific device is built
into World. A supplied light can still be excluded by residency, light budgets or
other renderer policy.

## Validation and limits

The runnable supply-failure example physically stresses one link. Its downstream
lamp loses supply, an independent lamp remains supplied, and debugging identifies
lamp -> harness -> link with the measured impulse. Unit tests cover both render
collection paths, actual loaded SDK callbacks, shared Editor preview, save/resume,
cycles and dangling references, transactional rollback, the depth bound, source
order independence and old saves. Headless CI exercises actual edit/cook/module
staging; native acceptance also launches the game and observes the live state.

This is a bounded current-state explanation, not a chronological causal journal,
replay trace, electrical flow network, shared material identity or general
interaction-provenance schema. Multiple providers, alternate routes, AND/OR
requirements, live link repair without physics reset and electrical quantities
need separate decisions and acceptance tests.

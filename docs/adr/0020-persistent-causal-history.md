# Persistent causal history and Runtime World checkpoints

## Decision

World owns a typed causal journal for distance-link changes, measured constraint
breaks, supply configuration changes, SDK source enable/disable commands and the
resulting availability transitions. This is historical evidence alongside current
state. Derived lighting still uses current dependency state.

Each history has a persistent lineage UUID. Each event has an independent UUID,
monotonic sequence position, capture UUID, optional earlier parent UUID/position,
subject and participant UUIDs, typed reason, region and typed evidence. A physics
break alone carries measured linear impulse and a solver step local to its capture.
Derived transitions refer to their immediate cause; they do not copy measurements.
Propagation emits providers before consumers, linking a failed lamp to the harness
transition and the harness to the physical break. Generic authoring changes never
claim physical provenance. SDK toggles use the gameplay origin.

Recording starts on Runtime module startup and shared authored simulation reset.
A reset/resume starts a fresh capture marker. Restoring or inspecting a World does
not itself record anything. Recording starts only after explicit Runtime activation;
source authoring and cooking cannot manufacture prior runtime evidence. Offline
edits preserve existing records. Forks preserve the saved prefix UUIDs but assign
independent UUIDs to new events and capture segments, even where sequence positions
coincide. Sequence positions are branch-local ordering, not global identity.

## Bounds and validation

The journal retains the latest 1,024 events in a deque. Appending/pruning is O(1).
Mutations inspect the affected dependency closure through reverse indexes, not all
entities every frame. The journal is not a transform log. Parent UUIDs remain even
when parent records are pruned. Query results expose retained range, next sequence,
capacity, capture state and truncation. Selected-entity Runtime debugging includes
its latest eight matching events plus ancestors, capped at 64 records. Missing
entities can still have history. SDK queries page by lineage and sequence and
explicitly distinguish wrong lineage, pruned records and future/unavailable records.

Validation rejects invalid event kinds/origins, nonfinite or incompatible evidence,
noncontiguous sequences, duplicate UUIDs, self/future parents, incorrect retained
parent identities and missing capture markers when the complete prefix is retained.
Historical entity/region references need not remain live. Sequence exhaustion
rejects journaled mutations before World changes. A failed post-solver publication
latches simulation failure until reset so native state cannot keep advancing unseen.

Cooked World format 11 appends an optional bounded journal; formats 1–10 remain
readable with empty history. Source format 1 has optional `causal_history` and
`causal_event` records, validated and serialized deterministically. Snapshot restore
validates the complete candidate before replacing the World. Records are structured
saved data, not cryptographically authenticated telemetry.

## SDK and development checkpoint protocol

ABI1 adds optional size-guarded history/event reads and a phase-guarded supply toggle
at the tail of HostApi. Older hosts return unavailable; older modules remain valid.
Runtime debug retains the existing entity request and adds a strict
`kind: world_checkpoint` request. The main-thread observer snapshots World after
normal publication. No destination path crosses into the game process.

`vice runtime checkpoint <session> <new-output.vworldc>` requests a fixed
session-owned `checkpoint.vworldc`. The Runtime atomically replaces it with the
existing OS-locked exact-byte compare-and-replace primitive. A correlated response
contains SHA-256 of those exact bytes. The CLI checks the hash, decodes/validates the
World, and atomically creates the requested new output. Existing files are never
overwritten, including if another writer creates the destination during the request.
One fixed session file bounds disk use; overlapping requests fail on mismatched
bytes rather than saving another request's World. Export has a 16 MiB limit and
10-second CLI wait. No history event is emitted merely for exporting a checkpoint.

This saves World components, physical publication state and retained history. It
does not save game module globals, input state, solver caches, assets or a complete
execution replay. Loading the cooked checkpoint through the normal startup path
reconstructs derived Runtime state and starts a new capture segment.

## Evidence and remaining scope

`vice_causal_history_tests` exercises real SDK callbacks, physical publication,
causal parent chains, source/binary round trips, deletion, pruning, fork identity,
sequence exhaustion, failed solver publication and optional Editor preview.
`vice_runtime_checkpoint_cli_tests` covers request dispatch, atomic save, hash
correlation, corruption and existing-output preservation.
`scripts/validate_causal_workflow.py --run-runtime` builds the real game module,
observes a physical break and subsequent SDK outages/restoration, exports the live
World, restarts the staged executable from that save, and compares original event
UUIDs/evidence and restored supply state. CI runs headless cook/build acceptance on
macOS and Windows. Native graphical acceptance is a separate local run.

Materials, conserved flows, energy/heat networks, generalized contact causality,
long-term journal storage, distributed capture, replay and save-slot UI remain
future work. This journal establishes a bounded historical foundation for those
systems; it does not implement them.

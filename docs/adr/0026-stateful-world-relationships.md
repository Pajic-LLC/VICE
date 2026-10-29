# 0026: Stateful World relationships (Phase 7, Slice B)

Status: Implemented foundation, following ADR 0025.

## Identity and ownership

A relationship is the existing directed unique edge `(subject persistent UUID, kind UUID, object persistent UUID)`. This tuple is its stable identity. It supports game-owned kind identifiers without the core interpreting their meaning. We retain the existing local `Relation` handle API and add a persistent `RelationKey` contract. No process-local handle is serialized or exported through the game ABI.

Each edge may carry up to 16 typed payloads using Slice A's schema/value contracts. Payloads are authoritative World-owned state, stored sparsely by edge key. They share the component schema registry and schema-use count with entity payloads; a live relation-only use prevents unregistering the schema. There is no separate gameplay database or Editor graph. Schema versions, field kinds, bounds and validation remain the same for both owner kinds.

A distinct UUID per edge was deferred because the existing graph permits one edge per tuple and its three persistent identifiers already provide a stable address. Deleting an edge clears its state. Re-adding the same tuple identifies the same conceptual edge but starts empty; it is not a distinct lifetime/incarnation. If games need parallel edges of the same kind or separately retained edge lifetimes, add explicit instance identity in a future wire/API revision. Do not infer incarnation guarantees from the current key.

## Indexed graph and costs

World retains the legacy relation vector for span compatibility, a hash index from stable key to vector position, and ordered adjacency sets per endpoint. Indexes are derived routing data, rebuilt on restore. State belongs to the World table, not to an index. Swap-removal means the legacy span's storage order is unspecified; canonical snapshots, endpoint queries and SDK pages use persistent-key order.

Exact edge lookup is expected O(1). Add/remove updates ordered adjacency in O(log degree); swap-removal avoids shifting the full relation vector. Entity deletion visits connected edges, including incoming, outgoing and self edges, and releases their data/schema users. Its cost is proportional to connected degree and index updates, not all edges. Self edges appear once in a combined adjacency query. Bulk endpoint enumeration is O(degree) with filtering and returns canonical order.

SDK paging uses an ordered key cursor, not a numeric offset or a rebuilt full adjacency list. Each page visits at most 256 indexed edges and emits at most 64 matches. A filtered page can be empty while `has_more` remains true: continue using the returned cursor. A query over a complete adjacency list visits each scanned edge once across pages, plus tree seek costs. Reset the page when changing endpoint, direction or kind filter.

A process-unique revision invalidates retained pages after relation topology/state writes or World replacement, including equal-count restores. It is ephemeral view metadata, not World time or persistent gameplay state. It must not be persisted or reused across processes. Identical SDK add on an existing edge is idempotent and retains its data. Invalid writes do not change payloads or revision.

Limits retain the existing file's 8 million edge ceiling, with a separate 65536 relation-payload budget and 16 payloads per edge. The common payload helper currently has the same 65536 budget for entity owners; a static assertion prevents silently diverging these limits. These limits are validation bounds, not population/performance promises. Keys are 48 bytes on tested platforms; index nodes, duplicate adjacency keys, allocator overhead, payload vectors and strings add memory. The 30000-edge stress fixture records insertion, copied lookup, cursor traversal, validated encoding, hub deletion and cooked size without fragile timing thresholds.

## Mutation, lifetime and fidelity

World adds explicit add/remove/existence, endpoint queries, paged traversal and typed state read/set/remove. A validated whole-payload upsert is atomic with respect to rejected input. Multi-operation source edits validate a complete candidate before commit. Arbitrary multi-effect Runtime transactions are not provided by this slice.

Both endpoints must exist and the kind must be non-nil. Real entity destruction removes all incident edges and state. Removing spatial representation, changing region, or changing fidelity does not remove generic graph state. Full/reduced/inactive endpoints may relate across region/island boundaries; there is no automatic per-frame relationship simulation. Future streaming must retain persistent entity records/stubs when unloading a representation rather than equating unload with destruction.

The game remains responsible for relationship meaning and explicit changes. Native modules are privileged game code; these APIs do not implement Agent perception/access grants or turn World facts into beliefs. Nothing named ownership, trust, squads or friendship is defined by core APIs.

## Persistence and tooling

World file v17 appends a sparse table of relation key plus typed payload text after the v16 entity-payload table. Empty Worlds add four bytes; the old edge table is unchanged. Record strings are bounded to 8192 bytes. Readers support v1–16 with empty relation state. Unknown edges, duplicate type instances, malformed payloads, missing schemas/endpoints and version mismatches fail complete restore. The replacement World builds indexes and schema-use counts before swapping; failure leaves the original World unchanged.

Source relation records accept `state` metadata using the shared typed field codec. Revision-safe `set_relation_component` and `remove_relation_component` operations join existing edge add/remove; impact reports include both endpoints and their regions. Removing a stateful edge matches its key rather than comparing payload contents. An entity-creation undo refuses to delete an entity with subsequently attached relationships, because that older command does not own/capture those edges.

World snapshots, standalone Runtime, checkpoints and Editor Play clones use the same data. Read-only `render` projections omit both payloads and the generic relationship graph, and cannot be saved/restored. Diagnostic/profiler relation counts use the stored vector size without materializing edges. The earlier `omit` projection still retains keys without payloads for explicit consumers. Runtime debugging exposes sorted `relation_states` with keys and typed values, plus the relevant schema definitions. Source inspection includes edge payloads. Editor Inspector shows keys, schema names and values. Endpoint inspection now uses direct entity/adjacency queries rather than copying the entire World; this supersedes ADR 0025's observation about the old per-entity debug snapshot cost. Causal-context lookup retains its existing bounded-history cost.

ABI 1 appends fixed-layout relation key/info/page DTOs and size-checked callbacks for edge queries/mutation and relation payload access. The component data encoder/decoder is shared between entity and relation callbacks. Old host prefixes and DTOs remain intact. Owner-thread/phase guards apply; frame and stop are read-only. No STL, solver or renderer types cross the ABI. Missing edge/state, missing endpoint, invalid input, schema conflict and stale page results remain distinguishable.

## Evidence and future changes

Tests cover canonical source/bytes, old v16 component files, invalid/duplicate/missing records, atomic transactions/restore, schema guards, stale handles, self/reverse edges, removal/recreation, cross-region/1e12 positions and inactive state. A 30000-edge graph exercises indexed lookup, bounded cursor scans and bulk deletion. SDK tests cover multipage reads, page invalidation after mutation/equal-count restore, sizes, old hosts, wrong threads and frame writes. Editor Play matches Runtime and resets to authored state. A native physical walk/drive/animation/audio loop preserves a game-defined relation counter through mounted checkpoint/restart.

The vector-only scan, offset paging, unbounded event callbacks, separate relationship store outside World, and game-specific relationship classes were rejected. Future evidence may justify compact adjacency storage, more precise memory budgets, multi-edge instance IDs or indexed kind/direction partitions. Typed World events with causal provenance are Slice C; this change does not add a competing event/history system or retain every relation mutation forever.

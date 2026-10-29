# Stateful relationships

[ADR 0026](../adr/0026-stateful-world-relationships.md) defines Phase 7 Slice B. A relation's stable identity is its subject UUID, kind UUID and object UUID. All three are explicit values; only the two endpoints must identify existing entities. Kind meaning belongs to games.

```
component b8000000-0000-4000-8000-000000000100 1 "LinkState" fields 2 1 real "weight" 2 text "label"
entity b8000000-0000-4000-8000-000000000001 none
entity b8000000-0000-4000-8000-000000000002 none
relation b8000000-0000-4000-8000-000000000001 b8000000-0000-4000-8000-000000000050 b8000000-0000-4000-8000-000000000002 state b8000000-0000-4000-8000-000000000100 1 2 1 real 0.8 2 text "game defined"
```

World's `RelationKey` query/mutation contract coexists with the legacy handle-based `Relation` API. Use `has_relation`, `add_relation`, `remove_relation`, `relations_for` and bounded `relation_page`. Use `set_relation_component`, `relation_component`, `relation_components` and `remove_relation_component` for data. Payload schemas and scalar types are the same as [generic components](rules-of-reality.md), with a shared live-schema guard and separate relation payload budget.

The tuple identifies one directed edge, not a lifetime. Self edges are supported. Removing an edge clears its data, and recreating its tuple starts empty. Entity destruction cascades to incident edges; spatial removal, region handoff and fidelity changes preserve them. Stored relations do not run a per-frame simulation. Future Agents require separate observation/access contracts; direct graph queries are privileged game operations.

Source authoring supports `set_relation_component SUBJECT KIND OBJECT TYPE VERSION COUNT ...` and `remove_relation_component SUBJECT KIND OBJECT TYPE`. Existing `add_relation`/`remove_relation` remain available and operate on keys even when data is attached. Revision-safe edits report affected endpoint identities and regions; invalid candidate data cannot partially publish another operation.

`vice world inspect` returns edge `component_payloads`. `vice runtime debug` returns `relation_states` with stable keys/values and `component_schemas`. Editor Inspector exposes the same state; Play writes remain isolated from source. File v17 retains v1–16 readers. Full snapshots/checkpoints preserve edges and data; render projections skip the generic graph entirely and cannot be saved.

Native modules use `RelationKey`, `RelationInfo`, `RelationPage` and Slice A's `ComponentData`. Read an edge or list a direction/kind-filtered endpoint view, then read/write/remove payloads. Queries work in read-only callbacks; writes require start/event/update on the owning thread. Schema registration remains through the common component-schema API.

For paged queries, begin with a default `RelationPage`. Continue calling `read_relations` with the returned page while `has_more` is true. Each call advances its stable-key cursor, scans at most 256 edges and returns at most 64 matches. Empty intermediate pages are valid. Keep endpoint/direction/kind unchanged; otherwise reset the page. `reset_required` means a graph/state edit or same-process World replacement invalidated the retained view; start again with a default page. The revision/cursor is transient and must not be persisted across process runs.

Run `ctest --test-dir build -R 'stateful_relation|relation_runtime' --output-on-failure` and `python3 scripts/validate_relation_workflow.py --vice build/tools/vice_cli/vice --run-runtime`. The example's game-owned ConnectionState lives on the character-to-vehicle edge; its launch count continues from 1 to 2 after a mounted checkpoint/restart while physics, animation and audio continue.

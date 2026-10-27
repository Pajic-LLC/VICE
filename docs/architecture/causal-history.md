# Historical causes in World

For the persistent data and protocol contracts, see
[ADR 0020](../adr/0020-persistent-causal-history.md).

Current dependency debugging explains why a device is unavailable now. The causal
journal preserves what changed earlier, including evidence that survives later
restoration or deletion. Read `World::causal_context(id)` for a bounded event/ancestor
slice, or `causal_info()` and `causal_event(sequence)` to page retained events.
Use event UUIDs for identity and parent UUIDs for cause links. Sequence positions
order one branch; capture UUIDs scope physical solver-step counters.

SDK modules use `Context::read_causal_history`, `read_causal_event` and
`set_dependency_enabled`. The writable command is allowed during update/event/start;
frame callbacks can only read. Availability changes propagate through the ordinary
World dependency graph and effective-light path.

Run the composed example acceptance:

```sh
python3 scripts/validate_causal_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

In an active development session:

```sh
vice runtime debug /path/to/session ENTITY_UUID
vice runtime checkpoint /path/to/session /path/to/new-save.vworldc
```

The checkpoint is a cooked World suitable for the normal startup-world loading
path. To inspect retained history offline, the source serializer can expose the
same records; Runtime debugging includes bounded selected-entity history even when
the selected historical entity has been deleted. Truncation and pruned ancestors
are explicit and must be shown as incomplete evidence by consumers.

# Authored distance links

A logical `.vworld` entity can connect two authored physical bodies:

```text
entity 96000000-0000-4000-8000-000000000003 none distance_link 96000000-0000-4000-8000-000000000001 96000000-0000-4000-8000-000000000002 2 2 0 0 1000 intact 0 0
```

After the endpoint IDs, fields are minimum/maximum distance in meters, spring
frequency in Hz, damping ratio, break impulse threshold in N*s, `intact|broken`,
last measured break impulse and solver step. Zero threshold means unbreakable.
For a newly intact link, evidence fields are zero. Both endpoints must be spatial
rigid boxes in the same region and at least one must be dynamic. Broken records
retain their identities and evidence after physics is removed or regions split.

Edit requests use `set_distance_link <owner-id> <same fields>` or
`clear_distance_link <owner-id>`. They use source revisions, dry-run, full-batch
validation and atomic publication like other World edits. Clear a dependent link
before deleting an endpoint or removing its intact physics. Both endpoint IDs
and their regions are included in affected-identity results.

Gameplay reads `Context::read_distance_link` using the logical link's persistent
UUID. It receives copied endpoints, settings, broken state and break evidence.
The same information appears in World inspect and Runtime entity debugging.
`break_step` belongs to the reset that observed the event; it is preserved by
save/reload but is not a global clock.

See `examples/breakable-links` for a complete SDK-only project. Editor Play and
standalone Runtime instantiate the same links. Playing preserves authored source;
saving the running World preserves the broken record, so resuming does not repair
it silently. Code/source edits require reset. Intact connected mechanisms are not
migrated across regions as a group in this slice.

```sh
python3 scripts/validate_distance_link_workflow.py --vice <vice-executable>
python3 scripts/validate_distance_link_workflow.py --vice <vice-executable> --run-runtime
```

The native check observes selected physical stress break one link, release its
payload and leave an independent link holding its body. SDK tests observe break
state in module callbacks; solver tests cover persistence and validation. See ADR
0017 for format, ownership and scope.

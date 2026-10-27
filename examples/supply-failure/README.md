# Supply Failure

A source supplies two lamps. The left lamp depends on a harness whose availability
is gated by the weak physical link. After one second the SDK module drives the
left payload downward. The real solver breaks its link and releases the payload;
the left lamp loses supply. The independent blue lamp stays supplied. The module
observes these states through the SDK and changes the background.

```sh
build/tools/vice_cli/vice project build examples/supply-failure/supply.viceproj
build/tools/vice_cli/vice project run examples/supply-failure/supply.viceproj
```

Editor Play executes the same isolated module and physical scene. Reset/relaunch
starts from the authored state. Inspect the source with `vice world inspect` or
query these IDs with `vice runtime debug <session> <entity-id>`:

- source: `97000000-0000-4000-8000-000000000221`
- harness: `97000000-0000-4000-8000-000000000222`
- affected lamp: `97000000-0000-4000-8000-000000000223`
- independent lamp: `97000000-0000-4000-8000-000000000224`
- physical gate: `97000000-0000-4000-8000-000000000203`

The affected lamp reports an upstream failure, root reason `link_broken`, the gate
identity and lamp -> harness -> gate path, measured impulse and local solver step.
Its authored light remains enabled, but its supplied light is disabled. Saving a
running World preserves the broken link and supply explanation on reload.

No electrical load, capacity, voltage/current or fuse equations are simulated.
This project demonstrates composed supply availability and current failure
explanations; it does not establish a complete causal history.

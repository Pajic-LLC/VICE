# Authored supply availability

A source, gated harness and light can be composed with ordinary World entities:

```text
entity 99000000-0000-4000-8000-000000000001 none dependency none none enabled
entity 99000000-0000-4000-8000-000000000002 none dependency 99000000-0000-4000-8000-000000000001 <distance-link-id> enabled
entity 99000000-0000-4000-8000-000000000003 spatial 0 3 0 1 light point 1 1 1 10 8 enabled dependency 99000000-0000-4000-8000-000000000002 none enabled
```

Replace the placeholder with an existing distance-link owner. Providers must have
the dependency component, gates must have a distance link, chains are acyclic and
at most 256 nodes long. `none` denotes no provider or no gate. A source has no
provider. A zero-threshold physical link is an unbreakable gate.

Use `set_dependency <entity-id> <provider|none> <gate|none> enabled|disabled` or
`clear_dependency <entity-id>` in a `.viceedit` transaction. The complete candidate
is validated before publication. Removing a referenced provider or gate requires
explicit dependent removal or rewiring. Changes to logical supply take effect on
the next query; structural physical edits still require a simulation reset.

`vice world inspect` and `vice runtime debug` expose `dependency`: config,
availability, local/root reasons, root identity, ordered path and break evidence.
`Context::read_dependency` copies the same summary into gameplay; query providers
or use Runtime debug for the full path. Missing components report `no_dependency`.
Old hosts report `unavailable`. No backend types or pointers cross the SDK.

Rendering gates lights through current supply while preserving authored enabled
state. Runtime debug's `light_enabled` describes author intent and `light_supplied`
describes supply-qualified enabled state, independent of render residency/caps.
Play is isolated from the authored document. Save/reload preserves link evidence
and graph configuration; derived availability is reconstructed without repair.

The runnable project is `examples/supply-failure/supply.viceproj`. Its left lamp
loses supply when physical stress breaks the harness gate; the independent lamp
keeps supply. This is availability logic rather than voltage/current simulation.
The explanation describes the current selected cause, not a historical journal.

```sh
python3 scripts/validate_dependency_workflow.py --vice <executable>
python3 scripts/validate_dependency_workflow.py --vice <executable> --run-runtime
```

Logical queries are bounded and backend independent. They do not wake inactive
regions or synthesize new physical events. See ADR 0018 for precedence, ownership,
persistence and remaining scope.

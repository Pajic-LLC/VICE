# Agent World editing and verification

The `vice` CLI inspects, validates, edits, builds and observes the same authored
World used by Editor and Runtime. It emits `vice.dev.v1` JSON and explicit exit
codes. Inspecting a `.vworld` returns `data.source_revision`, a SHA-256 token for
its exact current bytes.

```sh
vice world inspect assets/worlds/main.vworld
vice world edit assets/worlds/main.vworld move.viceedit --dry-run
vice world edit assets/worlds/main.vworld move.viceedit --apply
vice world validate assets/worlds/main.vworld
vice project build game.viceproj
vice project run game.viceproj
vice runtime inspect /path/to/session
vice runtime profile /path/to/session
vice runtime debug /path/to/session 90000000-0000-4000-8000-000000000201
```

Use the session path returned by `project run`. Runtime debug requests select one
persistent entity ID and read from the normal running World. They do not modify
simulation or expose arbitrary process memory. A query timeout is bounded to two
seconds. Runtime payloads are versioned JSON strings inside `data.payload`.

## Edit request version 1

Create a UTF-8 `.viceedit` request with this structure. Replace the revision token
with the complete token from inspection; replace the sample IDs with existing
World identities. Neither mode accepts an absent or wildcard revision.

```text
vice-world-edit 1
revision "sha256:<64 lowercase hexadecimal digits from inspection>"
set_spatial 90000000-0000-4000-8000-000000000201 1000000000001.25 2.5 3.75 1
set_transform 90000000-0000-4000-8000-000000000201 0 0 0 1 1 1 1
set_fidelity 1 reduced
```

Records are ordered and accept whitespace-separated values. Blank lines and whole
lines beginning with `#` are ignored. Unknown records, extra fields, malformed
values, unsupported versions, more than 256 operations or more than 64 KiB reject
the entire request. Source files are limited to 16 MiB.

| Record | Fields after the name |
|---|---|
| `set_spatial` | entity ID, global x y z, region ID |
| `set_transform` | entity ID, quaternion x y z w, positive scale x y z |
| `clear_transform` | entity ID |
| `set_fidelity` | region ID, `inactive` / `reduced` / `full` |
| `add_relation` / `remove_relation` | subject ID, relation-kind ID, object ID |
| `add_resource` / `remove_resource` | entity ID, resource role, resource ID |

Resource roles are `render_mesh`, `render_material` and `reflection_environment`.
Relations require existing subject/object entities and a non-nil kind ID. Added
resource references require an existing resource. Repeated additions/removals are
idempotent. Transform quaternions must satisfy the canonical World normalization
invariant; this service does not silently normalize invalid input.

## Results and conflicts

`data.code` is `previewed`, `applied`, or `unchanged` on success. Results include
`changed`, `published`, `dry_run`, `current_revision`, `result_revision`, validated
operation count and sorted affected entity/region IDs. Dry-run returns the proposed
revision but never publishes it. No-op apply preserves exact source bytes.

Rejections include `invalid_request`, `invalid_revision`, `revision_conflict`,
`invalid_world`, `transaction_rejected`, `source_busy`, `source_unavailable` and
`publication_failed`. `failed_operation`, when available, is a zero-based index.
Failed transactions never publish an earlier valid operation from the same batch.
Inspect again and rebase after a revision conflict; do not retry with an invented
token. Editor saves also reject external changes while retaining unsaved work.

Exit 0 means success, 2 means CLI usage, 3 means validation/conflict and 4 means an
unavailable source/session or publication failure. Build and launch retain their
existing separate exit categories. Authored edits require a subsequent build/run
or relaunch to affect an already running game; live reload is not implemented.

## Executable verification

```sh
python3 scripts/validate_agent_world_workflow.py --vice build/tools/vice_cli/vice
python3 scripts/validate_agent_world_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The first command uses the actual CLI to inspect, preview, apply, reject stale and
invalid batches, validate and stage a project. The optional second command opens
the staged native game, inspects/profiles it, verifies the selected entity matches
the authored large-world edit, and terminates only that validation process.
On multi-config Windows builds, pass `build/tools/vice_cli/Release/vice.exe`.

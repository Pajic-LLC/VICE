# Authored physics in projects

Attach `rigid_box` metadata to a spatial entity in `.vworld`, or use a revision-
checked `set_rigid_box` transaction. Runtime and Editor preview instantiate the
same VICE rigid-body simulation from that data.

```text
vice-world-source 1
region 1 10 1000000000000 0 0 0 0 0 full none
entity 91000000-0000-4000-8000-000000000001 spatial 1000000000000 -0.5 0 1 rigid_box static 10 0.5 10 0 0 0 0 0 0 0.5 0 discrete solid
entity 91000000-0000-4000-8000-000000000002 spatial 1000000000000.25 5 0 1 rigid_box dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 continuous solid
```

The first box is a floor with its top at y=0. The second starts above it and falls
under gravity. Global positions retain double precision; simulation uses local
float coordinates relative to the region origin.

Fields after `rigid_box` are:

1. Motion: `static`, `kinematic` or `dynamic`.
2. Half extents: x y z, positive meters before entity scale.
3. Linear velocity: x y z, meters per second.
4. Angular velocity: x y z, radians per second.
5. Friction and restitution: finite nonnegative values.
6. Collision quality: `discrete` or `continuous` (dynamic bodies only).
7. Contact kind: `solid` or `trigger`.

Orientation comes from the existing authored transform quaternion; scale changes
the collider dimensions. Do not include backend handles or simulation participant
numbers in source data. They are derived from persistent entity identities.

An edit request uses the same fields after the entity ID:

```text
vice-world-edit 1
revision "sha256:<complete token from world inspect>"
set_rigid_box 91000000-0000-4000-8000-000000000002 dynamic 0.5 0.5 0.5 0 0 0 0 0 0 0.5 0 continuous solid
```

`clear_rigid_box <entity-id>` removes physical participation. Inspect/validate,
dry-run, apply and build/run follow the normal agent workflow. Existing running
instances must be reset or relaunched after structural edits. The shared service
rejects external changes to existing body records, poses or velocities before
advancing the solver, preserving the edit for an explicit reset.

World inspection and Runtime entity debugging report the physical record.
Running state updates position, orientation and velocities in World; it does not
write the authored file. Editor preview is an isolated clone using the same
service and exposes body snapshots through the physics debugger.

```sh
python3 scripts/validate_agent_world_workflow.py --vice build/tools/vice_cli/vice --physics
python3 scripts/validate_agent_world_workflow.py --vice build/tools/vice_cli/vice --physics --run-runtime
```

The native check authors a dynamic body through the CLI, stages a normal game,
observes it falling onto the static floor, verifies large-world precision and
checks that simulation leaves authored source unchanged. The automated simulation
test also compares Editor preview with the shared standalone service, checks
kinematic motion and inactive fidelity, and resumes a saved moving World.

This first slice supports boxes and existing preconfigured physical regions.
Other shapes, constraints, gameplay module controls and streaming new physical
regions remain separate capabilities. See ADR 0013 for persistence and fidelity
limits.

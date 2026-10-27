# Gameplay controls

A project builds `game/game.cpp` as a shared module using only
`vice/runtime/module_api.hpp`. Use `Context::read_entity` with persistent UUID
bytes to obtain copied World state, then `Context::set_body_velocity` to control
a dynamic or kinematic authored box. Velocities use meters/second and
radians/second. Global query positions use double precision.

See `examples/rigid-box-playground` for a complete runnable project. Its SDK-owned
`Key` values interpret input without including platform or solver headers.

`read_entity` returns spatial and rigid-box presence flags, region/fidelity,
position, quaternion, scale, box half extents, motion and velocities. A nonspatial
entity remains queryable with flags unset. Half extents precede authored scale.
Results distinguish unavailable capabilities, missing identity/body, invalid
arguments, static motion, inactive regions, required reset and wrong phase.

Controls execute synchronously during start, event and update. Queries also work
in frame and stop. Calls from background threads or retained contexts outside
callbacks fail. Frame callbacks cannot mutate physics. Successful writes publish
actual solver velocities immediately into World, preserving the next step's
ownership checks. Source files remain unchanged.

Existing ABI 1 modules retain their original host prefix. New SDK methods check
host size and report unavailable when an older host lacks a callback. The frozen
legacy-module fixture guards binary compatibility. See ADR 0014 for the record
and threading contract.

```sh
python3 scripts/validate_gameplay_workflow.py --vice build/tools/vice_cli/vice
python3 scripts/validate_gameplay_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The first command compiles the real project module and stages its authored scene.
The second launches a native game and observes module-driven platform motion and
dynamic floor contact. Automated loaded-module tests additionally deliver input
and verify control, callback restrictions and saved-state resume. Editor Play/Reset compiles the same project module and runs its callbacks against
an isolated clone of the current World, including unsaved changes. Focus the
Scene viewport to send game input. Pause freezes callbacks and physics; Step runs
one fixed gameplay/physics tick. Reset rebuilds the module with fresh globals; a
failed build or start leaves the current preview intact. The scene renderers show
live preview poses. Stop restores authoring interaction. See ADR 0015.

`Context::read_body_support` queries solid support using the body's current
geometry, a caller-selected up vector and minimum normal alignment. It returns
support identity, global contact point, normal and linear-plus-angular surface
velocity. The query includes sleeping bodies, excludes triggers and uses a 2 cm
near-contact margin. No support state is serialized or inferred from an entity's
absolute height. The playground uses this query to gate jumping and inherit
platform velocity. See ADR 0016 for semantics and limits.

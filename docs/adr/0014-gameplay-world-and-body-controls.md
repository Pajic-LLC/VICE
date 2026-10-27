# Gameplay World queries and body controls

## Decision

Append optional `read_entity` and `set_body_velocity` callbacks to the module
HostApi ABI 1 prefix. The original prefix, ModuleApi and entry point retain their
layout and version. SDK loading checks the original prefix size; each new method
checks the end offset of its own callback slot before reading it. Old modules
therefore run on extended hosts, and new SDK modules can detect unavailable
extensions on older hosts. Future incompatible changes still require an ABI
version change. Tests include a module compiled against a frozen ABI 1 definition.

Cross-module records use fixed-width values, 16 persistent-ID bytes and explicit
status codes. They contain no engine objects, backend handles, STL containers,
borrowed World pointers or allocator-owned strings. Entity queries return copied
spatial, transform and rigid-box state. Position remains double precision. The
current snapshot record requires its complete size; an incompatible record
revision must use a new method or explicit version negotiation.

Module key names are SDK-owned, with explicit translation from platform keys.

## Ownership and execution

AuthoredSimulation controls resolve persistent identity to the current region and
body binding. They reject stale external edits, unknown bodies, nonfinite input,
static bodies and inactive regions. Successful velocity changes wake a moving
body without promoting region fidelity. The service publishes the actual solver
velocity, including backend velocity limits, immediately into World and updates
its expected-state baseline. Subsequent physics steps and saves observe that same
state. Calls do not rebuild the simulation or write authored source files.

The module bridge permits queries only during lifecycle callbacks on the calling
thread. Velocity changes are permitted during start, input/event and update, and
rejected during frame and stop. A thread-local active bridge rejects background
or retained-context calls before accessing World. These restrictions define
engine threading and synchronization; native modules remain trusted process code.
Callbacks are synchronous. A successful write means it was applied, not queued.

Editor preview forwards the same control operation into its isolated shared
service. It does not mutate the authored document. Loading project modules inside
Editor preview is separate integration work; this decision does not claim it.

## Validation and limits

An SDK-only playground compiles through the normal project builder and contains
a controllable dynamic box, moving kinematic platform, floor, mesh, camera and
light. Integration tests load a real shared module, deliver input, compare World
and solver state, reject invalid/phase/thread calls, and save/resume controlled
motion. Preview/standalone tests apply identical controls and compare outcomes.
A script optionally launches the staged native project and inspects live motion.

This slice controls velocity, not force, impulse, teleport, body creation or
structural edits. Character grounding and authored constraints remain separate
contracts. The sample's jump uses a documented height heuristic. A backend may
limit requested velocities; clients should query the resulting state. No
network determinism or full solver-cache persistence is promised.

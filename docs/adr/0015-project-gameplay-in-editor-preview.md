# Project gameplay in Editor preview

## Decision

Editor Play/Reset compiles `game/game.cpp` through the canonical project module
builder and loads it into the isolated preview World. Module compilation is now
a public ProjectWorkflow service used by both standalone builds and preview.
The module target is rebuilt with clean objects on each invocation so rapid edits
are not missed by build tools with coarse timestamp resolution. It does not save
or cook the current document. Preview clones the current
in-memory World, including unsaved edits, rather than loading a stale cooked one.

Project preview always uses the general authored simulation service. A project
without a module still previews its authored records; familiar Lab entity IDs do
not implicitly create physics. Legacy fixture preview remains available only to
callers that explicitly use the old fixture reset without project configuration.

The same GameModuleLifecycle supplies start, event, update, frame, stop and destroy
behavior as standalone Runtime. Each normal preview frame calls module update,
advances bounded fixed-step physics, then calls module frame. A paused single step
calls update with one fixed interval, advances exactly one physics tick and calls
frame. Paused host frames do not update the module or physics. Start runs during
reset, and frame establishes the initial clear color. Stop runs once for a started
session, before module destruction and World/physics teardown.

## Module image lifetime

Each preview copies the compiled module into a unique temporary directory and
loads a uniquely named image. This prevents a live DLL from blocking recompilation
or reset on Windows and prevents loader reuse of previous module globals. A new
session is fully initialized before replacing the current one. Build, load or
start failure therefore preserves the old session. The old module stops before
it unloads; its owned image directory is removed afterward on a best-effort basis.
A process crash can leave a temporary image directory. Arbitrary neighboring DLL
or resource dependency staging is not introduced by this change; modules still
use the established SDK build contract.

## Input and display

Gameplay input is routed only to a running module with focused Scene viewport and
application. Losing focus or pausing sends focus loss, allowing held input to be
cleared. Pointer positions are translated into viewport-local coordinates.
ImGui still receives input state, while authoring shortcuts are suppressed for
consumed gameplay events. Pointer input continues to reach editor controls, so
Pause/Stop remain usable. Authoring scene manipulation is suspended during an
active preview; stopping restores it.

The scene mesh renderers consume the preview World while it is active, rather
than showing authored poses beneath a live physics overlay. Module frame clear
color is used. The editor camera and inspector remain editor tools; preview is
not a pixel-identical standalone game window. Authoring edits in other panels
remain separate and require resetting the preview to be reflected there.

## Acceptance and remaining work

Tests compile a real project module, deliver viewport input, and compare its
preview positions to the same standalone lifecycle and simulation. They verify
pause, single step, coordinate translation, unsaved-state isolation, compilation
and startup failure preservation, module-global reset, rebuild while loaded,
exact stop/destroy counts, and moduleless projects without implicit Lab physics.

This closes the project-module execution gap from ADR 0014. It does not add hot
state transfer, arbitrary native dependency packaging, a game-camera view,
character grounding, authored constraints or runtime structural mutation.

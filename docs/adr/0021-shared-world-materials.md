# Shared World material identity

## Decision

World owns a bounded catalog of material definitions and an optional material UUID
on each entity. The same UUID selects physical surface properties and an optional
render material resource. Solver material numbers and GPU material rows remain
derived runtime identities. Neither crosses the persistent or module boundary.

A definition contains a non-nil UUID, a name of 1–128 bytes without control
characters, density in kg/m³, nonnegative friction, restitution in [0,1], and an
optional registered render resource UUID. Density is finite and bounded to
[10^-6,10^9] kg/m³. There are at most 4,096 definitions per World. This first schema
contains properties consumed by implemented services; thermal, electrical, acoustic
and other property families remain future extensions.

An assigned material overrides a rigid box's legacy inline friction and restitution.
Unassigned boxes retain their existing inline behavior and the previous native
box density of 1,000 kg/m³. Density applies to the collider dimensions after authored
scale. Dynamic mass is density × box volume; static/kinematic bodies report zero
dynamic mass. Character controller mass remains its explicit authored profile;
assigning a material to a character does not replace controller policy or mass.

AuthoredSimulation maps the canonical sorted catalog to derived MaterialIds and
registers the catalog in each physical domain. Bodies carry those material IDs.
Native box shapes receive density, and debug/SDK mass reads come from actual native
inverse mass, not only the authoring calculation. Box creation rejects nonfinite
or out-of-range calculated mass (<10^-6 or >10^15 kg) before native allocation.
World surface calculation is a prediction; physical instantiation remains subject
to backend size/mass limits. Density and the registered surface survive normal
World checkpoint reconstruction and bridge-owned creation descriptors.

## Consistency and mutation

World rejects unknown material assignments, duplicate catalog IDs, invalid scalar
values, missing render resources and conflicting shared/explicit visual bindings.
A material with live entity users cannot be removed. A render resource used by a
catalog entry cannot be destroyed. A matching explicit visual reference is allowed;
a conflicting one is rejected. Clearing the shared assignment restores the entity's
legacy inline surface and explicit visual reference.

`resource_reference(entity, render_material)` returns the effective binding.
`resource_references(entity)` and snapshots preserve explicit authored references;
they do not synthesize duplicate catalog references. Both direct and resident
Runtime render extraction resolve the effective binding through World. Catalog
lookup and surface reads are O(1), apart from the small per-entity explicit list.
Authoring mutation/removal can scan material users. No material scan is added to
physical stepping or each body query.

A material revision changes on definition/assignment mutation, including removal
of an assigned entity. Shared physical simulation observes it before stepping or
accepting body/controller commands. A changed catalog or assignment requires reset;
the solver cannot continue with stale density or friction. This conservatively
includes name-only and render-only catalog edits. Same-value writes are no-ops. Successful World restore advances the existing
material generation rather than copying a reconstructed count. Generations are
process-unique, so equal-sized catalog/assignment replacement and World copy
replacement cannot alias the active solver generation. Generation
exhaustion rejects mutations instead of wrapping.
Reset constructs replacement derived state before publishing it. The existing
failed-publication latch and World authority rules remain in effect.

Editor entity undo/recreation preserves the assignment. Undo preflights catalog
references to owned resources before deleting an entity, avoiding partial deletion.
Inspector exposes the material identity, density, surface values, volume and mass.
Typed catalog/assignment edits use the existing complete-candidate transaction,
source revision, dry-run and atomic publication services. Dry-run includes users
from both sides of a change, their regions and downstream supply impact.

## Persistence and historical provenance

Cooked World format 12 appends the bounded catalog and sparse entity-to-material
bindings, and adds two participant material UUIDs to causal records. Versions 1–11
remain readable; old worlds retain inline surfaces and old events get nil material
provenance. Sparse decode allocates by assigned-entity count, avoiding another
full-entity lookup table for Worlds that do not use materials. Counts, duplicate
bindings and missing entities are validated before World replacement.

Source format 1 gains:

```
material UUID "Name" density friction restitution RENDER_RESOURCE_OR_none
entity UUID ... material MATERIAL_UUID
```

Causal event records accept optional trailing `materials FIRST_OR_none SECOND_OR_none`.
Old records without the clause remain valid. Canonical serialization preserves
catalog order, binding identities, source precision and event UUIDs deterministically.

Only measured physical constraint breaks freeze the two endpoint material UUIDs.
Derived transitions follow the original physical event through existing parent IDs;
they do not copy material provenance or measurements. Later reassignment or catalog
removal cannot change the saved event identities. Historical references need not
remain live. A record freezes material identity, not a versioned copy of every
material property; reconstructing earlier mutable definitions is not implemented.

## SDK and observability

ABI1 appends optional `read_material` and `read_causal_materials` callbacks and new
size-guarded DTOs. Existing EntitySnapshot, CausalEventSnapshot and other published
DTO layouts remain unchanged. This avoids invalidating modules compiled against
the previous journal extension. Old hosts report unavailable through size checks.
Reads require the owning callback/thread. They return copied UUIDs, scalar surface
values, computed volume/mass and an explicit flag for native mass availability.
No backend handles, World pointers or mutable catalog API cross the SDK boundary.

World inspect includes the catalog and resolved entity surface. Runtime debug adds
the same surface and observed native mass. Causal history exposes the frozen material
UUIDs. The selected-event bound remains 64 records under the 64 KiB transport limit.
This does not add a general contact journal or a simulation interaction replay.

## Acceptance and remaining work

The material playground composes slippery/grippy boxes, elastic/nonelastic drops,
a hanging breakable link and its supplied device at a large global origin. It uses
the existing built-in mesh renderer so it runs without the separate DXC/converter
installation. It does not claim unique cooked material shaders or native PBR visual
acceptance. Shared render-resource selection is tested in both production extraction
paths; compiling/executing project material shaders still requires their toolchain.

Tests cover actual native mass, scaled collider mass, friction and restitution,
real SDK reads/guards, material-aware physical history, source/binary round trips,
version 11 history loading, reset rejection, catalog deletion/recycling, render
binding consistency, Editor preview/source isolation and undo preflight/recreation.
The workflow edits shared density through the canonical file service, checks stale
and invalid requests, cooks/stages the SDK module, optionally observes native
behavior, exports a live checkpoint, and restarts from it with original material
identities, native mass and failure evidence intact.

Conserved material/energy flows, temperature, wear, electrical load, pair-specific
material laws, layered/multiple surfaces, character surface locomotion, hot solver
material patching, profile history, full replay and production Metal acceptance
remain separate work.

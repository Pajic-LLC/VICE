# Authored breakable distance links

## Decision

World owns an optional `DistanceLink` component on a logical entity. It references
two distinct persistent entity IDs and stores center-to-center distance limits,
spring frequency/damping, an optional break threshold (zero means unbreakable),
and broken state with measured impulse and solver-step evidence. No backend
constraint or participant handles are persisted or exposed through the SDK.

Intact endpoints require spatial rigid boxes in one physical region, with at
least one dynamic endpoint. The link owner cannot be an endpoint. Broken links
retain endpoint identity but no longer require endpoint physics or a common
region. Deleting a referenced endpoint requires explicitly clearing the link
first, including retained broken evidence. Reverse dependency indexes let hot
position/velocity publication avoid scanning every entity.

Cooked World format 8 adds the optional record and continues reading versions
1–7. Authored source and revision-checked transactions use the same neutral
record and deterministic serializer. Restore creates all entities/bodies before
validating/installing links, preserving transactional replacement and allowing
forward references in source.

## Simulation and persistence

AuthoredSimulation instantiates intact links after deriving body bindings. It
uses the existing distance-constraint solver behind VICE types. Each fixed step
publishes native break evidence into authoritative World before released bodies
can hand off regions. Broken records are not recreated as solver constraints on
reset or resume. External link additions, removal, repair or setting changes
require reset; a revision and record preflight reject them before solver advance.

Intact links pin their endpoints to their shared region under the current handoff
contract. A requested split is rejected rather than silently dropping a joint.
After break, endpoints can hand off independently. Group migration of connected
mechanisms is separate work. Region fidelity is respected, without implicit
activation. The measured break impulse uses N*s; `break_step` is local to the
simulation reset that observed the break, not a global persistent timeline.

The SDK gains an optional size-checked read callback. World inspection, normal
Runtime entity debugging and Editor inspector/undo state preserve link settings
and evidence. Gameplay can observe failures without touching arbitrary memory.

## Validation and limits

Tests cover a held payload, solver-triggered release, immutable source preview,
broken-state save/resume, endpoint dependencies, invalid transactional batches,
old format 7/6 reads, SDK observation and stale-state rejection. A runnable project
holds two payloads, stresses only the weak link, observes its break and fall,
and verifies that the independent link remains intact. A script exercises the
real edit/cook/module/stage pipeline and optionally the native game.

This slice joins body centers. Local attachment frames, hinges, fixed joints,
slider motors, joint rendering, repair without reset, cross-region mechanisms,
force APIs and a general causal event journal remain future work. Persistent
break evidence is a bounded state record, not a claim of complete causal history.

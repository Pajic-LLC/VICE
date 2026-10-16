# Automatic character region handoff

## Decision

VICE can resolve World-owned characters across bounded large-world regions without gameplay choosing the destination region or calling the transfer primitive directly.

`WorldBridge::orchestrate_characters` reads the live character state, synchronizes the authoritative global World position, evaluates region ownership, requests unavailable destination regions from the streaming layer, and uses the existing transactional `handoff_character` path when the destination is ready.

The manual handoff method remains the transfer primitive. The orchestrator decides when to call it.

## Region bounds

A World region can define an optional local axis-aligned `RegionBounds`.

Bounds use the region's local float coordinate frame. The minimum face is inclusive and the maximum face is exclusive on every axis.

This makes adjacent regions unambiguous. A point on the shared maximum face of one region can belong to the adjacent region that owns the same point through its minimum face.

Bounds must be finite and must have positive size on all three axes.

Regions without bounds remain valid. They can be used for manual spatial ownership and manual handoff, but they do not participate in automatic destination lookup.

## Deterministic destination lookup

`World::region_containing` accepts a global double-precision position and returns the bounded region that contains it.

If authored bounds overlap, the lowest `RegionId` wins. This makes the result independent of unordered container iteration order.

The overlap rule is a deterministic fallback, not a recommendation to author overlapping ownership regions.

Region bounds are part of `RegionDesc`, so existing World snapshots preserve them without a separate serialization format.

## Fixed-step orchestration

The world or streaming layer should call `orchestrate_characters` after the rigid domains have completed the fixed simulation step.

For each World-owned character, VICE:

1. resolves the current World entity through its persistent ID
2. reads the live character state from the current rigid domain
3. converts the local foot position into the authoritative global World position
4. writes that global position back to the World entity while keeping the current region
5. evaluates current region containment and destination readiness
6. requests streaming work or performs transactional handoff when required

Characters are processed in persistent-ID order so orchestration does not depend on creation or container order.

## Prefetch before crossing

`CharacterRegionOrchestrationConfig::prefetch_seconds` controls a simple velocity look-ahead. The default is 0.5 seconds.

When the live position is still inside the current bounded region, VICE predicts a future global position from the character's current linear velocity.

If that predicted point leaves the source region and enters another bounded region that is not ready, VICE emits an activation request before the real crossing. The character remains in the source island.

If the predicted destination is already ready, VICE does not move the character early and does not emit an unnecessary activation request. Actual handoff still waits for the live foot position to leave the source region.

A negative or non-finite prefetch value disables look-ahead for that call.

## Destination readiness

A destination is ready only when all of these are true:

- the World region is not `inactive`
- the region has a rigid domain registered with `WorldBridge`
- that rigid domain is registered with the simulation coordinator

A `reduced` or `full` region can receive a handoff when its rigid domain is available.

An inactive or unregistered destination never receives the character.

## Activation requests

When the character is approaching or has crossed into an unavailable bounded region, `WorldBridge` publishes a `RegionActivationRequest` containing:

- persistent character owner
- source region
- target region
- target simulation island
- target streaming cell

The request is an intent signal. `WorldBridge` does not load assets, create a rigid domain, or change region fidelity itself.

A streaming system consumes the request, prepares the target region, registers the target rigid domain, and leaves the normal orchestration pass to retry the handoff.

Requests are deduplicated per persistent owner and destination. Repeated orchestration while the same target remains unavailable does not grow the request list.

Callers can clear consumed requests. If the need still exists on the next pass, VICE publishes the request again.

## Crossing into a ready region

When the live global position has left the source bounds and belongs to a ready destination region, the orchestrator calls the existing transactional `handoff_character` path.

That path preserves live velocity, horizontal movement target, queued jump state, controller policy, persistent World identity, and large-world position.

The destination local `CharacterId` can be remapped without changing the World owner.

## Waiting for streaming

If the live character has already crossed into a bounded destination that is not ready, World global position continues to track the live source controller, but the World region and local solver ownership remain the source region.

The source controller is not destroyed and no partial transfer occurs.

Once the destination becomes ready, the next orchestration pass performs the normal transactional handoff from the live source state.

## No-target space

A character can leave a bounded source region without entering any other bounded region.

In that case VICE records a `no_target` orchestration result, keeps the source controller alive, keeps the current World region, and continues synchronizing the live global position.

The engine does not invent a destination or destroy the character because world coverage is incomplete.

## Orchestration events

Each pass publishes one `CharacterRegionEvent` for each bridge-owned character.

The current actions are:

- `stayed`
- `handed_off`
- `activation_requested`
- `no_target`
- `failed`

Events are rebuilt on each orchestration pass. Activation requests persist separately until they are no longer needed or a caller clears them.

## Current boundaries

This milestone does not define:

- asset or terrain streaming implementation
- automatic rigid-domain construction
- automatic unloading of regions behind the character
- hysteresis beyond min-inclusive and max-exclusive ownership
- portal or non-spatial handoff rules
- network prediction or reconciliation
- character versus character transfer coordination
- automatic rigid-body region orchestration

Those systems can use the same region bounds, activation requests, persistent ownership, and transactional transfer foundation.

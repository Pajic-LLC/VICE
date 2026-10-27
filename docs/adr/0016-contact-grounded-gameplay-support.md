# Contact-grounded gameplay support

## Decision

AuthoredSimulation exposes a copied `BodySupport` query by persistent entity ID.
The caller selects its up direction and minimum normal alignment (default +Y and
0.7). The query returns an eligible supporting entity, a double-precision global
contact point, normal and surface velocity. The surface velocity includes linear
motion and angular velocity crossed with the contact offset.

Support uses a backend-neutral body-contact query implemented through the narrow
phase against the body's current shape and pose, with a 2 cm separation margin.
It excludes the queried body and trigger shapes. Sleeping bodies remain
queryable. Contact event streams describe transitions and can remove pairs when
bodies sleep; event presence is therefore not a grounding predicate. No events
are fabricated to keep a sleeping body grounded.

Queries return copied derived state, not a new persistent World component.
Save/resume reconstructs support from restored geometry, without serializing
solver contact caches. Identity/pose/body edits that require simulation reset
produce no valid support result; inactive regions and trigger bodies are
unsupported. Eligible contacts are selected by greatest up alignment with stable
identity and point tie-breaking. The query does not wake bodies or promote region
fidelity, and uses spatial collision queries rather than scanning every body.

## Module and gameplay contract

Append an optional size-checked `read_body_support` callback to the ABI 1 host.
Its record contains fixed-width values and persistent identity bytes. Old hosts
report unavailable. Invalid up vectors, thresholds and output record sizes are
rejected. Queries follow the same callback/thread restrictions as entity reads.

The playground now gates jumping on physical support rather than absolute height.
It also rejects jump requests while separating upward from that support and adds
support surface velocity while grounded. Holding a key is still an example input
policy; a game may use a different controller. Grounding does not replace a full
character solver, capsule stepping, slope sliding, navigation or animation.

## Verification and limits

A real SDK-only module jumps from a floor at y=50 near a 10^12-meter origin in both
standalone lifecycle and Editor preview. Tests cover sleeping support, caller-
selected up direction, triggers, separation/airborne rejection, repeated jump
requests, moving and rotating platform surface velocity, inactive regions,
invalid records/thread calls and support reconstruction after save/resume.

The 2 cm margin makes this a near-contact support query, not proof of sustained
load or a friction constraint. Clients should use the returned normal, velocity
and their own movement policy. Velocity controls remain explicit; this query adds
no force, impulse, teleport, authored character or constraint format.

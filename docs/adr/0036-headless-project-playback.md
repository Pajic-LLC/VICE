# 0036 — Headless project playback

Accepted, 2026-10-29.

A real external game project needs to exercise its module, physics and checkpoint
restart in CI without a desktop. Add `vice runtime simulate` over the same staged
manifest, cooked assets, World, module loader and lifecycle used by native Runtime.
The engine accepts step count and optional checkpoint; scenario logic and assertions
belong to the consuming project. No game semantics or second gameplay database enter VICE.

Each step advances World time, invokes module update, steps authored physics,
advances presentation and invokes the read-only frame callback, in native order.
The fixed step is 1/120 second. Pause/rate policies remain authoritative. There is
no renderer, device input or audio output; this does not replace native/Editor QA.
A maximum of 72,000 steps bounds a command. Module loading executes trusted native
project code, exactly as native Runtime does; this is not an untrusted-code sandbox.

Restore uses existing transactional World restore before constructing derived
physics/presentation. Save uses the existing versioned snapshot codec and atomic
create-only publication after stopping the module. Failures publish no checkpoint;
existing destinations are preserved. No new persistence format or module ABI is
introduced. Inspection now accepts both authored `.vworld` and cooked `.vworldc`.

Cost is O(steps × active simulation cost), with one initial snapshot/restore and one
final serialization. It intentionally does not render or emulate window events.
Independent cold launches allocate different causal stream identities; fixed-step
entity/time determinism is tested, not byte equality between independently created
histories. Checkpoint identity is preserved on resume.

Rejected: a game-specific test runner inside VICE, a mock module host, and GUI-only
CI. Revisit when a reusable explicit input replay contract is required. For now a
game may author a test-driver flag that issues ordinary SDK controls.

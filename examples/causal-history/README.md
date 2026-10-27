# Causal history and checkpoint resume

Build `history.viceproj` with `vice project build`. Run the staged game with a
Runtime development session, or run the complete acceptance script:

```sh
python3 scripts/validate_causal_workflow.py --vice build/tools/vice_cli/vice --run-runtime
```

The game stresses an authored breakable link. The harness loses supply and its lamp
turns off while the independent lamp stays supplied. Two seconds later the SDK
switches the source off; after another two seconds it restores the source. The
independent lamp recovers while the broken harness remains unavailable. Historical
physical and derived cause UUIDs remain queryable throughout those later changes.

The acceptance script exports the actual running World with `runtime checkpoint`,
restarts a private staged copy using that cooked save, and verifies that the original
failure evidence and restored source survive. A resumed game recognizes the broken
link and does not repeat the stress/toggle sequence. It opens a fresh capture segment.
Authored source is never modified by Runtime. The save contains World state, not
module globals, assets, held input or solver caches.

# Animated walk and drive

Build with `vice project build examples/presentation-playground/presentation.viceproj`, then launch the executable reported by the command. Open the same project in Editor and use Play for the shared Runtime/SDK presentation path. Arrow keys move or drive, Space jumps on foot/brakes in the car, E enters/exits, mouse movement orbits the camera. The opening automatically walks, mounts, drives, brakes and exits; movement keys take manual control.

This is a first imported articulated-character gate for CRASHOUT. `sources/humanoid.gltf` contains original embedded geometry and idle/walk/ride clips. The importer produces 13 rigid mesh nodes with stable resource/part UUIDs; ordinary World spatial transforms carry their evaluated poses. It does not implement weighted skinning, animation blending, IK, or textured character materials. The car is still a chassis block, not final vehicle art.

Reimport into a **new** directory:

```
vice rig import examples/presentation-playground/sources/humanoid.gltf /tmp/vice-humanoid-import a4000000-0000-4000-8000-000000000200 a2000000-0000-4000-8000-000000000003
```

Merge the generated bindings into a World with a character root. This example already includes those bindings. Source assets are kept separate from `.vice` cooked and staged output.

`assets/audio` contains original synthesized mono 48 kHz WAV fixtures: vehicle harmonics, a footstep-like noise rhythm, and a stationary beacon. The `.vsound` files associate relative WAV paths with stable resource UUIDs. The shared mixer uses World source positions and the primary camera pose for equal-power stereo panning, inverse-distance attenuation, and range cutoff. Device output is optional; deterministic offline mixing and saved clocks remain available with `VICE_ENABLE_AUDIO_OUTPUT=OFF`. This is a bounded prototype mixer without HRTF, occlusion, Doppler, streaming, or a background mixing worker. Long rendering stalls can underrun its ring and are reported by Runtime debugging.

The game module selects walk/idle/ride and adjusts vehicle gain/pitch through the SDK. Checkpoints preserve the attachment plus clip/time/rate and audio playback time/gain/pitch. They reload against the same cooked asset package. The format is World v16, with readers for v1–15 retained.

The humanoid geometry, animation keys and synthesized audio were created for this repository; no third-party character or sound assets were imported. Dependency notices are in `docs/licenses/presentation.txt` and staged game packages.

Validation: `python3 scripts/validate_presentation_workflow.py --vice <built-vice> --run-runtime`. Omit `--run-runtime` on headless CI. CTest adds importer/PCM/World/SDK/preview/save tests.

Phase 7 Slice A adds game-defined `SessionState` on the physical vehicle. Its schema and initial payload are authored in the same World. The module verifies the schema, increments a saved launch counter at startup, and increments an interaction counter when E is pressed. Engine core does not interpret either field. Runtime `component_payloads`/`component_schemas` and Editor Inspector expose this state. A checkpoint resumes the counters in a fresh process while physical movement, animation and audio continue. See `scripts/validate_component_workflow.py`.

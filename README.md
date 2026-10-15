# VICE

VICE is Pajic Studios' game engine project.

The engine is being built around a simple goal: give developers the tools to build any kind of game without forcing one genre, one simulation model, or one visual style.

Development targets macOS and Windows as first-class platforms from the start.

## Status

VICE is in foundation development. The repository structure, build system, platform layer, world model, rendering architecture, simulation interfaces, and developer tooling are being established before feature work expands.

## Project rules

- VICE models interactions, not genres.
- Gameplay has final authority over simulation.
- Expensive systems must scale down or turn off.
- macOS and Windows are first-class targets.
- Platform-specific code stays behind platform interfaces.
- Engine code must not depend on game-specific code.
- Documentation uses direct human language. No em dashes and no filler.

See `docs/` as the project grows.

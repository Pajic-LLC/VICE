# Project playback and game-driven development

VICE remains a separate reusable engine. Production game requirements now drive
priority: requirement → reusable engine capability → external game integration →
regression coverage. More speculative renderer or physical-character research is
subordinate to a demonstrated production blocker.

```
vice project build /path/to/game.viceproj
vice runtime simulate /path/to/staged/game 2400 /path/to/new-save.vworldc
vice runtime simulate /path/to/staged/game 120 /path/to/new-resumed.vworldc --restore /path/to/new-save.vworldc
vice world inspect /path/to/new-resumed.vworldc
```

On Windows use the staged `.exe`. Checkpoint parents must already exist and output
files must be new. Steps are integers from 1 to 72,000, at 120 Hz. JSON reports the
result, World ticks, checkpoint location and whether authored physics was active.
Game callback phase restrictions and stable SDK value contracts remain in force.
There is no visible rendering or audible output. CPU presentation still advances.

Pin engine dependencies to an exact revision. Keep game rules, test scenarios,
content and consumer CI in the game's repository. Use `scripts/validate_playback_workflow.py`
for VICE's generic module/physics/resume/validation regression. Desktop, Editor Play,
visual quality and actual hardware input still require their existing native checks.
See ADR 0036 for failure, persistence and determinism limits.

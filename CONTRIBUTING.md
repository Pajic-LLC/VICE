# Contributing to VICE

VICE is early in development. Changes should preserve the engine's ability to support different genres, simulation styles, and hardware classes.

## Before writing code

Check the architecture notes in `docs/architecture/` and the decisions in `docs/adr/`.

A change needs an ADR when it creates a long-lived constraint that will be expensive to reverse. Examples include public APIs, persistent file formats, renderer boundaries, threading rules, coordinate systems, platform requirements, and third-party dependencies that become part of the engine architecture.

## Code rules

- Use C++23 for engine code.
- Keep platform code behind platform interfaces.
- Keep game-specific code outside engine modules.
- Prefer composition over genre-specific inheritance trees.
- Do not expose third-party library types through VICE public APIs.
- Treat performance budgets as part of feature design.
- Add tests for behavior that can regress.

## Documentation rules

Write like an engineer explaining a decision to another engineer.

- Use plain human language.
- Do not use em dashes.
- Do not add promotional filler.
- State tradeoffs and unresolved questions directly.
- Do not present guesses as established facts.

## Build

VICE requires CMake 3.28 or newer and a C++23 compiler.

With Ninja installed:

```sh
cmake --preset dev
cmake --build --preset dev
ctest --preset dev
```

You can also use CMake without presets:

```sh
cmake -S . -B build -DVICE_BUILD_TESTS=ON -DVICE_BUILD_LAB=ON
cmake --build build --config Debug --parallel
ctest --test-dir build -C Debug --output-on-failure
```

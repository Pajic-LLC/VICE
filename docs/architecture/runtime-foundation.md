# Runtime foundation

The runtime foundation provides services that every larger VICE system can use without taking a dependency on the renderer, editor, scripting runtime, or platform SDK.

## Logging

The first logging API is synchronous and deliberately small. It supports severity filtering and a replaceable sink. The default sink writes readable development output to the process streams.

The sink is copied under a lock and called after the lock is released. A sink may therefore log again without deadlocking the logger state.

The current API copies each accepted message into a log record. This favors safe ownership and simple tooling integration over maximum throughput. High-volume telemetry can use a separate buffered path later.

## Assertions and fatal errors

`VICE_ASSERT` is active in debug builds and compiles out in release builds. It is for programmer errors and broken invariants.

`panic` is always active. It records a fatal diagnostic when possible and terminates the process.

Recoverable game and engine errors must not use assertions as normal control flow.

## Time

Simulation and frame timing use a monotonic clock. Wall-clock time is a separate concern and should not drive simulation.

`FrameClock` tracks frame delta, total elapsed time, and frame index. Tests can supply explicit time points, which keeps timing behavior deterministic without sleeping.

## Build metadata

Core exposes the current build configuration, compiler family, and processor architecture. This data will later be included in crash context, profiler captures, and bug reports.

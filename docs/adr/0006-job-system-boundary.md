# ADR 0006: Job system boundary

Status: Accepted

## Context

VICE will run rendering preparation, simulation, streaming, animation, agents, audio, asset work, and editor tasks concurrently. Letting each subsystem create and manage its own threads would make frame budgeting, shutdown, profiling, and dependency ordering harder to control.

The engine also needs work that must execute on the application thread because some platform and editor operations have thread affinity.

## Decision

VICE owns a scheduler API in `VICE::Jobs`.

The API provides task handles, dependencies, priorities, worker and main-thread lanes, waiting, deterministic draining on shutdown, and instrumentation events.

The first implementation uses standard C++ threads, condition variables, shared task state, and central priority queues. These are implementation details. Engine systems depend on the VICE contract rather than the queue implementation.

A worker that waits on another job from the same scheduler helps execute ready worker work. The owner thread pumps main-thread work while waiting.

Jobs become runnable after every dependency reaches either success or failure. The scheduler reports task failure but does not impose subsystem-specific cancellation policy.

## Consequences

Subsystems have one scheduling and instrumentation boundary from the start.

The first implementation favors clear semantics and test coverage over a premature lock-free design. Work stealing, fibers, specialized allocators, and task-local storage remain possible later without changing the public role of the scheduler.

Main-thread jobs require the engine loop to pump the main lane regularly.

Scheduler shutdown belongs on the thread that created the scheduler so main-thread work can be drained before worker threads are joined.

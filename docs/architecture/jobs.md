# Job system

VICE has one scheduling boundary for engine and game work. Rendering, simulation, streaming, animation, agents, audio, and tools should schedule through this boundary instead of creating subsystem-specific worker threads.

## Scheduler ownership

A scheduler records the thread that creates it as its main thread. Main-thread jobs only run when that thread calls `pump_main` or waits through the scheduler. The engine loop will pump this lane every frame.

The default worker count leaves one hardware thread for the main thread when possible. Projects can set an explicit worker count for tests, tools, or platform-specific tuning.

The scheduler must be shut down from its owner thread. Normal engine lifetime already follows this rule.

## Jobs and dependencies

A job has a function, priority, execution lane, optional name, and zero or more dependencies. Dependencies must be handles created by the same scheduler.

Dependencies refer only to jobs that already exist. This keeps the submitted dependency graph acyclic by construction. A dependent job becomes runnable after every dependency reaches a terminal state.

A failed dependency still satisfies the scheduling dependency. Failure policy belongs to the subsystem that owns the work. The generic scheduler does not silently cancel downstream jobs.

## Waiting

Waiting is cooperative where it matters.

The scheduler owner thread pumps ready main-thread jobs while waiting. A worker thread that waits on another job helps execute ready worker jobs from the same scheduler. This avoids the common one-worker nested-wait deadlock and reduces wasted worker time.

Threads that do not belong to the scheduler sleep on completion notification instead of spinning.

Dependencies are preferred over waits for frame and simulation graphs because dependencies expose more parallel work to the scheduler.

## Priorities

VICE currently exposes four priorities: low, normal, high, and critical. Ready jobs are selected from the highest available priority first. FIFO order is preserved inside one priority queue.

Priority is a scheduling hint, not a real-time guarantee. Long-running jobs can still delay later work. Smaller jobs and dependency graphs are preferred for latency-sensitive systems.

## Failure

Task functions are allowed to throw. The scheduler catches exceptions at the worker boundary, marks the job failed, wakes waiters, releases dependents, and keeps the worker alive.

The current public API reports failure status but does not expose the captured exception. Subsystems should communicate expected errors through their own result objects rather than exceptions.

## Instrumentation

`set_event_sink` installs a development hook for submitted, started, completed, and failed events. Callbacks can run from any scheduler thread and are never called while the scheduler queue or dependency locks are held.

This hook is intended for the VICE profiler, task graph visualization, and tests. It is not a gameplay event system.

## Thread safety

Shared scheduler state uses mutexes, condition variables, and atomics. Normal idle workers sleep on a condition variable. The implementation does not use a normal-load busy loop.

The job tests cover parallel batches, dependency chains, fan-out and fan-in, main-thread work, priority ordering, nested worker waits, and task failure.

For local race testing with Apple Clang, configure a separate debug build with ThreadSanitizer flags and run `vice_jobs_tests`. ThreadSanitizer is a development check, not a shipping configuration.

## Current limits

This first implementation uses shared task state and central priority queues. It establishes semantics before lock-free queues, work stealing, task arenas, fiber waits, or allocator specialization are justified by profiling.

The public API must stay stable enough that the scheduler internals can change without forcing rendering, physics, AI, or game code to change with them.

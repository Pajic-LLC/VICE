# World scheduler

[ADR 0029](../adr/0029-persistent-world-scheduler.md) defines persistence, ordering, catch-up, failure and performance semantics.

`World::schedule_event(event, due_ticks, interval_ticks, catch_up, optional_id)` validates a normal typed event template and returns a persistent schedule ID. Interval zero is one-shot. A caller-supplied ID supports authored identity; a nil ID generates one. IDs are unique among live schedule records. Cancellation frees pending or failed work and its schema usage. Removing a one-shot or cancelled record does not revoke evidence already emitted under that UUID.

Clock advancement dispatches at most 256 due records, ordered by next deadline then UUID bytes. `dispatch_schedules(budget)` can drain another batch without advancing time. Automatic clock pause stops ordinary dispatch; explicit dispatch/exact stepping remains gameplay authority. Due-at-creation work waits for the next dispatch. Restore never executes it.

Replay preserves every occurrence over bounded batches. Coalesce represents all currently missed intervals in one emitted event. Firing provenance reports schedule ID, final due tick, cumulative occurrence and represented count. Actual event time is execution time, which may be later than due time. Recurring cadence is preserved across time jumps, saves and restarts. A deleted reference or rejected event leaves inspectable failed work; cancel/recreate explicitly to repair it. Overflow never wraps time or occurrence counters.

Query with `schedule(id)`, `scheduler_info()`, `schedule_page(after,limit)` or `schedule_context(endpoint,budget)`. Summary is O(1), indexed mutations O(log n), pages at most 64. Endpoint debug context scans up to the fixed 16384-record capacity on demand. Runtime `world_schedules` contains summary/templates, and fired event JSON carries `schedule` provenance. Failure names distinguish rejected event, time overflow and occurrence overflow; the event-result name explains rejected emission. Pruned parent UUIDs remain available as captured evidence.

Source record and edit creation share this data grammar:

```
world_schedule ID DUE INTERVAL OCCURRENCE CATCH_UP FAILURE EVENT_RESULT RETAINED SUBJECT TARGET OBJECT REGION PAYLOAD PARENTS
```

Optional target/object IDs use `none`, unspecified region uses 0. Catch-up wire values are 0 replay / 1 coalesce. Failure is 0 pending / 1 event rejected / 2 next-time overflow / 3 occurrence overflow. `PAYLOAD PARENTS` is the existing event structured codec. Revision-safe edit `schedule_event` uses that record body but requires fresh occurrence/failure/result values of 0; `cancel_schedule ID` removes it. Source restore may represent failed or overdue work, while new creation requires live endpoint/region references and retained parents. See the workflow script for a complete transaction.

The presentation module registers its own `PulseFact` and preserves a recurring schedule across a physical walk/drive/save/restart. `scripts/validate_scheduler_workflow.py --run-runtime` checks cadence and original cause after a fresh process. `vice_world_schedule_tests` and `vice_schedule_runtime_tests` cover bounds, legacy persistence, ordering, stale IDs, pruned causes, pause, overflow, module guards and Editor parity.

## What changed

Describe the change and why it belongs in VICE.

## Architecture impact

State which engine boundaries, public APIs, data formats, or platform assumptions changed. Write `None` if there is no architecture impact.

## Validation

List the builds, tests, captures, benchmarks, or manual checks used to validate the change.

## Performance

State the expected CPU, GPU, memory, I/O, or build-time impact. Write `Not measured` when performance is not relevant yet.

## Checklist

- [ ] macOS remains a first-class target.
- [ ] Windows remains a first-class target.
- [ ] No game-specific assumption leaked into engine core.
- [ ] Expensive behavior can scale or be disabled where applicable.
- [ ] Tests cover new behavior where practical.
- [ ] Documentation is direct, specific, and contains no em dashes.

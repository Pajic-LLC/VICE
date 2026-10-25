# Reflection backend validation

These tests close the gap between Runtime reflection contract tests and backend ray-tracing RHI tests.

`vice_runtime_ray_reflection_backend_fallback_tests` runs on every configured backend and verifies that the production reflection pass remains a clean capability-driven no-op when no cooked reflection shader is supplied.

`vice_runtime_ray_reflection_backend_execution_tests` is built when the production reflection shader is cooked. It validates the cooked manifest and, when the selected device exposes hardware ray tracing, executes that exact production compute shader against a real BLAS/TLAS. The fixture verifies that `CommittedInstanceIndex()` resolves through the Runtime scene-index table to the authoritative scene instance and that the committed hit remains finite in the camera-relative frame.

The execution fixture also runs a small cooked validation probe after the production pass to read the RGBA16F reflection output through the normal compute RHI. With a known one-pixel environment fixture, it requires the raw replacement radiance to remain finite, non-negative, and equal to the expected environment contribution. The probe does not participate in Runtime rendering; it exists only to make production GPU output observable in the test.

A runner without hardware ray tracing reports an explicit skip after validating the production cooked manifest. This is evidence of shader/cook integration, not evidence of hardware ray execution.

The macOS production-shader execution gate remains dependent on real Metal Shader Converter provisioning in CI. Do not treat source fallback compilation as equivalent to a cooked Metal reflection execution.

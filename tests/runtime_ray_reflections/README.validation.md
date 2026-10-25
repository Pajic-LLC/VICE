# Reflection backend validation

These tests close the gap between Runtime reflection contract tests and backend ray-tracing RHI tests.

`vice_runtime_ray_reflection_backend_fallback_tests` runs on every configured backend and verifies that the production reflection pass remains a clean capability-driven no-op when no cooked reflection shader is supplied.

`vice_runtime_ray_reflection_backend_execution_tests` is built when the production reflection shader is cooked. It validates the cooked manifest and, when the selected device exposes hardware ray tracing, executes that exact production compute shader against a real BLAS/TLAS. The fixture verifies that `CommittedInstanceIndex()` resolves through the Runtime scene-index table to the authoritative scene instance and that the committed hit remains finite in the camera-relative frame.

The execution fixture also runs a small cooked validation probe after the production pass to read the RGBA16F reflection output through the normal compute RHI. With a known one-pixel environment fixture, it requires the raw replacement radiance to remain finite, non-negative, and equal to the expected environment contribution. The probe does not participate in Runtime rendering; it exists only to make production GPU output observable in the test.

`vice_runtime_ray_reflection_history_backend_execution_tests` executes the production temporal reconstruction pass with a committed hit, previous scene color/depth, and trillion-unit global origins. Its current and previous origins differ by `(3, 4, -5)`, and the current-camera-relative committed hit is chosen so applying that nonzero origin delta reconstructs the expected previous-camera-local position. The same validation probe requires the resolved GPU output to replace the current environment fallback with the expected previous-frame scene radiance while preserving the reflection replacement mask. This makes the large-world rebase an executed GPU contract rather than only a CPU/source assertion.

A runner without hardware ray tracing reports an explicit skip for the raw reflection RayQuery fixture after validating the production cooked manifest. The reflection-history execution test does not require hardware ray tracing and still executes the production reconstruction shader when the cooked shader targets are available.

The macOS production-shader execution gate remains dependent on real Metal Shader Converter provisioning in CI. Do not treat source fallback compilation as equivalent to a cooked Metal reflection execution.

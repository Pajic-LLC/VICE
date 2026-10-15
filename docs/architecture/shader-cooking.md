# Shader cooking

VICE uses HLSL as the shader authoring language.

## Pipeline

The production asset path is:

```text
HLSL
  |
  v
DXC
  |
  v
DXIL
  |----------------------> Direct3D 12
  |
  v
Metal Shader Converter
  |              |
  v              v
metallib     reflection JSON
  |
  v
Metal
```

The cooker executable is `vice_shader_cooker`.

## Example

```text
vice_shader_cooker \
  --input assets/shaders/vice_lab_triangle.hlsl \
  --output build/cooked/vice_lab_triangle_vs \
  --entry vice_triangle_vs \
  --stage vertex \
  --target all
```

The tool accepts explicit `--dxc` and `--metal-shader-converter` paths. The same paths can be supplied with `VICE_DXC` and `VICE_METAL_SHADER_CONVERTER`.

## Outputs

A successful cook writes:

- `shader.dxil`
- `shader.dxc.txt`
- `shader.metallib` when Metal is requested
- `shader.metal.json` when Metal is requested
- `shader.vice.json`

`shader.dxc.txt` is the compiler assembly listing used to extract logical HLSL resource reflection. It is kept beside the cooked binary so cache hits can recover the same reflection without recompiling.

`shader.metal.json` is emitted by Metal Shader Converter with `--output-reflection-file`. VICE treats it as backend-native metadata. It is not replaced by guessed argument-buffer offsets.

The manifest records the shader identity, compiler profile, cache key, tool versions, backend artifact names, and reflected resource bindings.

## Runtime loading

The RHI accepts cooked shaders through `ShaderBinaryDesc`.

The descriptor contains the shader stage, backend binary format, byte span, logical resource bindings, entry point, and debug name. It does not contain a file path.

Higher-level asset code reads the cooked artifact and passes its bytes and binding metadata into the RHI. The input spans only need to remain valid during module creation.

Direct3D 12 accepts `ShaderBinaryFormat::dxil`. Metal accepts `ShaderBinaryFormat::metallib`.

Runtime source compilation remains available as a development fallback while the asset layer is being connected.

## Logical binding identity

VICE uses the HLSL register class, binding index, and register space as the backend-neutral resource identity.

The register classes are:

- `constant_buffer` for `b` registers
- `shader_resource` for `t` registers
- `unordered_access` for `u` registers
- `sampler` for `s` registers

A reflected binding also records its resource count and whether the range is unbounded.

This keeps Direct3D 12 descriptor handles and Metal argument-buffer offsets out of game-facing code.

## Reflection extraction

DXC emits a `Resource Bindings` table in its assembly listing. The cooker parses that compiler output after a successful compile.

The parser records:

- resource name
- register class
- binding index
- register space
- count or unbounded range
- DXC resource type
- DXC format
- DXC dimension

Reflection failure fails the cook. A shader must not produce a binary with missing or ambiguous logical resource metadata.

When Metal is requested, Metal Shader Converter also writes `shader.metal.json`. The manifest records that artifact in a versioned `metal_binding` section. The converter file is the source of backend resource locations for Metal runtime binding.

VICE does not calculate Metal argument-buffer offsets from HLSL register numbers. When the runtime moves to explicit root-signature translation, converter or runtime resource-location queries remain authoritative.

CI compiles a resource-using shader with the Windows SDK DXC and checks the generated manifest. This protects the DXC parser against compiler output changes. Metal converter output is only required on machines that actually perform a Metal cook.

The logical reflection object remains version 1. The shader manifest is version 3.

## Cache key

The cache key is SHA-256 over the inputs that affect generated shader code:

- source bytes
- entry point
- shader stage
- requested target
- shader model
- debug or release mode
- DXC version output
- Metal Shader Converter version output when Metal is requested

If the manifest contains the same key and all requested artifacts exist, the cooker reuses the result unless `--force` is passed.

For Metal targets, a valid cache entry requires both `shader.metallib` and `shader.metal.json`.

The shader asset cache seed is versioned. Manifest or binding metadata changes invalidate older cached output.

## Compiler policy

The default shader model is 6.0. This keeps the first VICE pipeline on a conservative baseline while the required profile remains explicit in every manifest.

Release build infrastructure should pin exact compiler packages. The cooker does not download compilers. Local development can point to installed tools.

DXC errors and Metal Shader Converter errors are captured and returned by the cooker. Shader build failures should keep the original compiler context.

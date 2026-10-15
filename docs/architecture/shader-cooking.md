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
  |              |
  |              v
  |       VICE binding normalizer
  |              |
  +--------------+
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

`shader.metal.json` is emitted by Metal Shader Converter with `--output-reflection-file`. VICE treats it as converter-native evidence and normalizes supported top-level argument-buffer locations into the main shader manifest.

The manifest records the shader identity, compiler profile, cache key, tool versions, backend artifact names, logical resource reflection, and normalized Metal resource locations.

## Runtime loading

The RHI accepts cooked shaders through `ShaderBinaryDesc`.

The descriptor contains the shader stage, backend binary format, byte span, logical resource bindings, entry point, debug name, and optional normalized Metal resource locations. It does not contain a file path.

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

## DXC reflection

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

## Metal reflection normalization

When Metal is requested, Metal Shader Converter writes `shader.metal.json`. VICE parses the converter's `TopLevelArgumentBuffer` entries and matches them against DXC logical reflection by register class, binding index, and register space.

The normalized manifest records:

- register class
- binding index
- register space
- top-level argument-buffer byte offset
- converter-reported byte size

The automatic layout descriptor entry size is recorded as 24 bytes, matching the three 64-bit fields used by Metal Shader Converter for buffers, textures, and samplers.

Normalization fails when:

- the converter omits a logical resource
- a converter location does not match DXC reflection
- locations overlap
- a location is misaligned or too small for its reflected array
- a logical resource is unbounded
- a sampler has no authoritative top-level location

VICE does not calculate Metal argument-buffer offsets from HLSL register numbers. It also does not treat an undocumented relationship between another converter field and a byte offset as stable API.

Automatic Metal layout is therefore only used for bounded resources. Bindless and unbounded resources will use explicit VICE root-signature conversion and converter or runtime resource-location queries.

## Manifest versions

The logical DXC reflection object remains version 1.

The normalized Metal binding object is version 2.

The shader manifest is version 4.

Schema changes also change the shader asset cache seed so older manifests cannot be mistaken for current output.

## CI

CI compiles a resource-using shader with the Windows SDK DXC and checks the generated manifest. This protects the DXC parser and the platform-neutral manifest schema against compiler and code changes.

The shader cooker unit tests use representative Metal Shader Converter reflection JSON to verify normalization, including arrays, sampler locations, missing sampler locations, and the automatic-layout rejection of unbounded resources.

A real Metal Shader Converter smoke cook should run on CI once the Apple converter package is installed in the build environment. Until then, Metal runtime code is compiled and tested on the macOS ARM64 job while converter JSON normalization is covered by deterministic unit fixtures.

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

For Metal targets, a valid cache entry requires both `shader.metallib` and `shader.metal.json`. The converter reflection is normalized again on a cache hit before the cached shader is accepted.

## Compiler policy

The default shader model is 6.0. This keeps the first VICE pipeline on a conservative baseline while the required profile remains explicit in every manifest.

Release build infrastructure should pin exact compiler packages. The cooker does not download compilers. Local development can point to installed tools.

DXC errors and Metal Shader Converter errors are captured and returned by the cooker. Shader build failures should keep the original compiler context.

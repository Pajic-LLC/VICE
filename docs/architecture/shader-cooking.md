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
  |
  v
metallib
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
- `shader.metallib` when Metal is requested
- `shader.vice.json`

The JSON file is the first VICE shader manifest. It records the shader identity, compiler profile, cache key, tool versions, and backend artifact names.

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

## Compiler policy

The default shader model is 6.0. This gives the first VICE pipeline a conservative baseline while keeping the profile explicit in every manifest.

Release build infrastructure should pin exact compiler packages. The cooker itself does not download compilers. Local development can point to installed tools.

DXC errors and Metal Shader Converter errors are captured and returned by the cooker. Shader build failures must remain actionable and should not be replaced by generic asset-pipeline errors.

## Reflection

The manifest already reserves a versioned reflection object. It currently contains an empty resource list because the first triangle has no bound resources and the RHI binding contract does not exist yet.

The binding API and actual compiler reflection should be designed together. The manifest version gives that work a stable place to land without changing how shader binaries are named or cached.

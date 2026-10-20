from pathlib import Path


def replace_once(path: str, old: str, new: str) -> None:
    p = Path(path)
    text = p.read_text()
    if new in text:
        return
    if old not in text:
        raise SystemExit(f"missing patch context in {path}: {old[:160]!r}")
    p.write_text(text.replace(old, new, 1))


# Public cooked-shader metadata: distinguish automatic inline descriptors from explicit table pointers.
replace_once(
    "engine/render/include/vice/render/rhi.hpp",
    '''struct MetalShaderResourceLocationDesc {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t argument_buffer_offset = 0;
    std::uint32_t size = 0;
};

[[nodiscard]] constexpr bool is_valid(const MetalShaderResourceLocationDesc& desc) noexcept {
    constexpr std::uint32_t descriptor_size = 3U * sizeof(std::uint64_t);
    return desc.argument_buffer_offset % alignof(std::uint64_t) == 0U &&
        desc.size >= descriptor_size && desc.size % descriptor_size == 0U;
}
''',
    '''enum class MetalShaderResourceBindingModel : std::uint8_t {
    automatic_linear,
    descriptor_table,
};

struct MetalShaderResourceLocationDesc {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t argument_buffer_offset = 0;
    std::uint32_t size = 0;
    MetalShaderResourceBindingModel model = MetalShaderResourceBindingModel::automatic_linear;
};

[[nodiscard]] constexpr bool is_valid(const MetalShaderResourceLocationDesc& desc) noexcept {
    constexpr std::uint32_t descriptor_size = 3U * sizeof(std::uint64_t);
    if (desc.argument_buffer_offset % alignof(std::uint64_t) != 0U) return false;
    switch (desc.model) {
    case MetalShaderResourceBindingModel::automatic_linear:
        return desc.size >= descriptor_size && desc.size % descriptor_size == 0U;
    case MetalShaderResourceBindingModel::descriptor_table:
        return desc.size == sizeof(std::uint64_t);
    }
    return false;
}
'''
)

replace_once(
    "tools/shader_cooker/include/vice/tools/metal_reflection.hpp",
    '''struct MetalResourceLocationReflection {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t argument_buffer_offset = 0;
    std::uint32_t size = 0;
};

struct MetalBindingReflection {
    bool valid = false;
    std::vector<MetalResourceLocationReflection> locations;
    std::string diagnostics;
};

[[nodiscard]] MetalBindingReflection parse_metal_shader_converter_reflection(
    std::string_view json,
    const ShaderReflection& logical_reflection
);
''',
    '''enum class MetalBindingModel : std::uint8_t {
    automatic_linear,
    explicit_descriptor_tables,
};

struct MetalResourceLocationReflection {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t argument_buffer_offset = 0;
    std::uint32_t size = 0;
    bool descriptor_table = false;
};

struct MetalBindingReflection {
    bool valid = false;
    MetalBindingModel model = MetalBindingModel::automatic_linear;
    std::vector<MetalResourceLocationReflection> locations;
    std::string diagnostics;
};

[[nodiscard]] MetalBindingReflection parse_metal_shader_converter_reflection(
    std::string_view json,
    const ShaderReflection& logical_reflection
);

[[nodiscard]] MetalBindingReflection build_explicit_metal_binding(
    const ShaderReflection& logical_reflection
);

[[nodiscard]] std::string build_metal_root_signature_json(
    ShaderStage stage,
    const ShaderReflection& logical_reflection
);
'''
)

# Extend Metal reflection normalization with a deterministic explicit descriptor-table layout.
reflection = Path("tools/shader_cooker/src/metal_reflection.cpp")
text = reflection.read_text()
text = text.replace(
    '''    MetalBindingReflection result;
    if (!logical_reflection.valid) {''',
    '''    MetalBindingReflection result;
    result.model = MetalBindingModel::automatic_linear;
    if (!logical_reflection.valid) {''',
    1,
)
marker = '''    result.valid = true;
    return result;
}

} // namespace vice::tools::shader_cooker
'''
addition = r'''    result.valid = true;
    return result;
}

MetalBindingReflection build_explicit_metal_binding(
    const ShaderReflection& logical_reflection
) {
    MetalBindingReflection result;
    result.model = MetalBindingModel::explicit_descriptor_tables;
    if (!logical_reflection.valid) {
        result.diagnostics = "Metal explicit layout requires valid DXC logical reflection.";
        return result;
    }
    if (logical_reflection.resources.size() >
        std::numeric_limits<std::uint32_t>::max() / sizeof(std::uint64_t)) {
        result.diagnostics = "Metal explicit layout has too many root parameters.";
        return result;
    }
    result.locations.reserve(logical_reflection.resources.size());
    for (std::size_t index = 0U; index < logical_reflection.resources.size(); ++index) {
        const auto& resource = logical_reflection.resources[index];
        result.locations.push_back(MetalResourceLocationReflection{
            .register_class = resource.register_class,
            .binding = resource.binding,
            .space = resource.space,
            .argument_buffer_offset = static_cast<std::uint32_t>(index * sizeof(std::uint64_t)),
            .size = static_cast<std::uint32_t>(sizeof(std::uint64_t)),
            .descriptor_table = true,
        });
    }
    result.valid = true;
    return result;
}

std::string build_metal_root_signature_json(
    const ShaderStage stage,
    const ShaderReflection& logical_reflection
) {
    if (!logical_reflection.valid) return {};
    const auto range_type = [](const ShaderRegisterClass register_class) -> std::string_view {
        switch (register_class) {
        case ShaderRegisterClass::constant_buffer: return "IRDescriptorRangeTypeCBV";
        case ShaderRegisterClass::shader_resource: return "IRDescriptorRangeTypeSRV";
        case ShaderRegisterClass::unordered_access: return "IRDescriptorRangeTypeUAV";
        case ShaderRegisterClass::sampler: return "IRDescriptorRangeTypeSampler";
        }
        return {};
    };
    const auto visibility = [](const ShaderStage shader_stage) -> std::string_view {
        switch (shader_stage) {
        case ShaderStage::vertex: return "IRShaderVisibilityVertex";
        case ShaderStage::fragment: return "IRShaderVisibilityPixel";
        case ShaderStage::compute: return "IRShaderVisibilityAll";
        }
        return "IRShaderVisibilityAll";
    };

    std::string json;
    json += "{\n  \"RootSignature\": {\n";
    json += "    \"Flags\": \"IRRootSignatureFlagNone\",\n";
    json += "    \"NumParameters\": " + std::to_string(logical_reflection.resources.size()) + ",\n";
    json += "    \"NumStaticSamplers\": 0,\n";
    json += "    \"Parameters\": [";
    if (!logical_reflection.resources.empty()) json += "\n";
    for (std::size_t index = 0U; index < logical_reflection.resources.size(); ++index) {
        const auto& resource = logical_reflection.resources[index];
        const std::uint32_t descriptor_count = resource.unbounded
            ? std::numeric_limits<std::uint32_t>::max()
            : resource.count;
        json += "      {\n";
        json += "        \"DescriptorTable\": {\n";
        json += "          \"DescriptorRanges\": [\n";
        json += "            {\n";
        json += "              \"BaseShaderRegister\": " + std::to_string(resource.binding) + ",\n";
        json += "              \"Flags\": \"IRDescriptorRangeFlagDescriptorsVolatile\",\n";
        json += "              \"NumDescriptors\": " + std::to_string(descriptor_count) + ",\n";
        json += "              \"OffsetInDescriptorsFromTableStart\": 0,\n";
        json += "              \"RangeType\": \"" + std::string{range_type(resource.register_class)} + "\",\n";
        json += "              \"RegisterSpace\": " + std::to_string(resource.space) + "\n";
        json += "            }\n";
        json += "          ],\n";
        json += "          \"NumDescriptorRanges\": 1\n";
        json += "        },\n";
        json += "        \"ParameterType\": \"IRRootParameterTypeDescriptorTable\",\n";
        json += "        \"ShaderVisibility\": \"" + std::string{visibility(stage)} + "\"\n";
        json += "      }";
        if (index + 1U != logical_reflection.resources.size()) json += ',';
        json += "\n";
    }
    json += "    ],\n    \"StaticSamplers\": []\n  },\n";
    json += "  \"version\": \"IRRootSignatureVersion_1_1\"\n}\n";
    return json;
}

} // namespace vice::tools::shader_cooker
'''
if addition not in text:
    if marker not in text:
        raise SystemExit("missing metal reflection tail")
    text = text.replace(marker, addition, 1)
reflection.write_text(text)

# Cooker V5: automatic layout for bounded shaders; explicit root-signature tables when any binding is unbounded.
cooker = Path("tools/shader_cooker/src/shader_cooker.cpp")
text = cooker.read_text()
text = text.replace('''        << "  \\\"version\\\": 4,\\n"''', '''        << "  \\\"version\\\": 5,\\n"''', 1)
text = text.replace('''        << "    \\\"version\\\": 2,\\n"
        << "    \\\"model\\\": ";
    if (metal_binding.has_value()) {
        manifest << "\\\"automatic_linear\\\",\\n";
    } else {''', '''        << "    \\\"version\\\": 3,\\n"
        << "    \\\"model\\\": ";
    if (metal_binding.has_value()) {
        manifest << (metal_binding->model == MetalBindingModel::explicit_descriptor_tables
            ? "\\\"explicit_descriptor_tables\\\",\\n"
            : "\\\"automatic_linear\\\",\\n");
    } else {''', 1)
text = text.replace('''                << "        \\\"size\\\": " << location.size << "\\n"''', '''                << "        \\\"size\\\": " << location.size << "\\n"''', 1)
text = text.replace('''    hash_append(material, "VICE_SHADER_ASSET_V4_COMPUTE_GROUPS1");''', '''    hash_append(material, "VICE_SHADER_ASSET_V5_METAL_EXPLICIT_TABLES1");''', 1)

cached_old = '''            if (result.metal_reflection_path.has_value()) {
                const MetalBindingReflection metal = parse_metal_shader_converter_reflection(
                    read_file(*result.metal_reflection_path),
                    result.reflection
                );
                if (!metal.valid) {
                    result.diagnostics =
                        "Cached Metal reflection normalization failed. " + metal.diagnostics;
                    return result;
                }
            }
'''
cached_new = '''            if (result.metal_reflection_path.has_value()) {
                const bool explicit_layout = std::any_of(
                    result.reflection.resources.begin(), result.reflection.resources.end(),
                    [](const auto& resource) { return resource.unbounded; }
                );
                const MetalBindingReflection metal = explicit_layout
                    ? build_explicit_metal_binding(result.reflection)
                    : parse_metal_shader_converter_reflection(
                        read_file(*result.metal_reflection_path), result.reflection
                    );
                if (!metal.valid) {
                    result.diagnostics =
                        "Cached Metal reflection normalization failed. " + metal.diagnostics;
                    return result;
                }
            }
'''
if cached_new not in text:
    if cached_old not in text:
        raise SystemExit("missing cached Metal reflection block")
    text = text.replace(cached_old, cached_new, 1)

convert_old = '''    std::optional<MetalBindingReflection> metal_binding;
    if (result.metallib_path.has_value() && result.metal_reflection_path.has_value()) {
        const auto converter_log = request.output_directory / "metal-shaderconverter.log";
        const std::string converter_command =
            shell_quote_path(toolchain.metal_shader_converter) + " " +
            shell_quote_path(result.dxil_path) + " -o " +
            shell_quote_path(*result.metallib_path) + " --output-reflection-file " +
            shell_quote_path(*result.metal_reflection_path);
        const ProcessResult converter_result = run_capture(converter_command, converter_log);
        if (converter_result.exit_code != 0 ||
            !std::filesystem::exists(*result.metallib_path, error) ||
            !std::filesystem::exists(*result.metal_reflection_path, error)) {
            result.diagnostics =
                "Metal Shader Converter failed.\\n" + converter_result.output;
            return result;
        }
        metal_binding = parse_metal_shader_converter_reflection(
            read_file(*result.metal_reflection_path),
            result.reflection
        );
        if (!metal_binding->valid) {
            result.diagnostics =
                "Metal Shader Converter reflection normalization failed. " +
                metal_binding->diagnostics;
            return result;
        }
    }
'''
convert_new = '''    std::optional<MetalBindingReflection> metal_binding;
    if (result.metallib_path.has_value() && result.metal_reflection_path.has_value()) {
        const bool explicit_layout = std::any_of(
            result.reflection.resources.begin(), result.reflection.resources.end(),
            [](const auto& resource) { return resource.unbounded; }
        );
        std::optional<std::filesystem::path> root_signature_path;
        if (explicit_layout) {
            root_signature_path = request.output_directory / "shader.metal.rootsig.json";
            const std::string root_signature = build_metal_root_signature_json(
                request.stage, result.reflection
            );
            if (root_signature.empty() || !write_file(*root_signature_path, root_signature)) {
                result.diagnostics = "Could not write Metal explicit root signature.";
                return result;
            }
        }

        const auto converter_log = request.output_directory / "metal-shaderconverter.log";
        std::string converter_command =
            shell_quote_path(toolchain.metal_shader_converter) + " " +
            shell_quote_path(result.dxil_path) + " -o " +
            shell_quote_path(*result.metallib_path) + " --output-reflection-file " +
            shell_quote_path(*result.metal_reflection_path);
        if (root_signature_path.has_value()) {
            converter_command += " --root-signature " + shell_quote_path(*root_signature_path);
        }
        const ProcessResult converter_result = run_capture(converter_command, converter_log);
        if (converter_result.exit_code != 0 ||
            !std::filesystem::exists(*result.metallib_path, error) ||
            !std::filesystem::exists(*result.metal_reflection_path, error)) {
            result.diagnostics =
                "Metal Shader Converter failed.\\n" + converter_result.output;
            return result;
        }
        metal_binding = explicit_layout
            ? build_explicit_metal_binding(result.reflection)
            : parse_metal_shader_converter_reflection(
                read_file(*result.metal_reflection_path), result.reflection
            );
        if (!metal_binding->valid) {
            result.diagnostics =
                "Metal Shader Converter reflection normalization failed. " +
                metal_binding->diagnostics;
            return result;
        }
    }
'''
if convert_new not in text:
    if convert_old not in text:
        raise SystemExit("missing Metal converter block")
    text = text.replace(convert_old, convert_new, 1)
cooker.write_text(text)

# Manifest loader: V4 automatic-linear compatibility + V5 explicit descriptor-table metadata.
asset = Path("engine/render/src/shader_asset.cpp")
text = asset.read_text()
text = text.replace('''    if (*version != 4U) {
        manifest.diagnostics = "Unsupported shader manifest version: " + std::to_string(*version) + ".";
        return manifest;
    }
''', '''    if (*version != 4U && *version != 5U) {
        manifest.diagnostics = "Unsupported shader manifest version: " + std::to_string(*version) + ".";
        return manifest;
    }
''', 1)
old = '''    const JsonValue* metal_binding = member(root, "metal_binding");
    if (metal_binding == nullptr || metal_binding->kind != JsonKind::object ||
        uint32_member(*metal_binding, "version") != 2U ||
        uint32_member(*metal_binding, "descriptor_entry_size") != 24U) {
        manifest.diagnostics = "Shader manifest Metal binding metadata is missing or unsupported.";
        return manifest;
    }
    const JsonValue* model = member(*metal_binding, "model");
    if (model == nullptr ||
        (model->kind != JsonKind::null_value &&
            (model->kind != JsonKind::string || model->string != "automatic_linear"))) {
        manifest.diagnostics = "Shader manifest contains an unsupported Metal binding model.";
        return manifest;
    }
'''
new = '''    const JsonValue* metal_binding = member(root, "metal_binding");
    const auto metal_binding_version = metal_binding != nullptr
        ? uint32_member(*metal_binding, "version") : std::nullopt;
    if (metal_binding == nullptr || metal_binding->kind != JsonKind::object ||
        !metal_binding_version.has_value() ||
        (*metal_binding_version != 2U && *metal_binding_version != 3U) ||
        uint32_member(*metal_binding, "descriptor_entry_size") != 24U) {
        manifest.diagnostics = "Shader manifest Metal binding metadata is missing or unsupported.";
        return manifest;
    }
    const JsonValue* model = member(*metal_binding, "model");
    const bool automatic_model = model != nullptr && model->kind == JsonKind::string &&
        model->string == "automatic_linear";
    const bool explicit_model = model != nullptr && model->kind == JsonKind::string &&
        model->string == "explicit_descriptor_tables";
    if (model == nullptr ||
        (model->kind != JsonKind::null_value && !automatic_model && !explicit_model) ||
        (explicit_model && (*version < 5U || *metal_binding_version < 3U))) {
        manifest.diagnostics = "Shader manifest contains an unsupported Metal binding model.";
        return manifest;
    }
'''
if new not in text:
    if old not in text:
        raise SystemExit("missing shader asset metal binding header")
    text = text.replace(old, new, 1)
text = text.replace('''        MetalShaderResourceLocationDesc location{
            .register_class = *register_class,
            .binding = *binding,
            .space = *space,
            .argument_buffer_offset = *offset,
            .size = *size,
        };
''', '''        MetalShaderResourceLocationDesc location{
            .register_class = *register_class,
            .binding = *binding,
            .space = *space,
            .argument_buffer_offset = *offset,
            .size = *size,
            .model = explicit_model
                ? MetalShaderResourceBindingModel::descriptor_table
                : MetalShaderResourceBindingModel::automatic_linear,
        };
''', 1)
text = text.replace('''        if (logical == manifest.resources.end() || logical->unbounded ||
            static_cast<std::uint64_t>(logical->count) * 24U > location.size) {
            manifest.diagnostics = "Shader manifest Metal location does not match logical reflection.";
            return manifest;
        }
''', '''        if (logical == manifest.resources.end() ||
            (automatic_model && (logical->unbounded ||
                static_cast<std::uint64_t>(logical->count) * 24U > location.size)) ||
            (explicit_model && location.size != sizeof(std::uint64_t))) {
            manifest.diagnostics = "Shader manifest Metal location does not match logical reflection.";
            return manifest;
        }
''', 1)
asset.write_text(text)

# Unit tests for explicit binding metadata and root-signature generation.
tests = Path("tests/shader_cooker_tests.cpp")
text = tests.read_text()
anchor = '''    CHECK(unbounded_metal.diagnostics.find("unbounded") != std::string::npos);

    CookRequest request;
'''
insert = '''    CHECK(unbounded_metal.diagnostics.find("unbounded") != std::string::npos);

    const MetalBindingReflection explicit_metal = build_explicit_metal_binding(reflection);
    CHECK(explicit_metal.valid);
    CHECK(explicit_metal.model == MetalBindingModel::explicit_descriptor_tables);
    CHECK(explicit_metal.locations.size() == reflection.resources.size());
    CHECK(explicit_metal.locations[0].argument_buffer_offset == 0U);
    CHECK(explicit_metal.locations[0].size == sizeof(std::uint64_t));
    CHECK(explicit_metal.locations[0].descriptor_table);
    CHECK(explicit_metal.locations[3].argument_buffer_offset == 3U * sizeof(std::uint64_t));
    CHECK(explicit_metal.locations[3].descriptor_table);

    const std::string explicit_root = build_metal_root_signature_json(
        ShaderStage::compute, reflection
    );
    CHECK(!explicit_root.empty());
    CHECK(explicit_root.find("IRRootSignatureVersion_1_1") != std::string::npos);
    CHECK(explicit_root.find("IRDescriptorRangeTypeUAV") != std::string::npos);
    CHECK(explicit_root.find("4294967295") != std::string::npos);
    CHECK(explicit_root.find("IRRootParameterTypeDescriptorTable") != std::string::npos);

    CookRequest request;
'''
if insert not in text:
    if anchor not in text:
        raise SystemExit("missing shader cooker test anchor")
    text = text.replace(anchor, insert, 1)
tests.write_text(text)

# Add explicit V5 parsing coverage without removing the V4 compatibility fixture.
tests = Path("tests/shader_asset_tests.cpp")
text = tests.read_text()
anchor = '''    CHECK(manifest.metal_resource_locations[1].size == 48U);

    std::string old_version{valid_manifest};
'''
insert = r'''    CHECK(manifest.metal_resource_locations[1].size == 48U);
    CHECK(manifest.metal_resource_locations[1].model ==
        MetalShaderResourceBindingModel::automatic_linear);

    std::string explicit_manifest{valid_manifest};
    auto replace_all = [&](const std::string_view from, const std::string_view to) {
        std::size_t position = 0U;
        while ((position = explicit_manifest.find(from, position)) != std::string::npos) {
            explicit_manifest.replace(position, from.size(), to);
            position += to.size();
        }
    };
    replace_all("\"version\": 4", "\"version\": 5");
    replace_all("\"version\": 2", "\"version\": 3");
    replace_all("\"automatic_linear\"", "\"explicit_descriptor_tables\"");
    replace_all("\"size\": 24", "\"size\": 8");
    replace_all("\"argument_buffer_offset\": 24", "\"argument_buffer_offset\": 8");
    replace_all("\"size\": 48", "\"size\": 8");
    replace_all("\"count\": 2,\n        \"unbounded\": false", "\"count\": 0,\n        \"unbounded\": true");
    const CookedShaderManifest explicit_parsed = parse_cooked_shader_manifest(explicit_manifest);
    CHECK(explicit_parsed.valid);
    CHECK(explicit_parsed.version == 5U);
    CHECK(explicit_parsed.resources[1].unbounded);
    CHECK(explicit_parsed.metal_resource_locations[1].model ==
        MetalShaderResourceBindingModel::descriptor_table);
    CHECK(explicit_parsed.metal_resource_locations[1].argument_buffer_offset == 8U);
    CHECK(explicit_parsed.metal_resource_locations[1].size == sizeof(std::uint64_t));

    std::string old_version{valid_manifest};
'''
if insert not in text:
    if anchor not in text:
        raise SystemExit("missing shader asset test anchor")
    text = text.replace(anchor, insert, 1)
tests.write_text(text)

# The cooker now emits V5 manifests; update only the CI expectations for newly cooked assets.
ci = Path(".github/workflows/ci.yml")
text = ci.read_text().replace("$labManifest.version -ne 4", "$labManifest.version -ne 5")
text = text.replace("$manifest.version -ne 4", "$manifest.version -ne 5")
text = text.replace("$manifest.metal_binding.version -ne 2", "$manifest.metal_binding.version -ne 3")
ci.write_text(text)

#include <vice/tools/metal_reflection.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::tools::shader_cooker {
namespace {

constexpr std::uint32_t descriptor_entry_size = 3U * sizeof(std::uint64_t);

struct JsonObjectView {
    std::string_view text;
};

[[nodiscard]] std::optional<std::string_view> array_contents(
    const std::string_view json,
    const std::string_view key
) noexcept {
    const std::string quoted_key = "\"" + std::string{key} + "\"";
    const std::size_t key_position = json.find(quoted_key);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t open = json.find('[', key_position + quoted_key.size());
    if (open == std::string_view::npos) {
        return std::nullopt;
    }

    bool in_string = false;
    bool escaped = false;
    std::size_t depth = 0;
    for (std::size_t index = open; index < json.size(); ++index) {
        const char character = json[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }
        if (character == '"') {
            in_string = true;
            continue;
        }
        if (character == '[') {
            ++depth;
        } else if (character == ']') {
            if (depth == 0U) {
                return std::nullopt;
            }
            --depth;
            if (depth == 0U) {
                return json.substr(open + 1U, index - open - 1U);
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<JsonObjectView> object_views(const std::string_view array) {
    std::vector<JsonObjectView> objects;
    bool in_string = false;
    bool escaped = false;
    std::size_t depth = 0;
    std::size_t object_start = std::string_view::npos;

    for (std::size_t index = 0; index < array.size(); ++index) {
        const char character = array[index];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }
        if (character == '"') {
            in_string = true;
            continue;
        }
        if (character == '{') {
            if (depth == 0U) {
                object_start = index;
            }
            ++depth;
        } else if (character == '}') {
            if (depth == 0U) {
                return {};
            }
            --depth;
            if (depth == 0U && object_start != std::string_view::npos) {
                objects.push_back(JsonObjectView{
                    array.substr(object_start, index - object_start + 1U)
                });
                object_start = std::string_view::npos;
            }
        }
    }
    if (depth != 0U) {
        return {};
    }
    return objects;
}

[[nodiscard]] std::optional<std::string_view> string_field(
    const JsonObjectView object,
    const std::string_view key
) noexcept {
    const std::string quoted_key = "\"" + std::string{key} + "\"";
    const std::size_t key_position = object.text.find(quoted_key);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t colon = object.text.find(':', key_position + quoted_key.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t quote = object.text.find('"', colon + 1U);
    if (quote == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t end = quote + 1U;
    bool escaped = false;
    for (; end < object.text.size(); ++end) {
        const char character = object.text[end];
        if (escaped) {
            escaped = false;
        } else if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return object.text.substr(quote + 1U, end - quote - 1U);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<std::uint32_t> uint_field(
    const JsonObjectView object,
    const std::string_view key
) noexcept {
    const std::string quoted_key = "\"" + std::string{key} + "\"";
    const std::size_t key_position = object.text.find(quoted_key);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t colon = object.text.find(':', key_position + quoted_key.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t begin = colon + 1U;
    while (begin < object.text.size() &&
        std::isspace(static_cast<unsigned char>(object.text[begin])) != 0) {
        ++begin;
    }
    if (begin == object.text.size()) {
        return std::nullopt;
    }
    std::size_t end = begin;
    while (end < object.text.size() &&
        std::isdigit(static_cast<unsigned char>(object.text[end])) != 0) {
        ++end;
    }
    if (end == begin) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const auto [parsed_end, error] = std::from_chars(
        object.text.data() + begin,
        object.text.data() + end,
        value
    );
    if (error != std::errc{} || parsed_end != object.text.data() + end) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<ShaderRegisterClass> register_class(
    const std::string_view type
) noexcept {
    if (type == "CBV") return ShaderRegisterClass::constant_buffer;
    if (type == "SRV") return ShaderRegisterClass::shader_resource;
    if (type == "UAV") return ShaderRegisterClass::unordered_access;
    if (type == "SMP") return ShaderRegisterClass::sampler;
    return std::nullopt;
}

[[nodiscard]] const ShaderResourceReflection* find_logical_resource(
    const ShaderReflection& reflection,
    const ShaderRegisterClass resource_class,
    const std::uint32_t binding,
    const std::uint32_t space
) noexcept {
    const auto found = std::find_if(
        reflection.resources.begin(),
        reflection.resources.end(),
        [&](const ShaderResourceReflection& resource) {
            return resource.register_class == resource_class &&
                resource.binding == binding && resource.space == space;
        }
    );
    return found == reflection.resources.end() ? nullptr : &*found;
}

[[nodiscard]] bool same_location_identity(
    const MetalResourceLocationReflection& left,
    const MetalResourceLocationReflection& right
) noexcept {
    return left.register_class == right.register_class &&
        left.binding == right.binding && left.space == right.space;
}

[[nodiscard]] MetalBindingReflection parse_binding_reflection(
    const std::string_view json,
    const ShaderReflection& logical_reflection,
    const MetalBindingModel model
) {
    MetalBindingReflection result;
    result.model = model;
    if (!logical_reflection.valid) {
        result.diagnostics = "Metal reflection requires valid DXC logical reflection.";
        return result;
    }
    if (model == MetalBindingModel::automatic_linear) {
        for (const auto& resource : logical_reflection.resources) {
            if (resource.unbounded) {
                result.diagnostics =
                    "Metal automatic resource layout does not support unbounded resource ranges. "
                    "Use the explicit root-signature conversion path.";
                return result;
            }
        }
    }
    if (logical_reflection.resources.empty()) {
        result.valid = true;
        return result;
    }

    const auto top_level = array_contents(json, "TopLevelArgumentBuffer");
    if (!top_level.has_value()) {
        result.diagnostics =
            "Metal Shader Converter reflection is missing TopLevelArgumentBuffer.";
        return result;
    }

    const auto objects = object_views(*top_level);
    if (objects.empty()) {
        result.diagnostics =
            "Metal Shader Converter reflection contains no top-level argument-buffer entries.";
        return result;
    }

    for (std::size_t object_index = 0U; object_index < objects.size(); ++object_index) {
        const JsonObjectView object = objects[object_index];
        const auto type = string_field(object, "Type");
        const auto offset = uint_field(object, "EltOffset");
        const auto size = uint_field(object, "Size");
        const auto slot = uint_field(object, "Slot");
        const auto space = uint_field(object, "Space");
        if (!type.has_value() || !offset.has_value() || !size.has_value()) {
            result.diagnostics =
                "Metal Shader Converter emitted an incomplete top-level resource location.";
            return result;
        }

        auto resource_class = register_class(*type);
        const ShaderResourceReflection* logical = nullptr;
        if (resource_class.has_value()) {
            if (!slot.has_value() || !space.has_value()) {
                result.diagnostics =
                    "Metal Shader Converter emitted an incomplete typed resource location.";
                return result;
            }
            logical = find_logical_resource(
                logical_reflection,
                *resource_class,
                *slot,
                *space
            );
        } else if (model == MetalBindingModel::explicit_descriptor_tables && *type == "Table") {
            if (object_index >= logical_reflection.resources.size()) {
                result.diagnostics =
                    "Metal Shader Converter emitted more descriptor-table roots than DXC logical resources.";
                return result;
            }
            logical = &logical_reflection.resources[object_index];
            resource_class = logical->register_class;
        } else {
            result.diagnostics =
                "Metal Shader Converter emitted an unsupported resource type: " +
                std::string{*type};
            return result;
        }

        if (logical == nullptr) {
            result.diagnostics =
                "Metal Shader Converter resource location does not match DXC logical reflection.";
            return result;
        }
        if (*offset % alignof(std::uint64_t) != 0U) {
            result.diagnostics = "Metal Shader Converter emitted a misaligned resource offset.";
            return result;
        }

        bool descriptor_table = false;
        if (model == MetalBindingModel::explicit_descriptor_tables) {
            if (*size != sizeof(std::uint64_t)) {
                result.diagnostics =
                    "Metal explicit descriptor-table root is not a 64-bit GPU-address entry.";
                return result;
            }
            descriptor_table = true;
        } else {
            const std::uint64_t required_size =
                static_cast<std::uint64_t>(logical->count) * descriptor_entry_size;
            if (*size < required_size || *size % descriptor_entry_size != 0U) {
                result.diagnostics =
                    "Metal Shader Converter resource location is too small for the reflected array.";
                return result;
            }
        }

        MetalResourceLocationReflection location{
            .register_class = *resource_class,
            .binding = logical->binding,
            .space = logical->space,
            .argument_buffer_offset = *offset,
            .size = *size,
            .descriptor_table = descriptor_table,
        };
        if (std::find_if(
                result.locations.begin(),
                result.locations.end(),
                [&](const MetalResourceLocationReflection& existing) {
                    return same_location_identity(existing, location);
                }
            ) != result.locations.end()) {
            result.diagnostics = "Metal Shader Converter emitted a duplicate resource location.";
            return result;
        }
        result.locations.push_back(location);
    }

    for (const auto& logical : logical_reflection.resources) {
        const auto found = std::find_if(
            result.locations.begin(),
            result.locations.end(),
            [&](const MetalResourceLocationReflection& location) {
                return location.register_class == logical.register_class &&
                    location.binding == logical.binding && location.space == logical.space;
            }
        );
        if (found == result.locations.end()) {
            result.diagnostics =
                logical.register_class == ShaderRegisterClass::sampler
                ? "Metal Shader Converter reflection did not provide an authoritative sampler offset."
                : "Metal Shader Converter reflection is missing a logical shader resource.";
            return result;
        }
    }

    std::sort(
        result.locations.begin(),
        result.locations.end(),
        [](const MetalResourceLocationReflection& left,
           const MetalResourceLocationReflection& right) {
            if (left.argument_buffer_offset != right.argument_buffer_offset) {
                return left.argument_buffer_offset < right.argument_buffer_offset;
            }
            if (left.register_class != right.register_class) {
                return left.register_class < right.register_class;
            }
            if (left.space != right.space) {
                return left.space < right.space;
            }
            return left.binding < right.binding;
        }
    );

    std::uint64_t previous_end = 0;
    for (const auto& location : result.locations) {
        if (location.argument_buffer_offset < previous_end) {
            result.diagnostics = "Metal Shader Converter resource locations overlap.";
            result.locations.clear();
            return result;
        }
        previous_end = static_cast<std::uint64_t>(location.argument_buffer_offset) + location.size;
        if (previous_end > std::numeric_limits<std::uint32_t>::max()) {
            result.diagnostics = "Metal argument-buffer layout exceeds VICE's current size limit.";
            result.locations.clear();
            return result;
        }
    }

    result.valid = true;
    return result;
}

} // namespace

MetalBindingReflection parse_metal_shader_converter_reflection(
    const std::string_view json,
    const ShaderReflection& logical_reflection
) {
    return parse_binding_reflection(
        json,
        logical_reflection,
        MetalBindingModel::automatic_linear
    );
}

MetalBindingReflection parse_explicit_metal_shader_converter_reflection(
    const std::string_view json,
    const ShaderReflection& logical_reflection
) {
    return parse_binding_reflection(
        json,
        logical_reflection,
        MetalBindingModel::explicit_descriptor_tables
    );
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

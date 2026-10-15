#include <vice/tools/shader_cooker.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::shader_cooker {
namespace {

inline constexpr std::uint32_t descriptor_size = 24U;

[[nodiscard]] std::optional<std::string_view> json_array(
    const std::string_view json,
    const std::string_view name
) noexcept {
    const std::string key = "\"" + std::string{name} + "\"";
    const std::size_t key_position = json.find(key);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t begin = json.find('[', key_position + key.size());
    if (begin == std::string_view::npos) {
        return std::nullopt;
    }

    bool in_string = false;
    bool escaped = false;
    std::uint32_t depth = 0;
    for (std::size_t index = begin; index < json.size(); ++index) {
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
                return json.substr(begin + 1U, index - begin - 1U);
            }
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::vector<std::string_view> json_objects(const std::string_view array) {
    std::vector<std::string_view> objects;
    bool in_string = false;
    bool escaped = false;
    std::uint32_t depth = 0;
    std::size_t object_begin = std::string_view::npos;
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
                object_begin = index;
            }
            ++depth;
        } else if (character == '}') {
            if (depth == 0U) {
                objects.clear();
                return objects;
            }
            --depth;
            if (depth == 0U && object_begin != std::string_view::npos) {
                objects.push_back(array.substr(object_begin, index - object_begin + 1U));
                object_begin = std::string_view::npos;
            }
        }
    }
    if (depth != 0U) {
        objects.clear();
    }
    return objects;
}

[[nodiscard]] std::optional<std::string_view> json_value_start(
    const std::string_view object,
    const std::string_view name
) noexcept {
    const std::string key = "\"" + std::string{name} + "\"";
    const std::size_t key_position = object.find(key);
    if (key_position == std::string_view::npos) {
        return std::nullopt;
    }
    const std::size_t colon = object.find(':', key_position + key.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }
    std::size_t value = colon + 1U;
    while (value < object.size() &&
        std::isspace(static_cast<unsigned char>(object[value])) != 0) {
        ++value;
    }
    return object.substr(value);
}

[[nodiscard]] std::optional<std::uint32_t> json_uint32(
    const std::string_view object,
    const std::string_view name
) noexcept {
    const auto value = json_value_start(object, name);
    if (!value.has_value() || value->empty()) {
        return std::nullopt;
    }
    std::uint64_t parsed = 0;
    const char* begin = value->data();
    const char* end = begin;
    while (end != value->data() + value->size() &&
        std::isdigit(static_cast<unsigned char>(*end)) != 0) {
        ++end;
    }
    if (begin == end) {
        return std::nullopt;
    }
    const auto [parsed_end, error] = std::from_chars(begin, end, parsed);
    if (error != std::errc{} || parsed_end != end ||
        parsed > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(parsed);
}

[[nodiscard]] std::optional<std::string_view> json_string(
    const std::string_view object,
    const std::string_view name
) noexcept {
    const auto value = json_value_start(object, name);
    if (!value.has_value() || value->size() < 2U || (*value)[0] != '"') {
        return std::nullopt;
    }
    bool escaped = false;
    for (std::size_t index = 1U; index < value->size(); ++index) {
        const char character = (*value)[index];
        if (escaped) {
            escaped = false;
            continue;
        }
        if (character == '\\') {
            escaped = true;
        } else if (character == '"') {
            return value->substr(1U, index - 1U);
        }
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<ShaderRegisterClass> register_class_from_metal_type(
    const std::string_view type
) noexcept {
    if (type == "CBV") {
        return ShaderRegisterClass::constant_buffer;
    }
    if (type == "SRV") {
        return ShaderRegisterClass::shader_resource;
    }
    if (type == "UAV") {
        return ShaderRegisterClass::unordered_access;
    }
    if (type == "SMP") {
        return ShaderRegisterClass::sampler;
    }
    return std::nullopt;
}

[[nodiscard]] bool same_identity(
    const ShaderResourceReflection& logical,
    const MetalResourceLocation& location
) noexcept {
    return logical.register_class == location.register_class &&
        logical.binding == location.binding && logical.space == location.space;
}

} // namespace

MetalBindingReflection parse_metal_resource_locations(
    const std::string_view converter_json,
    const std::span<const ShaderResourceReflection> logical_resources
) {
    MetalBindingReflection result;

    for (const auto& logical : logical_resources) {
        if (logical.unbounded) {
            result.diagnostics =
                "Metal automatic resource layout does not support unbounded ranges. "
                "Use the explicit root-signature path.";
            return result;
        }
    }

    if (logical_resources.empty()) {
        result.valid = true;
        return result;
    }

    const auto array = json_array(converter_json, "TopLevelArgumentBuffer");
    if (!array.has_value()) {
        result.diagnostics =
            "Metal Shader Converter reflection is missing TopLevelArgumentBuffer.";
        return result;
    }

    const auto objects = json_objects(*array);
    if (objects.empty()) {
        result.diagnostics =
            "Metal Shader Converter reflection contains no top-level resource locations.";
        return result;
    }

    for (const std::string_view object : objects) {
        const auto type = json_string(object, "Type");
        const auto binding = json_uint32(object, "Slot");
        const auto space = json_uint32(object, "Space");
        const auto offset = json_uint32(object, "EltOffset");
        const auto size = json_uint32(object, "Size");
        if (!type.has_value() || !binding.has_value() || !space.has_value() ||
            !offset.has_value() || !size.has_value()) {
            result.diagnostics =
                "Metal Shader Converter emitted a malformed top-level resource location.";
            return result;
        }
        const auto register_class = register_class_from_metal_type(*type);
        if (!register_class.has_value()) {
            continue;
        }
        if ((*offset % 8U) != 0U || *size == 0U || (*size % descriptor_size) != 0U) {
            result.diagnostics =
                "Metal Shader Converter emitted an invalid descriptor offset or size.";
            return result;
        }
        result.resources.push_back(MetalResourceLocation{
            .register_class = *register_class,
            .binding = *binding,
            .space = *space,
            .top_level_offset = *offset,
            .size_bytes = *size,
        });
    }

    for (const auto& logical : logical_resources) {
        const auto first = std::find_if(
            result.resources.begin(),
            result.resources.end(),
            [&](const MetalResourceLocation& location) {
                return same_identity(logical, location);
            }
        );
        if (first == result.resources.end()) {
            result.diagnostics =
                "Metal Shader Converter reflection is missing a logical shader resource location.";
            return result;
        }
        const auto duplicate = std::find_if(
            first + 1,
            result.resources.end(),
            [&](const MetalResourceLocation& location) {
                return same_identity(logical, location);
            }
        );
        if (duplicate != result.resources.end()) {
            result.diagnostics =
                "Metal Shader Converter reflection contains duplicate resource locations.";
            return result;
        }
        const std::uint64_t required =
            static_cast<std::uint64_t>(logical.count) * descriptor_size;
        if (first->size_bytes < required) {
            result.diagnostics =
                "Metal Shader Converter resource location is smaller than the reflected array.";
            return result;
        }
    }

    result.resources.erase(
        std::remove_if(
            result.resources.begin(),
            result.resources.end(),
            [&](const MetalResourceLocation& location) {
                return std::none_of(
                    logical_resources.begin(),
                    logical_resources.end(),
                    [&](const ShaderResourceReflection& logical) {
                        return same_identity(logical, location);
                    }
                );
            }
        ),
        result.resources.end()
    );

    std::sort(
        result.resources.begin(),
        result.resources.end(),
        [](const MetalResourceLocation& left, const MetalResourceLocation& right) {
            if (left.top_level_offset != right.top_level_offset) {
                return left.top_level_offset < right.top_level_offset;
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

    for (std::size_t index = 1U; index < result.resources.size(); ++index) {
        const auto& previous = result.resources[index - 1U];
        const auto& current = result.resources[index];
        const std::uint64_t previous_end =
            static_cast<std::uint64_t>(previous.top_level_offset) + previous.size_bytes;
        if (previous_end > current.top_level_offset) {
            result.diagnostics =
                "Metal Shader Converter resource locations overlap.";
            result.resources.clear();
            return result;
        }
    }

    result.valid = true;
    return result;
}

} // namespace vice::tools::shader_cooker

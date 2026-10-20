#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vice::render {
namespace {

enum class JsonKind : std::uint8_t {
    null_value,
    boolean,
    number,
    string,
    array,
    object,
};

struct JsonValue {
    JsonKind kind = JsonKind::null_value;
    bool boolean = false;
    std::uint64_t number = 0;
    std::string string;
    std::vector<JsonValue> array;
    std::vector<std::pair<std::string, JsonValue>> object;
};

class JsonParser {
public:
    explicit JsonParser(const std::string_view source) : source_{source} {}

    [[nodiscard]] bool parse(JsonValue& value) {
        skip_whitespace();
        if (!parse_value(value)) {
            return false;
        }
        skip_whitespace();
        if (position_ != source_.size()) {
            return fail("JSON contains trailing data.");
        }
        return true;
    }

    [[nodiscard]] const std::string& diagnostics() const noexcept {
        return diagnostics_;
    }

private:
    [[nodiscard]] bool parse_value(JsonValue& value) {
        if (position_ >= source_.size()) {
            return fail("JSON ended before a value was complete.");
        }
        switch (source_[position_]) {
        case '{': return parse_object(value);
        case '[': return parse_array(value);
        case '"':
            value.kind = JsonKind::string;
            return parse_string(value.string);
        case 't':
            if (!consume_literal("true")) return false;
            value.kind = JsonKind::boolean;
            value.boolean = true;
            return true;
        case 'f':
            if (!consume_literal("false")) return false;
            value.kind = JsonKind::boolean;
            value.boolean = false;
            return true;
        case 'n':
            if (!consume_literal("null")) return false;
            value.kind = JsonKind::null_value;
            return true;
        default:
            if (std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
                return parse_number(value);
            }
            return fail("JSON contains an unsupported value.");
        }
    }

    [[nodiscard]] bool parse_object(JsonValue& value) {
        ++position_;
        value.kind = JsonKind::object;
        skip_whitespace();
        if (consume('}')) {
            return true;
        }
        while (position_ < source_.size()) {
            std::string key;
            if (!parse_string(key)) {
                return false;
            }
            if (std::find_if(
                    value.object.begin(),
                    value.object.end(),
                    [&](const auto& field) { return field.first == key; }
                ) != value.object.end()) {
                return fail("JSON object contains a duplicate key.");
            }
            skip_whitespace();
            if (!consume(':')) {
                return fail("JSON object is missing a colon.");
            }
            skip_whitespace();
            JsonValue member_value;
            if (!parse_value(member_value)) {
                return false;
            }
            value.object.emplace_back(std::move(key), std::move(member_value));
            skip_whitespace();
            if (consume('}')) {
                return true;
            }
            if (!consume(',')) {
                return fail("JSON object is missing a comma.");
            }
            skip_whitespace();
        }
        return fail("JSON object was not closed.");
    }

    [[nodiscard]] bool parse_array(JsonValue& value) {
        ++position_;
        value.kind = JsonKind::array;
        skip_whitespace();
        if (consume(']')) {
            return true;
        }
        while (position_ < source_.size()) {
            JsonValue element;
            if (!parse_value(element)) {
                return false;
            }
            value.array.push_back(std::move(element));
            skip_whitespace();
            if (consume(']')) {
                return true;
            }
            if (!consume(',')) {
                return fail("JSON array is missing a comma.");
            }
            skip_whitespace();
        }
        return fail("JSON array was not closed.");
    }

    [[nodiscard]] bool parse_string(std::string& value) {
        if (!consume('"')) {
            return fail("JSON string is missing its opening quote.");
        }
        while (position_ < source_.size()) {
            const char character = source_[position_++];
            if (character == '"') {
                return true;
            }
            if (static_cast<unsigned char>(character) < 0x20U) {
                return fail("JSON string contains a control character.");
            }
            if (character != '\\') {
                value.push_back(character);
                continue;
            }
            if (position_ >= source_.size()) {
                return fail("JSON string ends inside an escape sequence.");
            }
            const char escaped = source_[position_++];
            switch (escaped) {
            case '"': value.push_back('"'); break;
            case '\\': value.push_back('\\'); break;
            case '/': value.push_back('/'); break;
            case 'b': value.push_back('\b'); break;
            case 'f': value.push_back('\f'); break;
            case 'n': value.push_back('\n'); break;
            case 'r': value.push_back('\r'); break;
            case 't': value.push_back('\t'); break;
            case 'u':
                if (!parse_unicode_escape(value)) {
                    return false;
                }
                break;
            default:
                return fail("JSON string contains an invalid escape sequence.");
            }
        }
        return fail("JSON string was not closed.");
    }

    [[nodiscard]] bool parse_unicode_escape(std::string& value) {
        if (position_ + 4U > source_.size()) {
            return fail("JSON unicode escape is incomplete.");
        }
        std::uint32_t code_point = 0;
        for (std::size_t index = 0; index < 4U; ++index) {
            const char digit = source_[position_++];
            code_point <<= 4U;
            if (digit >= '0' && digit <= '9') {
                code_point |= static_cast<std::uint32_t>(digit - '0');
            } else if (digit >= 'a' && digit <= 'f') {
                code_point |= static_cast<std::uint32_t>(digit - 'a' + 10);
            } else if (digit >= 'A' && digit <= 'F') {
                code_point |= static_cast<std::uint32_t>(digit - 'A' + 10);
            } else {
                return fail("JSON unicode escape contains a non-hex digit.");
            }
        }
        if (code_point >= 0xD800U && code_point <= 0xDFFFU) {
            return fail("JSON surrogate pairs are not supported in shader manifests.");
        }
        if (code_point <= 0x7FU) {
            value.push_back(static_cast<char>(code_point));
        } else if (code_point <= 0x7FFU) {
            value.push_back(static_cast<char>(0xC0U | (code_point >> 6U)));
            value.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        } else {
            value.push_back(static_cast<char>(0xE0U | (code_point >> 12U)));
            value.push_back(static_cast<char>(0x80U | ((code_point >> 6U) & 0x3FU)));
            value.push_back(static_cast<char>(0x80U | (code_point & 0x3FU)));
        }
        return true;
    }

    [[nodiscard]] bool parse_number(JsonValue& value) {
        const std::size_t begin = position_;
        while (position_ < source_.size() &&
            std::isdigit(static_cast<unsigned char>(source_[position_])) != 0) {
            ++position_;
        }
        if (position_ < source_.size() &&
            (source_[position_] == '.' || source_[position_] == 'e' || source_[position_] == 'E')) {
            return fail("Shader manifests only accept integer JSON numbers.");
        }
        std::uint64_t number = 0;
        const auto [end, error] = std::from_chars(
            source_.data() + begin,
            source_.data() + position_,
            number
        );
        if (error != std::errc{} || end != source_.data() + position_) {
            return fail("JSON integer is invalid or out of range.");
        }
        value.kind = JsonKind::number;
        value.number = number;
        return true;
    }

    [[nodiscard]] bool consume_literal(const std::string_view literal) {
        if (source_.substr(position_, literal.size()) != literal) {
            return fail("JSON literal is invalid.");
        }
        position_ += literal.size();
        return true;
    }

    [[nodiscard]] bool consume(const char character) noexcept {
        if (position_ >= source_.size() || source_[position_] != character) {
            return false;
        }
        ++position_;
        return true;
    }

    void skip_whitespace() noexcept {
        while (position_ < source_.size() &&
            std::isspace(static_cast<unsigned char>(source_[position_])) != 0) {
            ++position_;
        }
    }

    [[nodiscard]] bool fail(std::string message) {
        if (diagnostics_.empty()) {
            diagnostics_ = std::move(message) + " Byte " + std::to_string(position_) + ".";
        }
        return false;
    }

    std::string_view source_;
    std::size_t position_ = 0;
    std::string diagnostics_;
};

[[nodiscard]] const JsonValue* member(
    const JsonValue& object,
    const std::string_view key
) noexcept {
    if (object.kind != JsonKind::object) {
        return nullptr;
    }
    const auto found = std::find_if(
        object.object.begin(),
        object.object.end(),
        [&](const auto& field) { return field.first == key; }
    );
    return found == object.object.end() ? nullptr : &found->second;
}

[[nodiscard]] const std::string* string_member(
    const JsonValue& object,
    const std::string_view key
) noexcept {
    const JsonValue* value = member(object, key);
    return value != nullptr && value->kind == JsonKind::string ? &value->string : nullptr;
}

[[nodiscard]] std::optional<std::uint32_t> uint32_member(
    const JsonValue& object,
    const std::string_view key
) noexcept {
    const JsonValue* value = member(object, key);
    if (value == nullptr || value->kind != JsonKind::number ||
        value->number > std::numeric_limits<std::uint32_t>::max()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(value->number);
}

[[nodiscard]] std::optional<bool> bool_member(
    const JsonValue& object,
    const std::string_view key
) noexcept {
    const JsonValue* value = member(object, key);
    if (value == nullptr || value->kind != JsonKind::boolean) {
        return std::nullopt;
    }
    return value->boolean;
}

[[nodiscard]] bool is_null_member(const JsonValue& object, const std::string_view key) noexcept {
    const JsonValue* value = member(object, key);
    return value != nullptr && value->kind == JsonKind::null_value;
}

[[nodiscard]] std::optional<ShaderStage> parse_stage(const std::string_view value) noexcept {
    if (value == "vertex") return ShaderStage::vertex;
    if (value == "fragment") return ShaderStage::fragment;
    if (value == "compute") return ShaderStage::compute;
    return std::nullopt;
}

[[nodiscard]] std::optional<ShaderRegisterClass> parse_register_class(
    const std::string_view value
) noexcept {
    if (value == "constant_buffer") return ShaderRegisterClass::constant_buffer;
    if (value == "shader_resource") return ShaderRegisterClass::shader_resource;
    if (value == "unordered_access") return ShaderRegisterClass::unordered_access;
    if (value == "sampler") return ShaderRegisterClass::sampler;
    return std::nullopt;
}

[[nodiscard]] bool safe_artifact_name(const std::string_view value) noexcept {
    return !value.empty() && value != "." && value != ".." &&
        value.find('/') == std::string_view::npos &&
        value.find('\\') == std::string_view::npos;
}

[[nodiscard]] bool same_resource_identity(
    const ShaderResourceBindingDesc& left,
    const ShaderResourceBindingDesc& right
) noexcept {
    return left.register_class == right.register_class &&
        left.binding == right.binding && left.space == right.space;
}

[[nodiscard]] bool same_metal_identity(
    const MetalShaderResourceLocationDesc& left,
    const MetalShaderResourceLocationDesc& right
) noexcept {
    return left.register_class == right.register_class &&
        left.binding == right.binding && left.space == right.space;
}

[[nodiscard]] std::optional<std::string> read_text_file(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    constexpr std::uintmax_t maximum_size = 4U * 1024U * 1024U;
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        diagnostics = "Could not inspect shader manifest: " + error.message();
        return std::nullopt;
    }
    if (size == 0U || size > maximum_size) {
        diagnostics = "Shader manifest size is invalid.";
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        diagnostics = "Could not open shader manifest.";
        return std::nullopt;
    }
    std::string content(static_cast<std::size_t>(size), '\0');
    stream.read(content.data(), static_cast<std::streamsize>(content.size()));
    if (!stream) {
        diagnostics = "Could not read shader manifest.";
        return std::nullopt;
    }
    return content;
}

[[nodiscard]] std::optional<std::vector<std::byte>> read_binary_file(
    const std::filesystem::path& path,
    std::string& diagnostics
) {
    constexpr std::uintmax_t maximum_size = 128U * 1024U * 1024U;
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    if (error) {
        diagnostics = "Could not inspect cooked shader binary: " + error.message();
        return std::nullopt;
    }
    if (size == 0U || size > maximum_size ||
        size > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max())) {
        diagnostics = "Cooked shader binary size is invalid.";
        return std::nullopt;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        diagnostics = "Could not open cooked shader binary.";
        return std::nullopt;
    }
    std::vector<std::byte> content(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()));
    if (!stream) {
        diagnostics = "Could not read cooked shader binary.";
        return std::nullopt;
    }
    return content;
}

} // namespace

CookedShaderManifest parse_cooked_shader_manifest(const std::string_view json) {
    CookedShaderManifest manifest;
    JsonParser parser{json};
    JsonValue root;
    if (!parser.parse(root)) {
        manifest.diagnostics = parser.diagnostics();
        return manifest;
    }
    if (root.kind != JsonKind::object) {
        manifest.diagnostics = "Shader manifest root must be a JSON object.";
        return manifest;
    }

    const std::string* format = string_member(root, "format");
    const auto version = uint32_member(root, "version");
    const std::string* stage_name = string_member(root, "stage");
    const std::string* entry_point = string_member(root, "entry_point");
    if (format == nullptr || *format != "vice.shader" || !version.has_value() ||
        stage_name == nullptr || entry_point == nullptr || entry_point->empty()) {
        manifest.diagnostics = "Shader manifest identity fields are missing or invalid.";
        return manifest;
    }
    if (*version != 4U && *version != 5U) {
        manifest.diagnostics = "Unsupported shader manifest version: " + std::to_string(*version) + ".";
        return manifest;
    }
    const auto stage = parse_stage(*stage_name);
    if (!stage.has_value()) {
        manifest.diagnostics = "Shader manifest contains an unknown shader stage.";
        return manifest;
    }
    manifest.version = *version;
    manifest.stage = *stage;
    manifest.entry_point = *entry_point;

    const JsonValue* artifacts = member(root, "artifacts");
    if (artifacts == nullptr || artifacts->kind != JsonKind::object) {
        manifest.diagnostics = "Shader manifest is missing its artifacts object.";
        return manifest;
    }
    if (const std::string* dxil = string_member(*artifacts, "dxil"); dxil != nullptr) {
        if (!safe_artifact_name(*dxil)) {
            manifest.diagnostics = "Shader manifest contains an unsafe DXIL artifact path.";
            return manifest;
        }
        manifest.dxil_artifact = *dxil;
    } else if (!is_null_member(*artifacts, "dxil")) {
        manifest.diagnostics = "Shader manifest DXIL artifact must be a file name or null.";
        return manifest;
    }
    if (const std::string* metallib = string_member(*artifacts, "metallib"); metallib != nullptr) {
        if (!safe_artifact_name(*metallib)) {
            manifest.diagnostics = "Shader manifest contains an unsafe metallib artifact path.";
            return manifest;
        }
        manifest.metallib_artifact = *metallib;
    } else if (!is_null_member(*artifacts, "metallib")) {
        manifest.diagnostics = "Shader manifest metallib artifact must be a file name or null.";
        return manifest;
    }

    const JsonValue* reflection = member(root, "reflection");
    if (reflection == nullptr || reflection->kind != JsonKind::object ||
        uint32_member(*reflection, "version") != 1U) {
        manifest.diagnostics = "Shader manifest logical reflection is missing or unsupported.";
        return manifest;
    }

    const JsonValue* thread_group = member(*reflection, "thread_group_size");
    if (manifest.stage == ShaderStage::compute) {
        if (thread_group == nullptr || thread_group->kind != JsonKind::object) {
            manifest.diagnostics = "Compute shader manifest is missing thread-group reflection.";
            return manifest;
        }
        const auto x = uint32_member(*thread_group, "x");
        const auto y = uint32_member(*thread_group, "y");
        const auto z = uint32_member(*thread_group, "z");
        if (!x.has_value() || !y.has_value() || !z.has_value()) {
            manifest.diagnostics = "Compute shader thread-group reflection is incomplete.";
            return manifest;
        }
        const ComputeThreadGroupSize size{.x = *x, .y = *y, .z = *z};
        if (!is_valid(size)) {
            manifest.diagnostics = "Compute shader thread-group reflection is invalid.";
            return manifest;
        }
        manifest.compute_thread_group_size = size;
    } else if (thread_group != nullptr && thread_group->kind != JsonKind::null_value) {
        manifest.diagnostics = "Non-compute shader manifest contains compute thread-group reflection.";
        return manifest;
    }

    const JsonValue* resources = member(*reflection, "resources");
    if (resources == nullptr || resources->kind != JsonKind::array) {
        manifest.diagnostics = "Shader manifest reflection resources must be an array.";
        return manifest;
    }
    manifest.resources.reserve(resources->array.size());
    for (const JsonValue& item : resources->array) {
        if (item.kind != JsonKind::object) {
            manifest.diagnostics = "Shader manifest contains a non-object reflected resource.";
            return manifest;
        }
        const std::string* class_name = string_member(item, "class");
        const auto binding = uint32_member(item, "binding");
        const auto space = uint32_member(item, "space");
        const auto count = uint32_member(item, "count");
        const auto unbounded = bool_member(item, "unbounded");
        if (class_name == nullptr || !binding.has_value() || !space.has_value() ||
            !count.has_value() || !unbounded.has_value()) {
            manifest.diagnostics = "Shader manifest reflected resource is incomplete.";
            return manifest;
        }
        const auto register_class = parse_register_class(*class_name);
        if (!register_class.has_value()) {
            manifest.diagnostics = "Shader manifest contains an unknown resource class.";
            return manifest;
        }
        ShaderResourceBindingDesc resource{
            .register_class = *register_class,
            .binding = *binding,
            .space = *space,
            .count = *count,
            .unbounded = *unbounded,
        };
        if (!is_valid(resource)) {
            manifest.diagnostics = "Shader manifest contains an invalid reflected resource range.";
            return manifest;
        }
        if (std::find_if(
                manifest.resources.begin(),
                manifest.resources.end(),
                [&](const ShaderResourceBindingDesc& existing) {
                    return same_resource_identity(existing, resource);
                }
            ) != manifest.resources.end()) {
            manifest.diagnostics = "Shader manifest contains a duplicate reflected resource.";
            return manifest;
        }
        manifest.resources.push_back(resource);
    }

    const JsonValue* metal_binding = member(root, "metal_binding");
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
    const JsonValue* locations = member(*metal_binding, "locations");
    if (locations == nullptr || locations->kind != JsonKind::array) {
        manifest.diagnostics = "Shader manifest Metal locations must be an array.";
        return manifest;
    }
    if (model->kind == JsonKind::null_value && !locations->array.empty()) {
        manifest.diagnostics = "Shader manifest has Metal locations without a binding model.";
        return manifest;
    }
    manifest.metal_resource_locations.reserve(locations->array.size());
    for (const JsonValue& item : locations->array) {
        if (item.kind != JsonKind::object) {
            manifest.diagnostics = "Shader manifest contains a non-object Metal resource location.";
            return manifest;
        }
        const std::string* class_name = string_member(item, "class");
        const auto binding = uint32_member(item, "binding");
        const auto space = uint32_member(item, "space");
        const auto offset = uint32_member(item, "argument_buffer_offset");
        const auto size = uint32_member(item, "size");
        if (class_name == nullptr || !binding.has_value() || !space.has_value() ||
            !offset.has_value() || !size.has_value()) {
            manifest.diagnostics = "Shader manifest Metal resource location is incomplete.";
            return manifest;
        }
        const auto register_class = parse_register_class(*class_name);
        if (!register_class.has_value()) {
            manifest.diagnostics = "Shader manifest contains an unknown Metal resource class.";
            return manifest;
        }
        MetalShaderResourceLocationDesc location{
            .register_class = *register_class,
            .binding = *binding,
            .space = *space,
            .argument_buffer_offset = *offset,
            .size = *size,
            .model = explicit_model
                ? MetalShaderResourceBindingModel::descriptor_table
                : MetalShaderResourceBindingModel::automatic_linear,
        };
        if (!is_valid(location)) {
            manifest.diagnostics = "Shader manifest contains an invalid Metal resource location.";
            return manifest;
        }
        if (std::find_if(
                manifest.metal_resource_locations.begin(),
                manifest.metal_resource_locations.end(),
                [&](const MetalShaderResourceLocationDesc& existing) {
                    return same_metal_identity(existing, location);
                }
            ) != manifest.metal_resource_locations.end()) {
            manifest.diagnostics = "Shader manifest contains a duplicate Metal resource location.";
            return manifest;
        }
        const auto logical = std::find_if(
            manifest.resources.begin(),
            manifest.resources.end(),
            [&](const ShaderResourceBindingDesc& resource) {
                return location.register_class == resource.register_class &&
                    location.binding == resource.binding && location.space == resource.space;
            }
        );
        if (logical == manifest.resources.end() ||
            (automatic_model && (logical->unbounded ||
                static_cast<std::uint64_t>(logical->count) * 24U > location.size)) ||
            (explicit_model && location.size != sizeof(std::uint64_t))) {
            manifest.diagnostics = "Shader manifest Metal location does not match logical reflection.";
            return manifest;
        }
        manifest.metal_resource_locations.push_back(location);
    }

    if (!manifest.metallib_artifact.empty()) {
        if (model->kind != JsonKind::string) {
            manifest.diagnostics = "Metal shader artifact is missing its binding model.";
            return manifest;
        }
        if (manifest.resources.size() != manifest.metal_resource_locations.size()) {
            manifest.diagnostics = "Metal shader artifact is missing normalized resource locations.";
            return manifest;
        }
    } else if (model->kind != JsonKind::null_value || !manifest.metal_resource_locations.empty()) {
        manifest.diagnostics = "Metal binding metadata exists without a metallib artifact.";
        return manifest;
    }

    if (manifest.dxil_artifact.empty() && manifest.metallib_artifact.empty()) {
        manifest.diagnostics = "Shader manifest contains no loadable backend artifact.";
        return manifest;
    }

    manifest.valid = true;
    return manifest;
}

CookedShaderLoadResult load_cooked_shader(
    Device& device,
    const std::filesystem::path& manifest_path
) {
    CookedShaderLoadResult result;
    const auto text = read_text_file(manifest_path, result.diagnostics);
    if (!text.has_value()) {
        return result;
    }
    CookedShaderManifest manifest = parse_cooked_shader_manifest(*text);
    if (!manifest.valid) {
        result.diagnostics = manifest.diagnostics;
        return result;
    }
    result.compute_thread_group_size = manifest.compute_thread_group_size;

    std::string artifact_name;
    ShaderBinaryFormat format = ShaderBinaryFormat::dxil;
    std::span<const MetalShaderResourceLocationDesc> metal_locations;
    switch (device.backend()) {
    case Backend::direct3d12:
        artifact_name = manifest.dxil_artifact;
        format = ShaderBinaryFormat::dxil;
        break;
    case Backend::metal:
        artifact_name = manifest.metallib_artifact;
        format = ShaderBinaryFormat::metallib;
        metal_locations = manifest.metal_resource_locations;
        break;
    }
    if (artifact_name.empty()) {
        result.diagnostics = "Cooked shader does not contain an artifact for the active renderer.";
        return result;
    }

    const std::filesystem::path artifact_path = manifest_path.parent_path() / artifact_name;
    const auto binary = read_binary_file(artifact_path, result.diagnostics);
    if (!binary.has_value()) {
        return result;
    }

    const std::string debug_name = manifest_path.parent_path().filename().string();
    const ShaderBinaryDesc desc{
        .stage = manifest.stage,
        .format = format,
        .binary = std::span<const std::byte>{*binary},
        .resources = std::span<const ShaderResourceBindingDesc>{manifest.resources},
        .entry_point = manifest.entry_point,
        .debug_name = debug_name,
        .metal_resource_locations = metal_locations,
    };
    result.module = device.create_shader_module(desc);
    if (!result.module) {
        result.diagnostics = "Renderer rejected the cooked shader module.";
        return result;
    }
    return result;
}

} // namespace vice::render

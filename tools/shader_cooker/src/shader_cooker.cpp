#include <vice/tools/metal_reflection.hpp>
#include <vice/tools/shader_cooker.hpp>

#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <system_error>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace vice::tools::shader_cooker {
namespace {

[[nodiscard]] std::string read_file(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        return {};
    }
    std::ostringstream contents;
    contents << stream.rdbuf();
    return contents.str();
}

[[nodiscard]] bool write_file(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        return false;
    }
    stream.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    return static_cast<bool>(stream);
}

[[nodiscard]] std::string trim(std::string value) {
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())) != 0) {
        value.pop_back();
    }
    std::size_t start = 0;
    while (start < value.size() &&
        std::isspace(static_cast<unsigned char>(value[start])) != 0) {
        ++start;
    }
    return value.substr(start);
}

[[nodiscard]] std::string json_escape(std::string_view value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (const char character : value) {
        switch (character) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(character); break;
        }
    }
    return escaped;
}

[[nodiscard]] std::string shell_quote(std::string_view value) {
#ifdef _WIN32
    std::string quoted{"\""};
    for (const char character : value) {
        if (character == '"') {
            quoted += "\"\"";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('"');
    return quoted;
#else
    std::string quoted{"'"};
    for (const char character : value) {
        if (character == '\'') {
            quoted += "'\\''";
        } else {
            quoted.push_back(character);
        }
    }
    quoted.push_back('\'');
    return quoted;
#endif
}

[[nodiscard]] std::string shell_quote_path(const std::filesystem::path& path) {
    const std::string value = path.string();
    return shell_quote(std::string_view{value});
}

[[nodiscard]] std::optional<std::string> environment_value(const std::string_view name) {
    const std::string key{name};
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, key.c_str()) != 0 || value == nullptr) {
        return std::nullopt;
    }
    std::string result{value};
    std::free(value);
    if (result.empty()) {
        return std::nullopt;
    }
    return result;
#else
    const char* value = std::getenv(key.c_str());
    if (value == nullptr || value[0] == '\0') {
        return std::nullopt;
    }
    return std::string{value};
#endif
}

struct ProcessResult {
    int exit_code = -1;
    std::string output;
};

#ifdef _WIN32
[[nodiscard]] std::wstring utf8_to_wide(const std::string_view value) {
    if (value.empty() || value.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        return {};
    }
    const int input_length = static_cast<int>(value.size());
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        input_length,
        nullptr,
        0
    );
    if (required <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    const int converted = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        input_length,
        result.data(),
        required
    );
    if (converted != required) {
        return {};
    }
    return result;
}
#endif

[[nodiscard]] ProcessResult run_capture(
    const std::string& command,
    const std::filesystem::path& log_path
) {
#ifdef _WIN32
    SECURITY_ATTRIBUTES security{};
    security.nLength = sizeof(security);
    security.bInheritHandle = TRUE;

    HANDLE log_handle = CreateFileW(
        log_path.c_str(),
        GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        &security,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (log_handle == INVALID_HANDLE_VALUE) {
        return ProcessResult{-1, "Could not open shader tool log file."};
    }

    std::wstring command_line = utf8_to_wide(command);
    if (command_line.empty()) {
        CloseHandle(log_handle);
        return ProcessResult{-1, "Could not encode shader tool command line."};
    }

    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    startup.dwFlags = STARTF_USESTDHANDLES;
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = log_handle;
    startup.hStdError = log_handle;

    PROCESS_INFORMATION process{};
    const BOOL created = CreateProcessW(
        nullptr,
        command_line.data(),
        nullptr,
        nullptr,
        TRUE,
        CREATE_NO_WINDOW,
        nullptr,
        nullptr,
        &startup,
        &process
    );
    if (created == FALSE) {
        const DWORD error_code = GetLastError();
        CloseHandle(log_handle);
        return ProcessResult{
            -1,
            "Could not launch shader tool. Win32 error " + std::to_string(error_code) + "."
        };
    }

    const DWORD wait_result = WaitForSingleObject(process.hProcess, INFINITE);
    DWORD exit_code = 0;
    const BOOL have_exit_code = GetExitCodeProcess(process.hProcess, &exit_code);
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    CloseHandle(log_handle);

    if (wait_result != WAIT_OBJECT_0 || have_exit_code == FALSE ||
        exit_code > static_cast<DWORD>(std::numeric_limits<int>::max())) {
        return ProcessResult{-1, read_file(log_path)};
    }
    return ProcessResult{static_cast<int>(exit_code), read_file(log_path)};
#else
    const std::string redirected = command + " > " + shell_quote_path(log_path) + " 2>&1";
    const int result = std::system(redirected.c_str());
    return ProcessResult{result, read_file(log_path)};
#endif
}

[[nodiscard]] std::string tool_version(
    const std::filesystem::path& executable,
    const std::filesystem::path& scratch_directory
) {
    if (executable.empty()) {
        return {};
    }

    const auto log_path = scratch_directory / "tool-version.txt";
    auto result = run_capture(shell_quote_path(executable) + " --version", log_path);
    if (result.output.empty()) {
        result = run_capture(shell_quote_path(executable) + " -help", log_path);
    }

    std::string version = trim(result.output);
    constexpr std::size_t maximum_version_length = 1024;
    if (version.size() > maximum_version_length) {
        version.resize(maximum_version_length);
    }
    return version;
}

[[nodiscard]] std::string sha256(std::string_view value) {
    constexpr std::array<std::uint32_t, 64> constants{
        0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U,
        0x3956c25bU, 0x59f111f1U, 0x923f82a4U, 0xab1c5ed5U,
        0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U,
        0x72be5d74U, 0x80deb1feU, 0x9bdc06a7U, 0xc19bf174U,
        0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU,
        0x2de92c6fU, 0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU,
        0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
        0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U,
        0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU, 0x53380d13U,
        0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U,
        0xa2bfe8a1U, 0xa81a664bU, 0xc24b8b70U, 0xc76c51a3U,
        0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U,
        0x19a4c116U, 0x1e376c08U, 0x2748774cU, 0x34b0bcb5U,
        0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
        0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U,
        0x90befffaU, 0xa4506cebU, 0xbef9a3f7U, 0xc67178f2U
    };

    std::array<std::uint32_t, 8> state{
        0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
        0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U
    };

    std::string padded{value};
    const std::uint64_t bit_length = static_cast<std::uint64_t>(padded.size()) * 8ULL;
    padded.push_back(static_cast<char>(0x80));
    while ((padded.size() % 64U) != 56U) {
        padded.push_back('\0');
    }
    for (int shift = 56; shift >= 0; shift -= 8) {
        padded.push_back(static_cast<char>((bit_length >> shift) & 0xffULL));
    }

    std::array<std::uint32_t, 64> words{};
    for (std::size_t offset = 0; offset < padded.size(); offset += 64U) {
        for (std::size_t index = 0; index < 16U; ++index) {
            const std::size_t base = offset + index * 4U;
            words[index] =
                (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(padded[base])) << 24U) |
                (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(padded[base + 1U])) << 16U) |
                (static_cast<std::uint32_t>(
                    static_cast<unsigned char>(padded[base + 2U])) << 8U) |
                static_cast<std::uint32_t>(
                    static_cast<unsigned char>(padded[base + 3U]));
        }
        for (std::size_t index = 16U; index < 64U; ++index) {
            const std::uint32_t x = words[index - 15U];
            const std::uint32_t y = words[index - 2U];
            const std::uint32_t sigma0 =
                std::rotr(x, 7) ^ std::rotr(x, 18) ^ (x >> 3U);
            const std::uint32_t sigma1 =
                std::rotr(y, 17) ^ std::rotr(y, 19) ^ (y >> 10U);
            words[index] =
                words[index - 16U] + sigma0 + words[index - 7U] + sigma1;
        }

        std::uint32_t a = state[0];
        std::uint32_t b = state[1];
        std::uint32_t c = state[2];
        std::uint32_t d = state[3];
        std::uint32_t e = state[4];
        std::uint32_t f = state[5];
        std::uint32_t g = state[6];
        std::uint32_t h = state[7];

        for (std::size_t index = 0; index < 64U; ++index) {
            const std::uint32_t big_sigma1 =
                std::rotr(e, 6) ^ std::rotr(e, 11) ^ std::rotr(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temp1 =
                h + big_sigma1 + choice + constants[index] + words[index];
            const std::uint32_t big_sigma0 =
                std::rotr(a, 2) ^ std::rotr(a, 13) ^ std::rotr(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temp2 = big_sigma0 + majority;

            h = g;
            g = f;
            f = e;
            e = d + temp1;
            d = c;
            c = b;
            b = a;
            a = temp1 + temp2;
        }

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        state[4] += e;
        state[5] += f;
        state[6] += g;
        state[7] += h;
    }

    std::ostringstream output;
    output << std::hex << std::setfill('0');
    for (const std::uint32_t word : state) {
        output << std::setw(8) << word;
    }
    return output.str();
}

void hash_append(std::string& value, std::string_view field) {
    value.append(field);
    value.push_back('\0');
}

[[nodiscard]] bool needs_metal(const CookTarget target) noexcept {
    return target == CookTarget::metal || target == CookTarget::all;
}

[[nodiscard]] bool parse_uint32(const std::string_view text, std::uint32_t& value) noexcept {
    if (text.empty()) {
        return false;
    }
    const char* begin = text.data();
    const char* end = text.data() + text.size();
    const auto [parsed_end, error] = std::from_chars(begin, end, value);
    return error == std::errc{} && parsed_end == end;
}

struct BindingLocation {
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
};

[[nodiscard]] std::optional<BindingLocation> parse_binding_location(
    const std::string_view token
) noexcept {
    const std::size_t comma = token.find(',');
    const std::string_view register_token = token.substr(0, comma);
    const std::string_view space_token = comma == std::string_view::npos
        ? std::string_view{}
        : token.substr(comma + 1U);

    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::size_t prefix_length = 0;
    if (register_token.starts_with("cb")) {
        register_class = ShaderRegisterClass::constant_buffer;
        prefix_length = 2;
    } else if (register_token.starts_with('t')) {
        register_class = ShaderRegisterClass::shader_resource;
        prefix_length = 1;
    } else if (register_token.starts_with('u')) {
        register_class = ShaderRegisterClass::unordered_access;
        prefix_length = 1;
    } else if (register_token.starts_with('s')) {
        register_class = ShaderRegisterClass::sampler;
        prefix_length = 1;
    } else {
        return std::nullopt;
    }

    std::uint32_t binding = 0;
    if (!parse_uint32(register_token.substr(prefix_length), binding)) {
        return std::nullopt;
    }

    std::uint32_t space = 0;
    if (!space_token.empty()) {
        constexpr std::string_view prefix = "space";
        if (!space_token.starts_with(prefix) ||
            !parse_uint32(space_token.substr(prefix.size()), space)) {
            return std::nullopt;
        }
    }

    return BindingLocation{register_class, binding, space};
}

[[nodiscard]] std::vector<std::string> split_whitespace(const std::string& value) {
    std::istringstream stream{value};
    std::vector<std::string> fields;
    std::string field;
    while (stream >> field) {
        fields.push_back(std::move(field));
    }
    return fields;
}

[[nodiscard]] bool validate_compute_reflection(
    const ShaderStage stage,
    const ShaderReflection& reflection,
    std::string& diagnostics
) {
    if (stage == ShaderStage::compute) {
        if (!reflection.thread_group_size.has_value()) {
            diagnostics = "DXC compute reflection is missing NumThreads metadata.";
            return false;
        }
        const auto& size = *reflection.thread_group_size;
        if (size.x == 0U || size.y == 0U || size.z == 0U) {
            diagnostics = "DXC compute reflection contains an invalid zero thread-group dimension.";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::string build_manifest(
    const CookRequest& request,
    std::string_view cache_key,
    std::string_view dxc_version,
    std::string_view metal_converter_version,
    const std::filesystem::path& dxil_path,
    const std::filesystem::path& assembly_path,
    const std::optional<std::filesystem::path>& metallib_path,
    const std::optional<std::filesystem::path>& metal_reflection_path,
    const std::optional<MetalBindingReflection>& metal_binding,
    const ShaderReflection& reflection
) {
    std::ostringstream manifest;
    manifest
        << "{\n"
        << "  \"format\": \"vice.shader\",\n"
        << "  \"version\": 5,\n"
        << "  \"cache_key\": \"" << json_escape(cache_key) << "\",\n"
        << "  \"source\": \"" << json_escape(request.input.filename().string()) << "\",\n"
        << "  \"entry_point\": \"" << json_escape(request.entry_point) << "\",\n"
        << "  \"stage\": \"" << shader_stage_name(request.stage) << "\",\n"
        << "  \"profile\": \"" << json_escape(shader_profile(request.stage, request.shader_model)) << "\",\n"
        << "  \"target\": \"" << cook_target_name(request.target) << "\",\n"
        << "  \"debug\": " << (request.debug ? "true" : "false") << ",\n"
        << "  \"toolchain\": {\n"
        << "    \"dxc\": \"" << json_escape(dxc_version) << "\",\n"
        << "    \"metal_shader_converter\": \"" << json_escape(metal_converter_version) << "\"\n"
        << "  },\n"
        << "  \"artifacts\": {\n"
        << "    \"dxil\": \"" << json_escape(dxil_path.filename().string()) << "\",\n"
        << "    \"dxc_assembly\": \"" << json_escape(assembly_path.filename().string()) << "\",\n"
        << "    \"metallib\": ";
    if (metallib_path.has_value()) {
        manifest << "\"" << json_escape(metallib_path->filename().string()) << "\",\n";
    } else {
        manifest << "null,\n";
    }
    manifest << "    \"metal_reflection\": ";
    if (metal_reflection_path.has_value()) {
        manifest << "\"" << json_escape(metal_reflection_path->filename().string()) << "\"\n";
    } else {
        manifest << "null\n";
    }
    manifest
        << "  },\n"
        << "  \"metal_binding\": {\n"
        << "    \"version\": 3,\n"
        << "    \"model\": ";
    if (metal_binding.has_value()) {
        manifest << (metal_binding->model == MetalBindingModel::explicit_descriptor_tables
            ? "\"explicit_descriptor_tables\",\n"
            : "\"automatic_linear\",\n");
    } else {
        manifest << "null,\n";
    }
    manifest
        << "    \"descriptor_entry_size\": 24,\n"
        << "    \"locations\": [";
    if (metal_binding.has_value() && !metal_binding->locations.empty()) {
        manifest << "\n";
        for (std::size_t index = 0; index < metal_binding->locations.size(); ++index) {
            const auto& location = metal_binding->locations[index];
            manifest
                << "      {\n"
                << "        \"class\": \"" << shader_register_class_name(location.register_class) << "\",\n"
                << "        \"binding\": " << location.binding << ",\n"
                << "        \"space\": " << location.space << ",\n"
                << "        \"argument_buffer_offset\": " << location.argument_buffer_offset << ",\n"
                << "        \"size\": " << location.size << "\n"
                << "      }";
            if (index + 1U != metal_binding->locations.size()) {
                manifest << ',';
            }
            manifest << "\n";
        }
    }
    manifest
        << "    ]\n"
        << "  },\n"
        << "  \"reflection\": {\n"
        << "    \"version\": 1,\n"
        << "    \"thread_group_size\": ";
    if (reflection.thread_group_size.has_value()) {
        const auto& size = *reflection.thread_group_size;
        manifest
            << "{\"x\": " << size.x
            << ", \"y\": " << size.y
            << ", \"z\": " << size.z << "},\n";
    } else {
        manifest << "null,\n";
    }
    manifest << "    \"resources\": [";

    if (!reflection.resources.empty()) {
        manifest << "\n";
    }
    for (std::size_t index = 0; index < reflection.resources.size(); ++index) {
        const auto& resource = reflection.resources[index];
        manifest
            << "      {\n"
            << "        \"name\": \"" << json_escape(resource.name) << "\",\n"
            << "        \"class\": \"" << shader_register_class_name(resource.register_class) << "\",\n"
            << "        \"binding\": " << resource.binding << ",\n"
            << "        \"space\": " << resource.space << ",\n"
            << "        \"count\": " << resource.count << ",\n"
            << "        \"unbounded\": " << (resource.unbounded ? "true" : "false") << ",\n"
            << "        \"type\": \"" << json_escape(resource.type) << "\",\n"
            << "        \"format\": \"" << json_escape(resource.format) << "\",\n"
            << "        \"dimension\": \"" << json_escape(resource.dimension) << "\"\n"
            << "      }";
        if (index + 1U != reflection.resources.size()) {
            manifest << ',';
        }
        manifest << "\n";
    }
    manifest
        << "    ]\n"
        << "  }\n"
        << "}\n";
    return manifest.str();
}

[[nodiscard]] bool manifest_matches(
    const std::filesystem::path& path,
    std::string_view cache_key
) {
    const std::string manifest = read_file(path);
    if (manifest.empty()) {
        return false;
    }
    const std::string needle = "\"cache_key\": \"" + std::string{cache_key} + "\"";
    return manifest.find(needle) != std::string::npos;
}

[[nodiscard]] std::string executable_candidate_name(std::string_view name) {
#ifdef _WIN32
    if (name.size() < 4 || name.substr(name.size() - 4) != ".exe") {
        return std::string{name} + ".exe";
    }
#endif
    return std::string{name};
}

} // namespace

std::string_view shader_stage_name(const ShaderStage stage) noexcept {
    switch (stage) {
    case ShaderStage::vertex: return "vertex";
    case ShaderStage::fragment: return "fragment";
    case ShaderStage::compute: return "compute";
    }
    return "unknown";
}

std::string_view cook_target_name(const CookTarget target) noexcept {
    switch (target) {
    case CookTarget::direct3d12: return "direct3d12";
    case CookTarget::metal: return "metal";
    case CookTarget::all: return "all";
    }
    return "unknown";
}

std::string_view shader_register_class_name(const ShaderRegisterClass register_class) noexcept {
    switch (register_class) {
    case ShaderRegisterClass::constant_buffer: return "constant_buffer";
    case ShaderRegisterClass::shader_resource: return "shader_resource";
    case ShaderRegisterClass::unordered_access: return "unordered_access";
    case ShaderRegisterClass::sampler: return "sampler";
    }
    return "unknown";
}

std::string shader_profile(const ShaderStage stage, const std::string_view shader_model) {
    std::string prefix;
    switch (stage) {
    case ShaderStage::vertex: prefix = "vs_"; break;
    case ShaderStage::fragment: prefix = "ps_"; break;
    case ShaderStage::compute: prefix = "cs_"; break;
    }

    std::string model{shader_model};
    for (char& character : model) {
        if (character == '.') {
            character = '_';
        }
    }
    return prefix + model;
}

ShaderReflection parse_dxc_resource_bindings(const std::string_view assembly) {
    ShaderReflection reflection;
    reflection.thread_group_size = parse_dxc_compute_thread_group_size(assembly);
    constexpr std::string_view marker = "; Resource Bindings:";
    const std::size_t marker_position = assembly.find(marker);
    if (marker_position == std::string_view::npos) {
        reflection.diagnostics = "DXC assembly does not contain a Resource Bindings section.";
        return reflection;
    }

    std::istringstream stream{std::string{assembly.substr(marker_position)}};
    std::string line;
    bool saw_header = false;
    bool reading_rows = false;

    while (std::getline(stream, line)) {
        std::string content = trim(std::move(line));
        if (!content.starts_with(';')) {
            if (reading_rows) {
                reflection.valid = true;
                return reflection;
            }
            continue;
        }

        content = trim(content.substr(1));
        if (!reading_rows) {
            if (content.starts_with("Name") &&
                content.find("HLSL Bind") != std::string::npos &&
                content.find("Count") != std::string::npos) {
                saw_header = true;
                continue;
            }
            if (saw_header && content.starts_with("---")) {
                reading_rows = true;
            }
            continue;
        }

        if (content.empty()) {
            reflection.valid = true;
            return reflection;
        }

        const auto fields = split_whitespace(content);
        if (fields.size() < 7U) {
            reflection.diagnostics = "DXC emitted a malformed resource binding row: " + content;
            return reflection;
        }

        const std::string& binding_token = fields[fields.size() - 2U];
        const std::string& count_token = fields.back();
        const auto location = parse_binding_location(binding_token);
        if (!location.has_value()) {
            reflection.diagnostics = "DXC emitted an unsupported HLSL binding: " + binding_token;
            return reflection;
        }

        ShaderResourceReflection resource;
        resource.name = fields[0];
        resource.type = fields[1];
        resource.format = fields[2];
        resource.dimension = fields[3];
        resource.register_class = location->register_class;
        resource.binding = location->binding;
        resource.space = location->space;

        if (count_token == "unbounded") {
            resource.count = 0;
            resource.unbounded = true;
        } else if (!parse_uint32(count_token, resource.count) || resource.count == 0U) {
            reflection.diagnostics = "DXC emitted an invalid resource count: " + count_token;
            return reflection;
        }

        reflection.resources.push_back(std::move(resource));
        line.clear();
    }

    if (!reading_rows) {
        reflection.diagnostics = "DXC Resource Bindings section is missing its table header.";
        return reflection;
    }

    reflection.valid = true;
    return reflection;
}

std::string make_cache_key(
    const std::string_view source,
    const CookRequest& request,
    const std::string_view dxc_version,
    const std::string_view metal_converter_version
) {
    std::string material;
    material.reserve(source.size() + 512);
    hash_append(material, "VICE_SHADER_ASSET_V5_METAL_EXPLICIT_TABLES1");
    hash_append(material, source);
    hash_append(material, request.entry_point);
    hash_append(material, shader_stage_name(request.stage));
    hash_append(material, cook_target_name(request.target));
    hash_append(material, request.shader_model);
    hash_append(material, request.debug ? "debug" : "release");
    hash_append(material, dxc_version);
    hash_append(material, metal_converter_version);
    return sha256(material);
}

std::filesystem::path discover_executable(
    const std::filesystem::path& explicit_path,
    const std::string_view environment_variable,
    const std::string_view executable_name
) {
    std::error_code error;
    if (!explicit_path.empty()) {
        if (std::filesystem::exists(explicit_path, error)) {
            return explicit_path;
        }
        return {};
    }

    if (const auto configured = environment_value(environment_variable); configured.has_value()) {
        const std::filesystem::path configured_path{*configured};
        if (std::filesystem::exists(configured_path, error)) {
            return configured_path;
        }
    }

    const auto path_value = environment_value("PATH");
    if (!path_value.has_value()) {
        return {};
    }

#ifdef _WIN32
    constexpr char separator = ';';
#else
    constexpr char separator = ':';
#endif

    const std::string candidate_name = executable_candidate_name(executable_name);
    std::string_view remaining{*path_value};
    while (!remaining.empty()) {
        const std::size_t split = remaining.find(separator);
        const std::string_view segment = remaining.substr(0, split);
        if (!segment.empty()) {
            const std::filesystem::path candidate =
                std::filesystem::path{std::string{segment}} / candidate_name;
            if (std::filesystem::exists(candidate, error) &&
                std::filesystem::is_regular_file(candidate, error)) {
                return candidate;
            }
        }
        if (split == std::string_view::npos) {
            break;
        }
        remaining.remove_prefix(split + 1);
    }
    return {};
}

CookResult cook(const CookRequest& request) {
    CookResult result;
    if (request.input.empty() || request.output_directory.empty() ||
        request.entry_point.empty()) {
        result.diagnostics = "Input, output directory, and entry point are required.";
        return result;
    }

    const std::string source = read_file(request.input);
    if (source.empty()) {
        result.diagnostics = "Could not read shader source: " + request.input.string();
        return result;
    }

    std::error_code error;
    std::filesystem::create_directories(request.output_directory, error);
    if (error) {
        result.diagnostics = "Could not create output directory: " + error.message();
        return result;
    }

    Toolchain toolchain = request.toolchain;
    toolchain.dxc = discover_executable(toolchain.dxc, "VICE_DXC", "dxc");
    if (toolchain.dxc.empty()) {
        result.diagnostics =
            "DXC was not found. Pass --dxc, set VICE_DXC, or add dxc to PATH.";
        return result;
    }

    if (needs_metal(request.target)) {
        toolchain.metal_shader_converter = discover_executable(
            toolchain.metal_shader_converter,
            "VICE_METAL_SHADER_CONVERTER",
            "metal-shaderconverter"
        );
        if (toolchain.metal_shader_converter.empty()) {
            result.diagnostics =
                "Metal Shader Converter was not found. Pass --metal-shader-converter, "
                "set VICE_METAL_SHADER_CONVERTER, or add metal-shaderconverter to PATH.";
            return result;
        }
    }

    const std::string dxc_version = tool_version(toolchain.dxc, request.output_directory);
    const std::string converter_version = needs_metal(request.target)
        ? tool_version(toolchain.metal_shader_converter, request.output_directory)
        : std::string{};

    result.cache_key = make_cache_key(source, request, dxc_version, converter_version);
    result.dxil_path = request.output_directory / "shader.dxil";
    result.assembly_path = request.output_directory / "shader.dxc.txt";
    if (needs_metal(request.target)) {
        result.metallib_path = request.output_directory / "shader.metallib";
        result.metal_reflection_path = request.output_directory / "shader.metal.json";
    }
    result.manifest_path = request.output_directory / "shader.vice.json";

    const bool artifacts_exist =
        std::filesystem::exists(result.dxil_path, error) &&
        std::filesystem::exists(result.assembly_path, error) &&
        (!result.metallib_path.has_value() ||
            std::filesystem::exists(*result.metallib_path, error)) &&
        (!result.metal_reflection_path.has_value() ||
            std::filesystem::exists(*result.metal_reflection_path, error));
    if (!request.force && artifacts_exist &&
        manifest_matches(result.manifest_path, result.cache_key)) {
        const std::string assembly = read_file(result.assembly_path);
        result.reflection = parse_dxc_resource_bindings(assembly);
        if (result.reflection.valid &&
            validate_compute_reflection(request.stage, result.reflection, result.diagnostics)) {
            if (result.metal_reflection_path.has_value()) {
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
            result.success = true;
            result.cache_hit = true;
            result.diagnostics = "Shader is up to date.";
            return result;
        }
    }

    const auto compiler_log = request.output_directory / "dxc.log";
    const std::string profile = shader_profile(request.stage, request.shader_model);
    std::string dxc_command =
        shell_quote_path(toolchain.dxc) +
        " -T " + shell_quote(std::string_view{profile}) +
        " -E " + shell_quote(std::string_view{request.entry_point}) +
        " -HV 2021 -Ges -WX -Zpr ";
    if (request.debug) {
        dxc_command += "-Od -Zi ";
    } else {
        dxc_command += "-O3 -Qstrip_debug ";
    }
    dxc_command +=
        "-Fc " + shell_quote_path(result.assembly_path) + " " +
        "-Fo " + shell_quote_path(result.dxil_path) + " " + shell_quote_path(request.input);

    const ProcessResult dxc_result = run_capture(dxc_command, compiler_log);
    if (dxc_result.exit_code != 0 || !std::filesystem::exists(result.dxil_path, error) ||
        !std::filesystem::exists(result.assembly_path, error)) {
        result.diagnostics = "DXC failed.\n" + dxc_result.output;
        return result;
    }

    const std::string assembly = read_file(result.assembly_path);
    result.reflection = parse_dxc_resource_bindings(assembly);
    if (!result.reflection.valid) {
        result.diagnostics = "DXC reflection extraction failed.\n" + result.reflection.diagnostics;
        return result;
    }
    if (!validate_compute_reflection(request.stage, result.reflection, result.diagnostics)) {
        return result;
    }

    std::optional<MetalBindingReflection> metal_binding;
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
                "Metal Shader Converter failed.\n" + converter_result.output;
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

    const std::string manifest = build_manifest(
        request,
        result.cache_key,
        dxc_version,
        converter_version,
        result.dxil_path,
        result.assembly_path,
        result.metallib_path,
        result.metal_reflection_path,
        metal_binding,
        result.reflection
    );
    if (!write_file(result.manifest_path, manifest)) {
        result.diagnostics = "Could not write shader manifest.";
        return result;
    }

    result.success = true;
    result.diagnostics = "Shader cooked.";
    return result;
}

} // namespace vice::tools::shader_cooker

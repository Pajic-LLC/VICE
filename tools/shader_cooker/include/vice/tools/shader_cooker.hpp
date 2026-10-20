#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::shader_cooker {

enum class ShaderStage {
    vertex,
    fragment,
    compute,
};

enum class CookTarget {
    direct3d12,
    metal,
    all,
};

enum class ShaderRegisterClass {
    constant_buffer,
    shader_resource,
    unordered_access,
    sampler,
};

struct ComputeThreadGroupReflection {
    std::uint32_t x = 1U;
    std::uint32_t y = 1U;
    std::uint32_t z = 1U;
};

struct ShaderResourceReflection {
    std::string name;
    ShaderRegisterClass register_class = ShaderRegisterClass::shader_resource;
    std::uint32_t binding = 0;
    std::uint32_t space = 0;
    std::uint32_t count = 1;
    bool unbounded = false;
    std::string type;
    std::string format;
    std::string dimension;
};

struct ShaderReflection {
    bool valid = false;
    std::vector<ShaderResourceReflection> resources;
    std::optional<ComputeThreadGroupReflection> thread_group_size;
    std::string diagnostics;
};

struct Toolchain {
    std::filesystem::path dxc;
    std::filesystem::path metal_shader_converter;
};

struct CookRequest {
    std::filesystem::path input;
    std::filesystem::path output_directory;
    std::string entry_point;
    ShaderStage stage = ShaderStage::vertex;
    CookTarget target = CookTarget::all;
    std::string shader_model = "6.0";
    bool debug = false;
    bool force = false;
    Toolchain toolchain;
};

struct CookResult {
    bool success = false;
    bool cache_hit = false;
    std::string cache_key;
    std::filesystem::path dxil_path;
    std::filesystem::path assembly_path;
    std::optional<std::filesystem::path> metallib_path;
    std::optional<std::filesystem::path> metal_reflection_path;
    std::filesystem::path manifest_path;
    ShaderReflection reflection;
    std::string diagnostics;
};

[[nodiscard]] std::string_view shader_stage_name(ShaderStage stage) noexcept;
[[nodiscard]] std::string_view cook_target_name(CookTarget target) noexcept;
[[nodiscard]] std::string_view shader_register_class_name(ShaderRegisterClass register_class) noexcept;
[[nodiscard]] std::string shader_profile(ShaderStage stage, std::string_view shader_model);
[[nodiscard]] ShaderReflection parse_dxc_resource_bindings(std::string_view assembly);
[[nodiscard]] std::string make_cache_key(
    std::string_view source,
    const CookRequest& request,
    std::string_view dxc_version,
    std::string_view metal_converter_version
);
[[nodiscard]] std::filesystem::path discover_executable(
    const std::filesystem::path& explicit_path,
    std::string_view environment_variable,
    std::string_view executable_name
);
[[nodiscard]] CookResult cook(const CookRequest& request);

} // namespace vice::tools::shader_cooker

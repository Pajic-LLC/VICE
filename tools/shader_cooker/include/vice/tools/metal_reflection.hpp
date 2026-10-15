#pragma once

#include <vice/tools/shader_cooker.hpp>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::shader_cooker {

struct MetalResourceLocationReflection {
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

} // namespace vice::tools::shader_cooker

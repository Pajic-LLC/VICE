#pragma once
#include <cstdint>
#include <string>
#include <string_view>
namespace vice::runtime::detail {
// Stable compact package names keep staged native kernels within Windows path limits.
inline std::string builtin_shader_package_directory(std::string_view name) {
    std::uint64_t hash = 14695981039346656037ULL;
    for (const unsigned char value : name) { hash ^= value; hash *= 1099511628211ULL; }
    constexpr char digits[] = "0123456789abcdef";
    std::string result(16, '0');
    for (unsigned i = 0; i < 16; ++i) { result[15-i] = digits[hash & 15U]; hash >>= 4U; }
    return result;
}
}

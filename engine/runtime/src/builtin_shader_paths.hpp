#pragma once
#include <filesystem>
#include <vice/runtime/detail/builtin_shader_package.hpp>
namespace vice::runtime::detail {
// Each renderer selects its content root on its render thread before initializing passes.
inline thread_local std::filesystem::path packaged_shader_root;
inline std::filesystem::path builtin_shader_manifest(const std::filesystem::path &configured) {
    if(packaged_shader_root.empty()) return configured;
    return packaged_shader_root/builtin_shader_package_directory(configured.parent_path().filename().string())/configured.filename();
}
}

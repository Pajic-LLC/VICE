#include <vice/runtime/module_loader.hpp>

#include <vice/platform/dynamic_library.hpp>

#include <filesystem>
#include <string>
#include <string_view>

namespace vice::runtime {

std::filesystem::path game_module_path(
    const std::filesystem::path& runtime_root,
    const std::string_view game_target
) {
    if (runtime_root.empty() || game_target.empty()) return {};
    return runtime_root /
        "modules" /
        (std::string{game_target} + std::string{platform::dynamic_library_extension()});
}

} // namespace vice::runtime

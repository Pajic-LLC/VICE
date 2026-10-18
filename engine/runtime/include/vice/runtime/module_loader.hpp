#pragma once

#include <vice/runtime/runtime.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

namespace vice::runtime {

struct GameModuleLoadResult {
    std::unique_ptr<GameModule> module;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return module != nullptr;
    }
};

[[nodiscard]] std::filesystem::path staged_game_module_path(
    const std::filesystem::path& executable,
    std::string_view game_target
);

[[nodiscard]] GameModuleLoadResult load_game_module(
    const std::filesystem::path& path
);

} // namespace vice::runtime

#include <vice/runtime/module_api.hpp>

extern "C" VICE_GAME_MODULE_EXPORT bool vice_game_module_load(
    const std::uint32_t host_abi_version,
    const vice::runtime::module::HostApi*,
    vice::runtime::module::ModuleApi*
) noexcept {
    return host_abi_version == vice::runtime::module::abi_version + 1U;
}

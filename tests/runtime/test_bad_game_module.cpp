#include <vice/runtime/module_api.hpp>

extern "C" VICE_GAME_MODULE_EXPORT int vice_not_a_game_module() noexcept {
    return 1;
}

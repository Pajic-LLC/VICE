#include <vice/core/build.hpp>
#include <vice/core/log.hpp>
#include <vice/core/platform.hpp>
#include <vice/core/time.hpp>
#include <vice/core/version.hpp>
#include <vice/world/world.hpp>

#include <string>

int main() {
    using namespace vice::core;

    const auto platform = current_platform();
    if (platform == Platform::Unsupported) {
        log(LogLevel::fatal, "This platform is not a VICE development target.");
        return 1;
    }

    log(LogLevel::info, std::string{"VICE "} + std::string{version_string()});
    log(LogLevel::info, std::string{"Platform: "} + std::string{platform_name(platform)});
    log(LogLevel::info, std::string{"Architecture: "} + std::string{architecture_name()});
    log(LogLevel::info, std::string{"Compiler: "} + std::string{compiler_name()});
    log(
        LogLevel::info,
        std::string{"Build: "} + std::string{build_configuration_name(current_build_configuration())}
    );

    vice::world::World world;
    const auto entity = world.create_entity();
    if (const auto persistent_id = world.persistent_id(entity)) {
        log(LogLevel::info, std::string{"World entity: "} + persistent_id->to_string());
    }

    FrameClock clock;
    clock.tick();
    log(LogLevel::debug, std::string{"Frame clock delta: "} + std::to_string(clock.delta_seconds()) + " s");

    return 0;
}

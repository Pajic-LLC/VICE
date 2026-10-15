#include <vice/core/platform.hpp>
#include <vice/core/version.hpp>

#include <iostream>

int main() {
    const auto platform = vice::core::current_platform();

    std::cout << "VICE " << vice::core::version_string() << '\n';
    std::cout << "Platform: " << vice::core::platform_name(platform) << '\n';

    if (platform == vice::core::Platform::Unsupported) {
        std::cerr << "This platform is not a VICE development target.\n";
        return 1;
    }

    return 0;
}

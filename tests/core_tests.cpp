#include <vice/core/platform.hpp>
#include <vice/core/version.hpp>

#include <cassert>
#include <string_view>

int main() {
    using vice::core::Platform;

    static_assert(vice::core::Version::major == 0);
    static_assert(vice::core::Version::minor == 1);
    static_assert(vice::core::Version::patch == 0);

    assert(vice::core::version_string() == std::string_view{"0.1.0"});

    const auto platform = vice::core::current_platform();
    assert(platform == Platform::macOS || platform == Platform::Windows);
    assert(vice::core::platform_name(platform) != std::string_view{"Unsupported"});

    return 0;
}

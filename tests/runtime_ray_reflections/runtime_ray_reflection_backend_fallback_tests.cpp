#include <vice/render/rhi.hpp>
#include <vice/runtime/ray_reflections.hpp>

#include <iostream>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

int main() {
    using namespace vice;

    auto device = render::Device::create();
    if (device == nullptr) {
        std::cout << "SKIP: VICE render device is unavailable on this runner.\n";
        return 0;
    }

    runtime::RuntimeRayReflectionsPass pass;
    std::string diagnostics;

    CHECK(pass.initialize(*device, {}, &diagnostics));
    CHECK(!pass.available());
    CHECK(diagnostics.empty());

    CHECK(pass.resize(1U, 1U, &diagnostics));
    CHECK(pass.radiance() == nullptr);
    CHECK(pass.hits() == nullptr);
    CHECK(diagnostics.empty());

    std::cout << "Validated reflection fallback on "
              << render::backend_name(device->backend())
              << "; ray_tracing=" << (device->capabilities().ray_tracing ? "true" : "false")
              << ".\n";
    return 0;
}

#undef CHECK

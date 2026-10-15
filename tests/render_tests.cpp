#include <vice/render/rhi.hpp>

#include <cassert>

int main() {
    using namespace vice::render;

    assert(backend_name(Backend::metal) == "Metal");
    assert(backend_name(Backend::direct3d12) == "Direct3D 12");

    const DeviceCapabilities capabilities{};
    assert(!capabilities.ray_tracing);
    assert(!capabilities.mesh_shaders);
    assert(capabilities.recommended_frames_in_flight == 2U);

    constexpr Color black{};
    static_assert(black.red == 0.0F);
    static_assert(black.green == 0.0F);
    static_assert(black.blue == 0.0F);
    static_assert(black.alpha == 1.0F);

    return 0;
}

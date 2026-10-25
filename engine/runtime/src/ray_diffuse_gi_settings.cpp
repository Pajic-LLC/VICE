#include <vice/runtime/scene.hpp>

#include <utility>

namespace vice::runtime {

bool RuntimeSceneRenderer::set_ray_diffuse_gi_settings(
    const RuntimeRayDiffuseGiSettings settings,
    std::string* const diagnostics
) {
    if (!is_valid(settings)) {
        if (diagnostics != nullptr) {
            *diagnostics = "Runtime diffuse-GI settings are invalid.";
        }
        return false;
    }
    ray_diffuse_gi_settings_ = settings;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeRayDiffuseGiSettings RuntimeSceneRenderer::ray_diffuse_gi_settings() const noexcept {
    return ray_diffuse_gi_settings_;
}

} // namespace vice::runtime

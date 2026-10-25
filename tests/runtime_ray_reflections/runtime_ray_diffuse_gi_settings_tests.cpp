#include <vice/runtime/ray_diffuse_gi_settings.hpp>
#include <vice/runtime/scene.hpp>

#include <cassert>
#include <cmath>
#include <limits>
#include <string>

int main() {
    using vice::runtime::RuntimeRayDiffuseGiSettings;
    using vice::runtime::RuntimeSceneRenderer;
    using vice::runtime::is_valid;

    const RuntimeRayDiffuseGiSettings defaults{};
    assert(is_valid(defaults));

    auto disabled = defaults;
    disabled.enabled = false;
    assert(is_valid(disabled));

    auto zero_distance = defaults;
    zero_distance.max_distance = 0.0F;
    assert(!is_valid(zero_distance));

    auto negative_distance = defaults;
    negative_distance.max_distance = -1.0F;
    assert(!is_valid(negative_distance));

    auto nan_distance = defaults;
    nan_distance.max_distance = std::numeric_limits<float>::quiet_NaN();
    assert(!is_valid(nan_distance));

    auto infinite_distance = defaults;
    infinite_distance.max_distance = std::numeric_limits<float>::infinity();
    assert(!is_valid(infinite_distance));

    auto zero_bias = defaults;
    zero_bias.surface_bias = 0.0F;
    assert(!is_valid(zero_bias));

    auto negative_bias = defaults;
    negative_bias.surface_bias = -0.001F;
    assert(!is_valid(negative_bias));

    auto equal_bias = defaults;
    equal_bias.surface_bias = equal_bias.max_distance;
    assert(!is_valid(equal_bias));

    auto excessive_bias = defaults;
    excessive_bias.surface_bias = excessive_bias.max_distance + 1.0F;
    assert(!is_valid(excessive_bias));

    auto nan_bias = defaults;
    nan_bias.surface_bias = std::numeric_limits<float>::quiet_NaN();
    assert(!is_valid(nan_bias));

    auto zero_radius = defaults;
    zero_radius.spatial_kernel_radius = 0U;
    assert(!is_valid(zero_radius));

    auto maximum_radius = defaults;
    maximum_radius.spatial_kernel_radius = 4U;
    assert(is_valid(maximum_radius));

    auto excessive_radius = defaults;
    excessive_radius.spatial_kernel_radius = 5U;
    assert(!is_valid(excessive_radius));

    auto zero_history = defaults;
    zero_history.temporal_history_weight = 0.0F;
    assert(is_valid(zero_history));

    auto full_history = defaults;
    full_history.temporal_history_weight = 1.0F;
    assert(is_valid(full_history));

    auto negative_history = defaults;
    negative_history.temporal_history_weight = -0.01F;
    assert(!is_valid(negative_history));

    auto excessive_history = defaults;
    excessive_history.temporal_history_weight = 1.01F;
    assert(!is_valid(excessive_history));

    auto nan_history = defaults;
    nan_history.temporal_history_weight = std::numeric_limits<float>::quiet_NaN();
    assert(!is_valid(nan_history));

    RuntimeSceneRenderer renderer;
    assert(renderer.ray_diffuse_gi_settings() == defaults);

    RuntimeRayDiffuseGiSettings tuned = defaults;
    tuned.max_distance = 24.0F;
    tuned.surface_bias = 0.01F;
    tuned.spatial_kernel_radius = 3U;
    tuned.temporal_history_weight = 0.85F;
    std::string diagnostics = "stale";
    assert(renderer.set_ray_diffuse_gi_settings(tuned, &diagnostics));
    assert(diagnostics.empty());
    assert(renderer.ray_diffuse_gi_settings() == tuned);

    const auto committed = renderer.ray_diffuse_gi_settings();
    auto invalid_update = tuned;
    invalid_update.spatial_kernel_radius = 5U;
    assert(!renderer.set_ray_diffuse_gi_settings(invalid_update, &diagnostics));
    assert(!diagnostics.empty());
    assert(renderer.ray_diffuse_gi_settings() == committed);

    disabled = tuned;
    disabled.enabled = false;
    assert(renderer.set_ray_diffuse_gi_settings(disabled, &diagnostics));
    assert(diagnostics.empty());
    assert(renderer.ray_diffuse_gi_settings() == disabled);

    return 0;
}

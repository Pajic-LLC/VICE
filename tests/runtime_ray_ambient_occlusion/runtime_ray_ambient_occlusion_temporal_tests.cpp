#include <vice/runtime/ray_ambient_occlusion_temporal.hpp>

#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>

#define CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "CHECK failed at line " << __LINE__ << ": " #condition << '\n'; \
        return __LINE__; \
    } \
} while (false)

namespace {

[[nodiscard]] bool near(const float left, const float right, const float epsilon = 1.0e-5F) {
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] std::string read_text_file(const char* const path) {
    std::ifstream input{path, std::ios::binary};
    if (!input.good()) return {};
    return std::string{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{}
    };
}

} // namespace

int main() {
    using namespace vice::runtime;

    const auto constants = make_runtime_ray_ambient_occlusion_temporal_constants(
        1920U,
        1080U,
        true
    );
    CHECK(constants.has_value());
    CHECK(constants->target_size_history[0] == 1920.0F);
    CHECK(constants->target_size_history[1] == 1080.0F);
    CHECK(constants->target_size_history[2] == 1.0F);
    CHECK(near(constants->target_size_history[3], 0.88F));
    CHECK(near(constants->rejection_parameters[0], 0.0025F));
    CHECK(near(constants->rejection_parameters[1], 32.0F));

    CHECK(!make_runtime_ray_ambient_occlusion_temporal_constants(0U, 1080U, true).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_temporal_constants(
        1920U, 1080U, true, 1.1F
    ).has_value());
    CHECK(!make_runtime_ray_ambient_occlusion_temporal_constants(
        1920U, 1080U, true, 0.88F, 0.0F
    ).has_value());

    CHECK(near(runtime_ray_ambient_occlusion_temporal_history_weight(
        0.88F, 0.0F, 0.0F, 32.0F, true
    ), 0.88F));
    CHECK(runtime_ray_ambient_occlusion_temporal_history_weight(
        0.88F, 0.02F, 0.0F, 32.0F, true
    ) < 0.88F);
    CHECK(runtime_ray_ambient_occlusion_temporal_history_weight(
        0.88F, 0.0F, 0.0F, 32.0F, false
    ) == 0.0F);

    CHECK(runtime_ray_ambient_occlusion_temporal_depth_accept(0.5F, 0.5005F));
    CHECK(!runtime_ray_ambient_occlusion_temporal_depth_accept(0.2F, 0.8F));
    CHECK(!runtime_ray_ambient_occlusion_temporal_depth_accept(1.0F, 1.0F));

    CHECK(near(runtime_ray_ambient_occlusion_temporal_clamp_history(0.9F, 0.2F, 0.7F), 0.7F));
    CHECK(near(runtime_ray_ambient_occlusion_temporal_clamp_history(0.1F, 0.2F, 0.7F), 0.2F));
    CHECK(near(runtime_ray_ambient_occlusion_temporal_clamp_history(0.5F, 0.2F, 0.7F), 0.5F));

#ifdef VICE_RUNTIME_SCENE_SOURCE
    const std::string scene_source = read_text_file(VICE_RUNTIME_SCENE_SOURCE);
    CHECK(!scene_source.empty());
    const auto temporal_hook = scene_source.find("runtime_ray_ambient_occlusion_after_motion_vectors");
    const auto motion_macro = scene_source.find("#define encode_motion_vectors");
    const auto legacy_include = scene_source.find("#include \"scene_legacy.inc\"");
    CHECK(temporal_hook != std::string::npos);
    CHECK(motion_macro != std::string::npos);
    CHECK(legacy_include != std::string::npos);
    CHECK(motion_macro < legacy_include);
    CHECK(scene_source.find("temporal_visibility_ready") != std::string::npos);
    CHECK(scene_source.find("RuntimeTemporalFrameState") != std::string::npos);
#endif

    return 0;
}

#undef CHECK

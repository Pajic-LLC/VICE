#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] std::string read_text(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{stream}, std::istreambuf_iterator<char>{}};
}

} // namespace

int main() {
    const auto source = read_text(VICE_RAY_DIFFUSE_GI_PIPELINE_CPP_SOURCE);
    require(!source.empty(), "diffuse-GI production pipeline source must be readable");

    const auto resize = source.find("bool RuntimeRayDiffuseGiPipeline::resize(");
    const auto resize_clear_valid = source.find("valid_ = false;", resize);
    const auto resize_trace = source.find("trace_.resize(", resize);
    require(
        resize != std::string::npos && resize_clear_valid != std::string::npos &&
            resize_trace != std::string::npos && resize_clear_valid < resize_trace,
        "resize must invalidate publishable GI before any render-size resource transition"
    );

    const auto dispatch = source.find("bool RuntimeRayDiffuseGiPipeline::dispatch(");
    const auto clear_valid = source.find("valid_ = false;", dispatch);
    const auto disabled_fallback = source.find("!input.settings.enabled || !available()", dispatch);
    const auto trace = source.find("trace_.dispatch(", dispatch);
    const auto hit_history = source.find("hit_history_.dispatch(", dispatch);
    const auto denoise = source.find("denoise_.dispatch(", dispatch);
    const auto temporal = source.find("temporal_.dispatch(", dispatch);
    const auto publish_valid = source.find("valid_ = true;", dispatch);
    const auto radiance_accessor = source.find("render::Texture* RuntimeRayDiffuseGiPipeline::radiance() const noexcept");

    require(
        dispatch != std::string::npos && clear_valid != std::string::npos &&
            disabled_fallback != std::string::npos && clear_valid < disabled_fallback,
        "every dispatch must invalidate publication before disabled/unavailable fallback is evaluated"
    );
    require(
        trace != std::string::npos && hit_history != std::string::npos && denoise != std::string::npos &&
            temporal != std::string::npos && publish_valid != std::string::npos &&
            trace < hit_history && hit_history < denoise && denoise < temporal && temporal < publish_valid,
        "GI must execute trace, shared hit history, spatial reconstruction, temporal reconstruction, then publish in causal order"
    );
    require(
        source.find("input.temporal.previous_unjittered_view_projection", dispatch) != std::string::npos &&
            source.find("input.temporal.current_camera_global_position", dispatch) != std::string::npos &&
            source.find("input.temporal.previous_camera_global_position", dispatch) != std::string::npos,
        "GI hit history must reuse shared unjittered temporal projection and large-world camera origins"
    );
    require(
        source.find("input.temporal.frame_index % runtime_ray_diffuse_gi_sample_period", dispatch) !=
            std::string::npos,
        "GI sampling must derive deterministic bounded sampling state from the shared Runtime frame index"
    );
    require(
        radiance_accessor != std::string::npos &&
            source.find("return valid_ ? temporal_.radiance() : nullptr;", radiance_accessor) !=
                std::string::npos,
        "only a successful current-frame temporal result may be exposed for PBR publication"
    );
    require(
        source.find("return valid_ ? trace_.radiance()") == std::string::npos &&
            source.find("return valid_ ? denoise_.radiance()") == std::string::npos,
        "raw or spatial-only GI must never be exposed as a partial-pipeline fallback"
    );
    require(
        source.find("const float max_distance = std::min(input.settings.max_distance, input.camera.far_plane);") !=
            std::string::npos &&
            source.find("max_distance <= input.settings.surface_bias") != std::string::npos,
        "GI ray quality must respect the active camera far plane and fail closed on an unusable range"
    );

    if (failures != 0) {
        std::cerr << failures << " Runtime diffuse-GI pipeline contract test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime diffuse-GI pipeline contract tests passed\n";
    return 0;
}

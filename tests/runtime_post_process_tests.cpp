#include <vice/runtime/post_process.hpp>
#include <vice/runtime/scene.hpp>

#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "runtime post-process test failed: " << message << '\n';
    std::exit(1);
}

[[nodiscard]] bool matrix_matches(
    const vice::math::Mat4& left,
    const vice::math::Mat4& right,
    const float epsilon = 0.000001F
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (std::abs(left.values[index] - right.values[index]) > epsilon) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace vice;

    constexpr auto plan = runtime::default_runtime_post_process_plan();
    require(plan.size() == 3U, "default post-process plan has stable slots");
    require(
        plan[0].pass == runtime::RuntimePostProcessPass::temporal_resolve && plan[0].enabled,
        "temporal resolve is enabled by default"
    );
    require(
        plan[1].pass == runtime::RuntimePostProcessPass::bloom && plan[1].enabled,
        "HDR bloom is enabled between temporal resolve and tone mapping"
    );
    require(
        plan[2].pass == runtime::RuntimePostProcessPass::tone_map && plan[2].enabled,
        "tone map remains the enabled terminal pass"
    );

    const auto jitter0 = runtime::runtime_taa_jitter(0U, 1920U, 1080U);
    const auto jitter1 = runtime::runtime_taa_jitter(1U, 1920U, 1080U);
    const auto jitter8 = runtime::runtime_taa_jitter(8U, 1920U, 1080U);
    require(
        std::abs(jitter0.x_pixels) <= 0.5F && std::abs(jitter0.y_pixels) <= 0.5F,
        "TAA jitter remains sub-pixel"
    );
    require(
        jitter0.x_pixels != jitter1.x_pixels || jitter0.y_pixels != jitter1.y_pixels,
        "successive TAA jitter samples differ"
    );
    require(
        jitter0.x_pixels == jitter8.x_pixels && jitter0.y_pixels == jitter8.y_pixels,
        "TAA jitter repeats at the declared period"
    );
    require(
        std::abs(jitter0.x_ndc - (2.0F * jitter0.x_pixels / 1920.0F)) < 0.000001F,
        "horizontal jitter converts to NDC"
    );
    require(
        std::abs(jitter0.y_ndc - (-2.0F * jitter0.y_pixels / 1080.0F)) < 0.000001F,
        "vertical jitter converts to framebuffer-oriented NDC"
    );

    const auto base_view = math::identity();
    const auto jittered_view = runtime::runtime_apply_projection_jitter(base_view, jitter0);
    require(
        std::abs(jittered_view(0, 3) - jitter0.x_ndc) < 0.000001F &&
        std::abs(jittered_view(1, 3) - jitter0.y_ndc) < 0.000001F,
        "projection jitter translates clip space"
    );

    const auto first_camera = world::PersistentId::parse("56000000-0000-4000-8000-000000000001");
    const auto second_camera = world::PersistentId::parse("56000000-0000-4000-8000-000000000002");
    require(first_camera.has_value() && second_camera.has_value(), "camera IDs parse");

    runtime::RuntimeTemporalHistory history;
    const auto first_unjittered = math::identity();
    const auto second_unjittered = math::translation({1.0F, 0.0F, 0.0F});
    const auto third_unjittered = math::translation({2.0F, 0.0F, 0.0F});
    const auto first_jittered = runtime::runtime_apply_projection_jitter(first_unjittered, jitter0);
    const auto second_jittered = runtime::runtime_apply_projection_jitter(second_unjittered, jitter1);

    const math::Vec3 origin{0.0F, 0.0F, 0.0F};
    const math::Vec3 forward{0.0F, 0.0F, -1.0F};
    const auto first_frame = history.begin_frame(
        *first_camera,
        first_unjittered,
        first_jittered,
        jitter0,
        origin,
        forward
    );
    require(!first_frame.history_valid, "first frame has no history");
    require(first_frame.frame_index == 0U, "first frame index starts at zero");
    require(
        matrix_matches(first_frame.previous_view_projection, first_jittered),
        "invalid history aliases current jittered camera transform"
    );
    history.commit_frame(
        *first_camera,
        first_unjittered,
        first_jittered,
        jitter0,
        origin,
        forward
    );
    require(history.valid(), "history becomes valid after commit");
    require(history.completed_frames() == 1U, "successful frame increments completed count");

    const auto second_frame = history.begin_frame(
        *first_camera,
        second_unjittered,
        second_jittered,
        jitter1,
        math::Vec3{1.0F, 0.0F, 0.0F},
        forward
    );
    require(second_frame.history_valid, "normal same-camera movement retains history");
    require(second_frame.frame_index == 1U, "second frame index follows committed frame");
    require(
        matrix_matches(second_frame.previous_view_projection, first_jittered),
        "same-camera frame receives prior jittered view projection"
    );
    require(
        second_frame.previous_jitter.x_pixels == jitter0.x_pixels &&
        second_frame.previous_jitter.y_pixels == jitter0.y_pixels,
        "same-camera frame receives prior jitter sample"
    );
    history.commit_frame(
        *first_camera,
        second_unjittered,
        second_jittered,
        jitter1,
        math::Vec3{1.0F, 0.0F, 0.0F},
        forward
    );

    const auto generation_before_teleport = history.generation();
    const auto teleport = history.begin_frame(
        *first_camera,
        third_unjittered,
        third_unjittered,
        {},
        math::Vec3{runtime::runtime_taa_camera_cut_distance + 20.0F, 0.0F, 0.0F},
        forward
    );
    require(!teleport.history_valid, "large camera teleport invalidates history");
    require(
        teleport.generation == generation_before_teleport + 1U,
        "camera teleport advances history generation"
    );
    history.commit_frame(
        *first_camera,
        third_unjittered,
        third_unjittered,
        {},
        origin,
        forward
    );

    const auto generation_before_rotation_cut = history.generation();
    const auto rotation_cut = history.begin_frame(
        *first_camera,
        third_unjittered,
        third_unjittered,
        {},
        origin,
        math::Vec3{0.0F, 0.0F, 1.0F}
    );
    require(!rotation_cut.history_valid, "abrupt view-direction cut invalidates history");
    require(
        rotation_cut.generation == generation_before_rotation_cut + 1U,
        "view-direction cut advances history generation"
    );
    history.commit_frame(
        *first_camera,
        third_unjittered,
        third_unjittered,
        {},
        origin,
        forward
    );

    const auto generation_before_camera_change = history.generation();
    const auto camera_cut = history.begin_frame(
        *second_camera,
        third_unjittered,
        third_unjittered,
        {},
        origin,
        forward
    );
    require(!camera_cut.history_valid, "camera identity discontinuity invalidates history");
    require(
        camera_cut.generation == generation_before_camera_change + 1U,
        "camera identity discontinuity advances history generation"
    );
    require(
        matrix_matches(camera_cut.previous_view_projection, third_unjittered),
        "camera cut produces zero-camera-motion history input"
    );
    history.commit_frame(
        *second_camera,
        third_unjittered,
        third_unjittered,
        {},
        origin,
        forward
    );

    const auto generation_before_reset = history.generation();
    history.reset();
    require(!history.valid(), "explicit reset invalidates history");
    require(
        history.generation() == generation_before_reset + 1U,
        "explicit reset advances history generation"
    );
    require(history.completed_frames() == 5U, "history reset preserves global frame index");

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) / "vice_runtime_post_process_test_56000000";
    require(!error, "temporary directory path");
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    require(!error, "empty runtime content root creates");

    auto device = render::Device::create();
    require(device != nullptr, "native render device creates");

    runtime::RuntimeSceneRenderer renderer;
    std::string diagnostics;
    require(
        renderer.initialize(*device, render::TextureFormat::bgra8_unorm, root, &diagnostics),
        "Runtime renderer initializes TAA and bloom pipelines"
    );
    require(diagnostics.empty(), "Runtime renderer initialization diagnostics");

    require(
        renderer.set_bloom(1.1F, 0.45F, 0.10F, &diagnostics),
        "Runtime accepts finite bloom parameters"
    );
    require(diagnostics.empty(), "valid bloom parameters clear diagnostics");
    require(
        !renderer.set_bloom(-0.1F, 0.45F, 0.10F, &diagnostics),
        "Runtime rejects negative bloom threshold"
    );
    require(
        !renderer.set_bloom(1.0F, 1.1F, 0.10F, &diagnostics),
        "Runtime rejects bloom soft knee outside unit range"
    );
    require(
        !renderer.set_bloom(1.0F, 0.5F, -0.01F, &diagnostics),
        "Runtime rejects negative bloom strength"
    );
    require(
        renderer.set_bloom(1.0F, 0.5F, 0.08F, &diagnostics),
        "Runtime restores default bloom parameters"
    );
    require(diagnostics.empty(), "restored bloom parameters clear diagnostics");

    require(renderer.set_vignette(0.15F, &diagnostics), "Runtime accepts restrained vignette intensity");
    require(diagnostics.empty(), "valid vignette clears diagnostics");
    require(!renderer.set_vignette(-0.1F, &diagnostics), "Runtime rejects negative vignette intensity");
    require(!renderer.set_vignette(1.1F, &diagnostics), "Runtime rejects vignette above one");
    require(renderer.set_vignette(0.12F, &diagnostics), "Runtime restores default vignette intensity");
    require(diagnostics.empty(), "restored vignette clears diagnostics");

    require(
        renderer.resize(
            platform::WindowMetrics{
                .width = 64U,
                .height = 64U,
                .drawable_width = 64U,
                .drawable_height = 64U,
                .scale_factor = 1.0F,
            },
            &diagnostics
        ),
        "Runtime renderer allocates TAA and bloom frame targets"
    );
    require(diagnostics.empty(), "Runtime renderer resize diagnostics");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}

#include <vice/runtime/post_process.hpp>
#include <vice/runtime/scene.hpp>

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
    const vice::math::Mat4& right
) noexcept {
    for (std::size_t index = 0; index < left.values.size(); ++index) {
        if (left.values[index] != right.values[index]) return false;
    }
    return true;
}

} // namespace

int main() {
    using namespace vice;

    constexpr auto plan = runtime::default_runtime_post_process_plan();
    require(plan.size() == 3U, "default post-process plan has stable slots");
    require(
        plan[0].pass == runtime::RuntimePostProcessPass::temporal_resolve && !plan[0].enabled,
        "temporal resolve slot is reserved but disabled"
    );
    require(
        plan[1].pass == runtime::RuntimePostProcessPass::bloom && !plan[1].enabled,
        "bloom slot is reserved but disabled"
    );
    require(
        plan[2].pass == runtime::RuntimePostProcessPass::tone_map && plan[2].enabled,
        "tone map remains the enabled terminal pass"
    );

    const auto first_camera = world::PersistentId::parse("56000000-0000-4000-8000-000000000001");
    const auto second_camera = world::PersistentId::parse("56000000-0000-4000-8000-000000000002");
    require(first_camera.has_value() && second_camera.has_value(), "camera IDs parse");

    runtime::RuntimeTemporalHistory history;
    const auto first_view = math::identity();
    const auto second_view = math::translation({1.0F, 0.0F, 0.0F});
    const auto third_view = math::translation({2.0F, 0.0F, 0.0F});

    const auto first_frame = history.begin_frame(*first_camera, first_view);
    require(!first_frame.history_valid, "first frame has no history");
    require(first_frame.frame_index == 0U, "first frame index starts at zero");
    require(
        matrix_matches(first_frame.previous_view_projection, first_view),
        "invalid history aliases current camera transform"
    );
    history.commit_frame(*first_camera, first_view);
    require(history.valid(), "history becomes valid after commit");
    require(history.completed_frames() == 1U, "successful frame increments completed count");

    const auto second_frame = history.begin_frame(*first_camera, second_view);
    require(second_frame.history_valid, "same camera retains history");
    require(second_frame.frame_index == 1U, "second frame index follows committed frame");
    require(
        matrix_matches(second_frame.previous_view_projection, first_view),
        "same-camera frame receives prior view projection"
    );
    history.commit_frame(*first_camera, second_view);

    const auto generation_before_camera_change = history.generation();
    const auto camera_cut = history.begin_frame(*second_camera, third_view);
    require(!camera_cut.history_valid, "camera identity discontinuity invalidates history");
    require(
        camera_cut.generation == generation_before_camera_change + 1U,
        "camera identity discontinuity advances history generation"
    );
    require(
        matrix_matches(camera_cut.previous_view_projection, third_view),
        "camera cut produces zero-camera-motion history input"
    );
    history.commit_frame(*second_camera, third_view);

    const auto generation_before_reset = history.generation();
    history.reset();
    require(!history.valid(), "explicit reset invalidates history");
    require(
        history.generation() == generation_before_reset + 1U,
        "explicit reset advances history generation"
    );
    require(history.completed_frames() == 3U, "history reset preserves global frame index");

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
        "Runtime renderer initializes temporal pipelines"
    );
    require(diagnostics.empty(), "Runtime renderer initialization diagnostics");
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
        "Runtime renderer allocates history and motion targets"
    );
    require(diagnostics.empty(), "Runtime renderer resize diagnostics");

    std::filesystem::remove_all(root, error);
    require(!error, "temporary cleanup");
    return 0;
}

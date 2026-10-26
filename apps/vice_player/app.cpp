#include <vice/runtime/dade.hpp>
#include <vice/runtime/dev_session.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/runtime/package_metadata.hpp>
#include <vice/runtime/runtime.hpp>
#include <vice/runtime/status.hpp>

#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace {

class DefaultGameModule final : public vice::runtime::GameModule {};

class DevelopmentStatusObserver final : public vice::runtime::RuntimeObserver {
public:
    DevelopmentStatusObserver(
        vice::runtime::LoadedProject& project,
        vice::runtime::RuntimeDevSession session
    )
        : project_{&project}, session_{std::move(session)} {
        const auto latest = vice::runtime::read_latest_runtime_dev_snapshot(session_);
        if (latest) generation_ = latest.generation;
    }

    void on_runtime_observation(
        const vice::runtime::RuntimeFrameObservation& observation
    ) noexcept override {
        if (project_ == nullptr || project_->world == nullptr || generation_ ==
            std::numeric_limits<std::uint64_t>::max()) {
            return;
        }

        if (observation.execution_state == vice::runtime::RuntimeExecutionState::running) {
            elapsed_seconds_ += observation.frame_seconds;
            if (observation.frame_index != 1U && elapsed_seconds_ < publish_interval_seconds) {
                return;
            }
            elapsed_seconds_ = 0.0;
        }

        const auto snapshot = vice::runtime::make_runtime_status_snapshot(
            project_->manifest.id.to_string(),
            project_->manifest.game_target,
            project_->manifest.startup_world.generic_string(),
            observation.execution_state,
            observation.frame_index,
            observation.frame_seconds,
            observation.backend,
            observation.metrics,
            *project_->world,
            observation.scene_renderer
        );
        const auto payload = vice::runtime::serialize_runtime_status_snapshot(snapshot, session_.id);
        const auto next_generation = generation_ + 1U;
        std::string diagnostics;
        if (vice::runtime::publish_runtime_dev_snapshot(
                session_,
                next_generation,
                payload,
                &diagnostics
            )) {
            generation_ = next_generation;
            publication_error_reported_ = false;
            return;
        }

        if (!publication_error_reported_) {
            std::cerr << "VICE Runtime development snapshot publication failed: "
                      << diagnostics << '\n';
            publication_error_reported_ = true;
        }
    }

private:
    static constexpr double publish_interval_seconds = 0.10;

    vice::runtime::LoadedProject* project_ = nullptr;
    vice::runtime::RuntimeDevSession session_{};
    std::uint64_t generation_ = 0U;
    double elapsed_seconds_ = 0.0;
    bool publication_error_reported_ = false;
};

[[nodiscard]] constexpr std::string_view dade_platform() noexcept {
#if defined(_WIN32)
    return "windows";
#elif defined(__APPLE__)
    return "macos";
#elif defined(__linux__)
    return "linux";
#else
    return "unknown";
#endif
}

} // namespace

int main(const int argc, char** argv) {
    std::string diagnostics;
    vice::runtime::LaunchPaths launch;
    std::optional<vice::runtime::RuntimeDevSession> development_session;

    const bool staged_development_mode =
        argc == 4 && std::string_view{argv[1]} == "--vice-dev-session";
    const bool staged_mode = argc == 1 || staged_development_mode;

    if (staged_mode) {
        launch = vice::runtime::staged_launch_paths(
            std::filesystem::path{argv[0]},
            &diagnostics
        );
        if (staged_development_mode) {
            development_session = vice::runtime::RuntimeDevSession{
                .id = argv[3],
                .root = std::filesystem::path{argv[2]},
            };
            if (!vice::runtime::prepare_runtime_dev_session(*development_session, &diagnostics)) {
                std::cerr << "VICE Runtime could not prepare development session: "
                          << diagnostics << '\n';
                return 2;
            }
        }
    } else if (argc == 3) {
        launch = vice::runtime::explicit_launch_paths(
            std::filesystem::path{argv[1]},
            std::filesystem::path{argv[2]},
            &diagnostics
        );
    } else {
        std::cerr
            << "Usage: staged game target [--vice-dev-session <root> <id>], or "
               "vice_player <project.viceproj> <cooked-cache-root>\n";
        return 2;
    }

    if (launch.manifest.empty() || launch.cooked_cache_root.empty()) {
        std::cerr << "VICE Runtime could not resolve launch paths: " << diagnostics << '\n';
        return 1;
    }

    vice::runtime::DadeRequirement dade_requirement;
    if (staged_mode) {
        const auto package = vice::runtime::load_staged_package_metadata(
            std::filesystem::path{argv[0]}
        );
        if (!package) {
            std::cerr << "VICE Runtime rejected staged package metadata: "
                      << package.diagnostics << '\n';
            return 3;
        }
        dade_requirement.required = package.dade_required;
        dade_requirement.asset_id = package.dade_asset_id;
    } else {
        dade_requirement = vice::runtime::load_dade_requirement(launch.manifest);
        if (!dade_requirement) {
            std::cerr << "VICE Runtime could not read DADE ownership settings: "
                      << dade_requirement.diagnostics << '\n';
            return 3;
        }
    }

    if (dade_requirement.required) {
        const auto provider_path = vice::runtime::staged_dade_provider_path(
            std::filesystem::path{argv[0]}
        );
        auto provider = vice::runtime::load_dade_provider(provider_path);
        if (!provider) {
            std::cerr << "VICE Runtime cannot verify ownership for DADE asset `"
                      << dade_requirement.asset_id
                      << "`: "
                      << provider.diagnostics
                      << '\n';
            return 3;
        }

        const auto decision = provider.provider->verify(
            dade_requirement.asset_id,
            dade_platform(),
            launch.manifest.parent_path()
        );
        if (!decision.allowed) {
            std::cerr << "VICE Runtime denied DADE ownership for `"
                      << dade_requirement.asset_id
                      << "` ["
                      << vice::runtime::dade_decision_code(decision.code)
                      << "]: "
                      << decision.detail
                      << '\n';
            return 3;
        }
    }

    auto loaded = vice::runtime::load_project(launch);
    if (!loaded.project.has_value()) {
        std::cerr << "VICE Runtime could not boot the game project: " << loaded.diagnostics << '\n';
        return 1;
    }

    DefaultGameModule default_game;
    vice::runtime::GameModule* game = &default_game;
    vice::runtime::GameModuleLoadResult project_game;

    if (staged_mode) {
        const auto module_path = vice::runtime::game_module_path(
            launch.manifest.parent_path(),
            loaded.project->manifest.game_target
        );
        std::error_code error;
        const bool module_exists = std::filesystem::is_regular_file(module_path, error);
        if (error) {
            std::cerr
                << "VICE Runtime could not inspect project game module `"
                << module_path.generic_string()
                << "`: "
                << error.message()
                << '\n';
            return 1;
        }
        if (module_exists) {
            project_game = vice::runtime::load_game_module(module_path);
            if (!project_game) {
                std::cerr << "VICE Runtime could not load project game code: "
                          << project_game.diagnostics << '\n';
                return 1;
            }
            game = project_game.module.get();
        }
    }

    std::unique_ptr<DevelopmentStatusObserver> observer;
    if (development_session.has_value()) {
        observer = std::make_unique<DevelopmentStatusObserver>(
            *loaded.project,
            *development_session
        );
    }

    return vice::runtime::run_native_game(
        *loaded.project,
        *game,
        {},
        observer.get()
    );
}
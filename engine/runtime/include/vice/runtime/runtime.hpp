#pragma once

#include <vice/platform/event.hpp>
#include <vice/platform/types.hpp>
#include <vice/project/project.hpp>
#include <vice/render/rhi.hpp>
#include <vice/runtime/observation.hpp>
#include <vice/world/world.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

namespace vice::runtime {

struct LaunchPaths {
    std::filesystem::path manifest;
    std::filesystem::path cooked_cache_root;
};

struct LoadedProject {
    project::Manifest manifest;
    std::filesystem::path manifest_path;
    std::filesystem::path cooked_content_root;
    std::filesystem::path startup_world_path;
    std::unique_ptr<world::World> world;
};

struct LoadResult {
    std::optional<LoadedProject> project;
    std::string diagnostics;
};

[[nodiscard]] LaunchPaths staged_launch_paths(
    const std::filesystem::path& executable,
    std::string* diagnostics = nullptr
);

[[nodiscard]] LaunchPaths explicit_launch_paths(
    const std::filesystem::path& manifest,
    const std::filesystem::path& cooked_cache_root,
    std::string* diagnostics = nullptr
);

[[nodiscard]] LoadResult load_project(const LaunchPaths& launch);

struct GameContext {
    const project::Manifest& project;
    world::World& world;
};

class GameModule {
public:
    virtual ~GameModule() = default;

    [[nodiscard]] virtual bool on_start(GameContext& context) {
        (void)context;
        return true;
    }

    virtual void on_event(GameContext& context, const platform::Event& event) {
        (void)context;
        (void)event;
    }

    virtual void on_update(GameContext& context, double delta_seconds) {
        (void)context;
        (void)delta_seconds;
    }

    virtual void on_frame(GameContext& context, render::Color& clear_color) {
        (void)context;
        (void)clear_color;
    }

    virtual void on_stop(GameContext& context) {
        (void)context;
    }
};

class GameModuleLifecycle final {
public:
    GameModuleLifecycle(GameModule& module, GameContext context) noexcept;
    ~GameModuleLifecycle();

    GameModuleLifecycle(const GameModuleLifecycle&) = delete;
    GameModuleLifecycle& operator=(const GameModuleLifecycle&) = delete;

    [[nodiscard]] bool start();
    void event(const platform::Event& event);
    void update(double delta_seconds);
    void frame(render::Color& clear_color);
    void stop();

    [[nodiscard]] bool started() const noexcept { return started_; }
    [[nodiscard]] bool stopped() const noexcept { return stopped_; }

private:
    GameModule* module_ = nullptr;
    GameContext context_;
    bool started_ = false;
    bool stopped_ = false;
};

struct EventDecision {
    bool quit = false;
    std::optional<platform::WindowMetrics> resize;
};

[[nodiscard]] EventDecision classify_event(
    const platform::Event& event,
    platform::WindowId game_window
) noexcept;

struct ApplicationConfig {
    std::uint32_t width = 1280;
    std::uint32_t height = 720;
    bool resizable = true;
    render::Color clear_color{0.035F, 0.045F, 0.065F, 1.0F};
    float exposure = 1.0F;
};

[[nodiscard]] int run_native_game(
    LoadedProject& runtime_project,
    GameModule& module,
    const ApplicationConfig& config = {},
    RuntimeObserver* observer = nullptr
);

} // namespace vice::runtime

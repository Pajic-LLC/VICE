#include <vice/dev/cli.hpp>
#include <vice/platform/file.hpp>
#include <vice/presentation/system.hpp>
#include <vice/runtime/module_loader.hpp>
#include <vice/world/snapshot_file.hpp>
#if defined(VICE_CLI_AUTHORED_PHYSICS)
#include <vice/jobs/jobs.hpp>
#include <vice/rigid_body/authored_simulation.hpp>
#endif
#include <algorithm>
#include <charconv>
#include <exception>
#include <memory>
namespace vice::dev {
namespace {
std::string json_quote(std::string_view text) {
  std::string out = "\"";
  constexpr char hex[] = "0123456789abcdef";
  for (const char character : text) {
    const auto c = static_cast<unsigned char>(character);
    if (c == '\"' || c == '\\') {
      out += '\\';
      out += static_cast<char>(c);
    } else if (c < 32) {
      out += "\\u00";
      out += hex[c >> 4];
      out += hex[c & 15];
    } else
      out += static_cast<char>(c);
  }
  return out + '\"';
}
} // namespace
CommandResult execute_runtime_simulate(const std::filesystem::path &executable,
                                       std::string_view step_text,
                                       const std::filesystem::path &output,
                                       const std::filesystem::path &restore) {
  auto finish = [](int code, std::string_view message,
                   std::string data = "{}") {
    return CommandResult{
        code,
        "{\"schema\":\"vice.dev.v1\",\"command\":\"runtime.simulate\",\"ok\":" +
            std::string(code == 0 ? "true" : "false") + ",\"diagnostics\":[" +
            (message.empty() ? "" : json_quote(message)) +
            "],\"data\":" + data + "}\n"};
  };
  std::uint32_t steps = 0;
  const auto number = std::from_chars(
      step_text.data(), step_text.data() + step_text.size(), steps);
  if (number.ec != std::errc{} ||
      number.ptr != step_text.data() + step_text.size() || steps == 0 ||
      steps > 72000)
    return finish(exit_usage, "Steps must be an integer in [1,72000]; each "
                              "step is 1/120 simulation second.");
  try {
    std::error_code error;
    auto destination = std::filesystem::absolute(output, error);
    if (error || destination.filename().empty())
      return finish(exit_validation, "Invalid checkpoint destination.");
    auto parent = std::filesystem::canonical(destination.parent_path(), error);
    if (error)
      return finish(exit_validation, "Checkpoint parent directory must exist.");
    destination = parent / destination.filename();
    if (std::filesystem::symlink_status(destination, error).type() !=
            std::filesystem::file_type::not_found &&
        !error)
      return finish(exit_validation, "Checkpoint output must be a new file.");
    error.clear();
    if (!std::filesystem::is_regular_file(executable, error) || error)
      return finish(exit_unavailable,
                    "Staged Runtime executable is unavailable.");
    auto loaded =
        runtime::load_project(runtime::staged_launch_paths(executable));
    if (!loaded.project)
      return finish(exit_validation, loaded.diagnostics);
    auto &project = *loaded.project;
    if (!restore.empty()) {
      auto checkpoint = world::load_world_snapshot(restore);
      if (!checkpoint)
        return finish(exit_validation, checkpoint.diagnostics);
      if (!project.world->restore(*checkpoint.snapshot))
        return finish(exit_validation, "Checkpoint restore rejected.");
    }
    const auto snapshot = project.world->snapshot();
    const bool needs_physics =
        std::any_of(snapshot.entities.begin(), snapshot.entities.end(),
                    [](const auto &e) { return e.rigid_box || e.character; });
    rigid_body::AuthoredSimulation *physics = nullptr;
#if defined(VICE_CLI_AUTHORED_PHYSICS)
    std::unique_ptr<jobs::Scheduler> jobs;
    std::unique_ptr<rigid_body::AuthoredSimulation> simulation;
    if (needs_physics) {
      jobs = std::make_unique<jobs::Scheduler>(
          jobs::SchedulerConfig{.worker_count = 1});
      simulation = std::make_unique<rigid_body::AuthoredSimulation>();
      std::string diagnostics;
      if (!simulation->reset(*project.world, *jobs, &diagnostics))
        return finish(exit_validation, diagnostics);
      physics = simulation.get();
    }
#else
    if (needs_physics)
      return finish(exit_unavailable,
                    "This build has no authored physics capability.");
#endif
    presentation::System presentation;
    std::string diagnostics;
    if (!presentation.reset(*project.world, project.cooked_content_root,
                            &diagnostics))
      return finish(exit_validation, diagnostics);
    auto module = runtime::load_game_module(runtime::game_module_path(
        project.manifest_path.parent_path(), project.manifest.game_target));
    if (!module)
      return finish(exit_unavailable, module.diagnostics);
    runtime::GameModuleLifecycle lifecycle{
        *module.module,
        {project.manifest, *project.world, physics, &presentation}};
    if (!lifecycle.start())
      return finish(exit_validation, "Game module rejected headless startup.");
    constexpr double dt = 1.0 / 120;
    for (std::uint32_t i = 0; i < steps; ++i) {
      if (!project.world->advance_world_clock(dt))
        return finish(exit_validation, "World clock could not advance.");
      lifecycle.update(dt);
#if defined(VICE_CLI_AUTHORED_PHYSICS)
      if (physics && !physics->step(&diagnostics))
        return finish(exit_validation, diagnostics);
#endif
      if (!presentation.advance(dt, &diagnostics))
        return finish(exit_validation, diagnostics);
      render::Color color;
      lifecycle.frame(color);
    }
    lifecycle.stop();
    auto encoded = world::encode_world_snapshot(project.world->snapshot());
    if (!encoded)
      return finish(exit_validation, encoded.diagnostics);
    const auto written = platform::replace_file_if_unchanged(
        destination, std::nullopt, encoded.bytes);
    if (written.state != platform::FileReplaceState::updated)
      return finish(exit_validation, written.diagnostics);
    return finish(
        exit_success, {},
        "{\"project_id\":" + json_quote(project.manifest.id.to_string()) +
            ",\"steps\":" + std::to_string(steps) +
            ",\"step_seconds\":0.008333333333333333,\"world_ticks\":" +
            json_quote(std::to_string(project.world->world_clock().ticks)) +
            ",\"checkpoint\":" + json_quote(destination.generic_string()) +
            ",\"entities\":" + std::to_string(project.world->entity_count()) +
            ",\"physics\":" + (needs_physics ? "true" : "false") +
            ",\"rendered\":false,\"audio_output\":false}");
  } catch (const std::exception &e) {
    return finish(exit_unavailable, e.what());
  }
}
} // namespace vice::dev

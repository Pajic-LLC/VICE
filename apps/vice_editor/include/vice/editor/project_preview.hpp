#pragma once
#include <vice/editor/preview_runtime.hpp>
#include <vice/editor/project_session.hpp>
namespace vice::editor {
// Compiles the current module without saving or cooking the in-memory document.
[[nodiscard]] bool reset_project_preview(PreviewRuntime& preview, const ProjectSession& session,
    const world::World& authored_world, jobs::Scheduler& jobs, const AssetIndex& assets,
    std::string* diagnostics = nullptr);
}

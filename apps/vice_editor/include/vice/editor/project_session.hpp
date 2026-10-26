#pragma once

#include <vice/project/workspace.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace vice::editor {

enum class DirtyReplacePolicy {
    reject,
    discard,
};

class ProjectSession final {
public:
    [[nodiscard]] static std::optional<ProjectSession> open(
        const std::filesystem::path& manifest_path,
        const std::filesystem::path& cooked_cache_base,
        const std::filesystem::path& editor_state_base,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] const project::Workspace& workspace() const noexcept { return workspace_; }
    [[nodiscard]] const project::Manifest& manifest() const noexcept { return workspace_.manifest; }
    [[nodiscard]] const std::filesystem::path& manifest_path() const noexcept {
        return workspace_.manifest_path;
    }
    [[nodiscard]] const std::filesystem::path& asset_root() const noexcept {
        return workspace_.asset_root;
    }
    [[nodiscard]] const std::filesystem::path& startup_world_source() const noexcept {
        return workspace_.startup_world_source;
    }
    [[nodiscard]] const std::filesystem::path& cooked_root() const noexcept {
        return workspace_.cooked_root;
    }
    [[nodiscard]] const std::filesystem::path& cooked_startup_world() const noexcept {
        return workspace_.cooked_startup_world;
    }
    [[nodiscard]] const std::filesystem::path& game_output_root() const noexcept {
        return workspace_.game_output_root;
    }
    [[nodiscard]] const std::filesystem::path& editor_state_root() const noexcept {
        return editor_state_root_;
    }
    [[nodiscard]] const std::filesystem::path& layout_path() const noexcept { return layout_path_; }
    [[nodiscard]] const std::filesystem::path& working_world_path() const noexcept {
        return working_world_path_;
    }

    bool apply_manifest(project::Manifest manifest, std::string* diagnostics = nullptr) {
        const auto fail = [&](const std::string& message) {
            if (diagnostics != nullptr) *diagnostics = message;
            return false;
        };

        std::string validation;
        if (!project::validate(manifest, &validation)) return fail(validation);
        if (manifest.id != workspace_.manifest.id) {
            return fail("Project settings cannot change the stable Project ID.");
        }
        if (manifest.asset_root != workspace_.manifest.asset_root) {
            return fail("Project settings cannot change the asset root in this editor version.");
        }

        const auto startup_source = project::resolve_startup_world(workspace_.manifest_path, manifest);
        if (startup_source.empty()) return fail("Project startup World could not be resolved.");
        auto cooked_relative = manifest.startup_world;
        cooked_relative.replace_extension(".vworldc");
        const auto cooked_startup = (workspace_.cooked_root / cooked_relative).lexically_normal();
        const auto game_output = project::resolve_game_output_root(workspace_.manifest_path, manifest);
        if (game_output.empty()) return fail("Project game output root could not be resolved.");

        workspace_.manifest = std::move(manifest);
        workspace_.startup_world_source = startup_source;
        workspace_.cooked_startup_world = cooked_startup;
        workspace_.game_output_root = game_output;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

private:
    project::Workspace workspace_;
    std::filesystem::path editor_state_root_;
    std::filesystem::path layout_path_;
    std::filesystem::path working_world_path_;
};

class WorldDocument final {
public:
    WorldDocument();
    ~WorldDocument();

    WorldDocument(const WorldDocument&) = delete;
    WorldDocument& operator=(const WorldDocument&) = delete;
    WorldDocument(WorldDocument&&) noexcept;
    WorldDocument& operator=(WorldDocument&&) noexcept;

    [[nodiscard]] world::World& world() noexcept { return *world_; }
    [[nodiscard]] const world::World& world() const noexcept { return *world_; }
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] bool has_path() const noexcept { return !path_.empty(); }
    [[nodiscard]] bool authored() const noexcept { return path_.extension() == ".vworld"; }

    void mark_dirty() noexcept { dirty_ = true; }
    void mark_clean() noexcept { dirty_ = false; }
    void set_dirty(bool dirty) noexcept { dirty_ = dirty; }

    bool new_empty(
        DirtyReplacePolicy policy = DirtyReplacePolicy::reject,
        std::string* diagnostics = nullptr
    );

    bool open(
        const std::filesystem::path& path,
        DirtyReplacePolicy policy = DirtyReplacePolicy::reject,
        std::string* diagnostics = nullptr
    );
    bool open_authored(
        const std::filesystem::path& path,
        DirtyReplacePolicy policy = DirtyReplacePolicy::reject,
        std::string* diagnostics = nullptr
    );

    bool save(std::string* diagnostics = nullptr);
    bool save_as(const std::filesystem::path& path, std::string* diagnostics = nullptr);
    bool save_authored(std::string* diagnostics = nullptr);
    bool save_authored_as(const std::filesystem::path& path, std::string* diagnostics = nullptr);

private:
    [[nodiscard]] bool replacement_allowed(
        DirtyReplacePolicy policy,
        std::string* diagnostics
    ) const;

    std::unique_ptr<world::World> world_;
    std::filesystem::path path_;
    bool dirty_ = false;
};

[[nodiscard]] std::optional<std::filesystem::path> resolve_project_authored_world_path(
    const ProjectSession& session,
    const std::filesystem::path& requested,
    bool require_existing,
    std::string* diagnostics = nullptr
);

} // namespace vice::editor

#pragma once

#include <vice/project/project.hpp>
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

    [[nodiscard]] const project::Manifest& manifest() const noexcept { return manifest_; }
    [[nodiscard]] const std::filesystem::path& manifest_path() const noexcept { return manifest_path_; }
    [[nodiscard]] const std::filesystem::path& asset_root() const noexcept { return asset_root_; }
    [[nodiscard]] const std::filesystem::path& startup_world_source() const noexcept {
        return startup_world_source_;
    }
    [[nodiscard]] const std::filesystem::path& cooked_root() const noexcept { return cooked_root_; }
    [[nodiscard]] const std::filesystem::path& cooked_startup_world() const noexcept {
        return cooked_startup_world_;
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
        if (manifest.id != manifest_.id) {
            return fail("Project settings cannot change the stable Project ID.");
        }
        if (manifest.asset_root != manifest_.asset_root) {
            return fail("Project settings cannot change the asset root in this editor version.");
        }

        const auto startup_source = project::resolve_startup_world(manifest_path_, manifest);
        if (startup_source.empty()) return fail("Project startup World could not be resolved.");
        auto cooked_relative = manifest.startup_world;
        cooked_relative.replace_extension(".vworldc");
        const auto cooked_startup = (cooked_root_ / cooked_relative).lexically_normal();

        manifest_ = std::move(manifest);
        startup_world_source_ = startup_source;
        cooked_startup_world_ = cooked_startup;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

private:
    project::Manifest manifest_;
    std::filesystem::path manifest_path_;
    std::filesystem::path asset_root_;
    std::filesystem::path startup_world_source_;
    std::filesystem::path cooked_root_;
    std::filesystem::path cooked_startup_world_;
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

    bool save(std::string* diagnostics = nullptr);
    bool save_as(const std::filesystem::path& path, std::string* diagnostics = nullptr);

private:
    [[nodiscard]] bool replacement_allowed(
        DirtyReplacePolicy policy,
        std::string* diagnostics
    ) const;

    std::unique_ptr<world::World> world_;
    std::filesystem::path path_;
    bool dirty_ = false;
};

} // namespace vice::editor

#pragma once

#include <vice/project/project.hpp>
#include <vice/world/world.hpp>

#include <filesystem>
#include <memory>
#include <optional>
#include <string>

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

    bool set_startup_world(
        const std::filesystem::path& asset_relative_world,
        std::string* diagnostics = nullptr
    );

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

} // namespace vice::editor

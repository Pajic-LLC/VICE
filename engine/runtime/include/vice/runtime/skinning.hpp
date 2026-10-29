#pragma once
#include <memory>
#include <vice/runtime/scene.hpp>
namespace vice::runtime {
struct SkinningMeshView {
    world::ResourceId resource{};
    const render::MeshAsset *asset = nullptr;
    std::span<const render::MeshVertex> previous;
    bool changed = false;
    math::Vec3 minimum{}, maximum{};
};
// Derived rendering data only. No generated resource ID or vertex pose enters World/save truth.
class RuntimeSkinningCache {
  public:
    RuntimeSkinningCache();
    ~RuntimeSkinningCache();
    RuntimeSkinningCache(const RuntimeSkinningCache &) = delete;
    RuntimeSkinningCache &operator=(const RuntimeSkinningCache &) = delete;
    bool reset(const std::filesystem::path &, std::string *diagnostics = nullptr);
    void begin_frame();
    bool evaluate(const world::World &, world::PersistentId, const render::MeshAsset &,
                  SkinningMeshView &, std::string *diagnostics = nullptr);
    void end_frame();
    [[nodiscard]] std::optional<SkinningMeshView> find(world::ResourceId) const;
    [[nodiscard]] SkinningInfo info() const noexcept;
    [[nodiscard]] std::vector<SkinningObservation> observations(world::PersistentId endpoint) const;
    bool apply(const world::World &, CookedMeshCatalog &, std::vector<RenderMeshInstance> &,
               std::string *diagnostics = nullptr);

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vice::runtime

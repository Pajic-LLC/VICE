#pragma once
#include <vice/world/resource.hpp>
#include <cmath>
#include <cstddef>
#include <optional>
#include <string>
namespace vice::world {
inline constexpr std::size_t max_world_materials=4096;
// Shared persistent identity; render resources and solver material IDs are derived bindings.
struct MaterialDefinition {
    PersistentId id{};
    std::string name;
    float density=1000, friction=0.5F, restitution=0;
    ResourceId render_material{};
    friend bool operator==(const MaterialDefinition&,const MaterialDefinition&)=default;
};
[[nodiscard]] inline bool valid(const MaterialDefinition& m) noexcept {
    if(m.id.is_nil() || m.name.empty() || m.name.size()>128 || !std::isfinite(m.density) ||
       m.density<0.000001F || m.density>1.0e9F || !std::isfinite(m.friction) || m.friction<0 ||
       !std::isfinite(m.restitution) || m.restitution<0 || m.restitution>1) return false;
    for(const char c:m.name) if(static_cast<unsigned char>(c)<32U) return false;
    return true;
}
struct MaterialSurface {
    PersistentId id{};
    ResourceId render_material{};
    float density=1000, friction=0.5F, restitution=0;
    double volume=0, mass=0;
};
struct WorldSnapshot;
[[nodiscard]] bool validate_materials(const WorldSnapshot&,std::string* diagnostics=nullptr);
[[nodiscard]] std::string serialize_material_definition(const MaterialDefinition&);
[[nodiscard]] std::string serialize_material_surface(const std::optional<MaterialSurface>&);
} // namespace vice::world

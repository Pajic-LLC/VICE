#pragma once
#include <array>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <vector>
#include <vice/world/world.hpp>
namespace vice::presentation {
struct Pose {
    math::Vec3 translation{};
    math::Quat rotation{};
    float scale = 1;
};
struct Node {
    std::string name;
    int parent = -1;
    Pose rest;
    world::ResourceId mesh{};
};
enum class Path { translation, rotation, scale };
enum class Interpolation { linear, step, cubic_spline };
struct Key {
    double time = 0;
    std::array<float, 4> value{};
    std::array<float, 4> in_tangent{}, out_tangent{};
};
struct Track {
    std::uint32_t node = 0;
    Path path = Path::rotation;
    Interpolation interpolation = Interpolation::linear;
    std::vector<Key> keys;
};
struct Clip {
    std::string name;
    double duration = 0;
    std::vector<Track> tracks;
};
inline constexpr std::size_t max_skin_influences = 8, max_skin_vertices = 100000;
struct SkinWeights {
    std::array<std::uint16_t, max_skin_influences> joints{};
    std::array<float, max_skin_influences> weights{};
};
struct SkinJoint {
    std::uint32_t node = 0;
    math::Mat4 inverse_bind = math::identity();
};
struct Skin {
    std::uint32_t node = 0;
    std::vector<SkinJoint> joints;
    std::vector<SkinWeights> vertices;
};
struct SkinVertex {
    math::Vec3 position{}, normal{0, 1, 0};
};
struct Rig {
    world::ResourceId id{};
    std::vector<Node> nodes;
    std::vector<Clip> clips;
    std::vector<Skin> skins;
};
struct Sound {
    world::ResourceId id{};
    std::vector<float> samples;
    std::uint32_t sample_rate = 48000;
};
[[nodiscard]] bool valid(const Rig &rig, std::string *diagnostics = nullptr);
[[nodiscard]] std::optional<Rig> read_rig(const std::filesystem::path &path,
                                          std::string *diagnostics = nullptr);
[[nodiscard]] std::string serialize_rig(const Rig &rig);
[[nodiscard]] bool sample_blended_pose(const Rig &, std::uint32_t, double, std::uint32_t, double, float, std::vector<Pose> &);
[[nodiscard]] bool sample_pose(const Rig &rig, std::uint32_t clip, double time,
                               std::vector<Pose> &pose);
// Shared derived pose: physical nodes override animation; unbound descendants follow.
// Output remains unchanged on failure.
[[nodiscard]] bool sample_world_pose(const world::World &, world::EntityId, const Rig &,
                                      const world::AnimationPlayer &, std::vector<Pose> &);
[[nodiscard]] bool sample_world_pose(const world::World &, world::EntityId root, const Rig &,
                                     std::uint32_t clip, double time, std::vector<Pose> &);
[[nodiscard]] std::optional<Sound> read_pcm(const std::filesystem::path &path,
                                            std::string *diagnostics = nullptr);
[[nodiscard]] bool write_pcm(const std::filesystem::path &path, const Sound &sound,
                             std::string *diagnostics = nullptr);
[[nodiscard]] Pose compose(const Pose &parent, const Pose &child);
[[nodiscard]] const Skin *find_skin(const Rig &, std::uint32_t node) noexcept;
[[nodiscard]] bool valid_skin(const Rig &, const Skin &, std::string *diagnostics = nullptr);
[[nodiscard]] bool skin_palette(const Skin &, std::span<const Pose>, std::vector<math::Mat4> &);
// Pure reference deformation in rig space. Output is unchanged on invalid/singular input.
[[nodiscard]] bool deform_skin(const Skin &, std::span<const math::Mat4>,
                               std::span<const SkinVertex>, std::vector<SkinVertex> &);
} // namespace vice::presentation

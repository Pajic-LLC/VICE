#pragma once
#include <array>
#include <filesystem>
#include <optional>
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
enum class Interpolation { linear, step };
struct Key {
    double time = 0;
    std::array<float, 4> value{};
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
struct Rig {
    world::ResourceId id{};
    std::vector<Node> nodes;
    std::vector<Clip> clips;
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
[[nodiscard]] bool sample_pose(const Rig &rig, std::uint32_t clip, double time,
                               std::vector<Pose> &pose);
[[nodiscard]] std::optional<Sound> read_pcm(const std::filesystem::path &path,
                                            std::string *diagnostics = nullptr);
[[nodiscard]] bool write_pcm(const std::filesystem::path &path, const Sound &sound,
                             std::string *diagnostics = nullptr);
[[nodiscard]] Pose compose(const Pose &parent, const Pose &child);
} // namespace vice::presentation

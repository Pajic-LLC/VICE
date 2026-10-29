#pragma once
#include <memory>
#include <span>
#include <vice/presentation/assets.hpp>
namespace vice::presentation {
struct AudioObservation {
    double duration = 0;
    float left_gain = 0, right_gain = 0, distance = 0;
    bool output_active = false;
    std::uint64_t delivered_frames = 0, underrun_frames = 0;
};
struct AnimationObservation {
    std::string clip_name;
    double duration = 0;
    std::size_t node_count = 0, part_count = 0, skin_count = 0, joint_count = 0,
                weighted_vertices = 0;
};
class System {
  public:
    System();
    ~System();
    System(const System &) = delete;
    System &operator=(const System &) = delete;
    bool reset(world::World &world, const std::filesystem::path &cooked_root,
               std::string *diagnostics = nullptr);
    bool advance(double seconds, std::string *diagnostics = nullptr);
    bool control_animation(world::PersistentId entity, std::uint32_t clip, float rate, bool playing,
                           bool looping, bool restart);
    bool control_audio(world::PersistentId entity, float gain, float pitch, bool playing,
                       bool restart);
    [[nodiscard]] std::optional<AnimationObservation> animation(world::PersistentId entity) const;
    [[nodiscard]] std::optional<AudioObservation> audio(world::PersistentId entity) const;
    // Deterministic stereo mixer, no device required; does not advance World time.
    [[nodiscard]] std::vector<float> render_audio(std::size_t frames) const;
    bool start_audio();
    void stop_audio();
    [[nodiscard]] bool enabled() const noexcept;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace vice::presentation

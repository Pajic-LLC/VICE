#pragma once

#include <vice/render/rhi.hpp>

#include <filesystem>
#include <memory>
#include <string>

namespace vice::runtime::detail {

struct RuntimeEnvironmentBinding {
    render::Texture* irradiance = nullptr;
    render::Texture* specular = nullptr;
    render::Texture* brdf_lut = nullptr;
    render::Sampler* sampler = nullptr;
    float intensity = 0.0F;
    float rotation_radians = 0.0F;
    float max_specular_lod = 0.0F;
    bool available = false;
};

class RuntimeEnvironment final {
public:
    RuntimeEnvironment();
    ~RuntimeEnvironment();

    RuntimeEnvironment(const RuntimeEnvironment&) = delete;
    RuntimeEnvironment& operator=(const RuntimeEnvironment&) = delete;
    RuntimeEnvironment(RuntimeEnvironment&&) noexcept;
    RuntimeEnvironment& operator=(RuntimeEnvironment&&) noexcept;

    [[nodiscard]] bool initialize(
        render::Device& device,
        const std::filesystem::path& content_root,
        std::string* diagnostics = nullptr
    );

    [[nodiscard]] RuntimeEnvironmentBinding binding() const noexcept;

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace vice::runtime::detail

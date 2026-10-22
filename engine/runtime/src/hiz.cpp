#include <vice/runtime/hiz.hpp>

#include "hiz_shaders.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr std::uint32_t hiz_group_size = 8U;

struct HiZConstants {
    std::uint32_t source_width = 0U;
    std::uint32_t source_height = 0U;
    std::uint32_t target_width = 0U;
    std::uint32_t target_height = 0U;
};

static_assert(sizeof(HiZConstants) == 16U);

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] std::uint32_t dispatch_groups(const std::uint32_t extent) noexcept {
    return (extent + hiz_group_size - 1U) / hiz_group_size;
}

} // namespace

std::vector<RuntimeHiZLevelSize> runtime_hiz_level_sizes(
    const std::uint32_t width,
    const std::uint32_t height
) {
    std::vector<RuntimeHiZLevelSize> result;
    if (width == 0U || height == 0U) return result;

    std::uint32_t level_width = width;
    std::uint32_t level_height = height;
    for (;;) {
        result.push_back(RuntimeHiZLevelSize{
            .width = level_width,
            .height = level_height,
        });
        if (level_width == 1U && level_height == 1U) break;
        level_width = std::max(1U, (level_width + 1U) / 2U);
        level_height = std::max(1U, (level_height + 1U) / 2U);
    }
    return result;
}

struct RuntimeHiZPyramid::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> copy_shader;
    std::unique_ptr<render::ShaderModule> reduce_shader;
    std::unique_ptr<render::ComputePipeline> copy_pipeline;
    std::unique_ptr<render::ComputePipeline> reduce_pipeline;
    std::unique_ptr<render::Sampler> sampler;
    std::vector<RuntimeHiZLevelSize> level_sizes;
    std::vector<std::unique_ptr<render::Texture>> levels;
    std::vector<std::unique_ptr<render::Buffer>> constants;
};

RuntimeHiZPyramid::RuntimeHiZPyramid() : state_{std::make_unique<State>()} {}
RuntimeHiZPyramid::~RuntimeHiZPyramid() = default;
RuntimeHiZPyramid::RuntimeHiZPyramid(RuntimeHiZPyramid&&) noexcept = default;
RuntimeHiZPyramid& RuntimeHiZPyramid::operator=(RuntimeHiZPyramid&&) noexcept = default;

bool RuntimeHiZPyramid::initialize(
    render::Device& device,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime Hi-Z state is unavailable.");
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;

    render::ShaderModuleDesc copy_desc;
    copy_desc.stage = render::ShaderStage::compute;
    copy_desc.language = metal
        ? render::ShaderSourceLanguage::metal
        : render::ShaderSourceLanguage::hlsl;
    copy_desc.source = metal ? detail::hiz_copy_metal_source : detail::hiz_copy_hlsl_source;
    copy_desc.resources = detail::hiz_resources;
    copy_desc.entry_point = "vice_runtime_hiz_copy_cs";
    copy_desc.debug_name = "VICE Runtime Hi-Z depth copy";
    auto copy_shader = device.create_shader_module(copy_desc);

    render::ShaderModuleDesc reduce_desc = copy_desc;
    reduce_desc.source = metal ? detail::hiz_reduce_metal_source : detail::hiz_reduce_hlsl_source;
    reduce_desc.entry_point = "vice_runtime_hiz_reduce_cs";
    reduce_desc.debug_name = "VICE Runtime Hi-Z reduce";
    auto reduce_shader = device.create_shader_module(reduce_desc);

    if (!copy_shader || !reduce_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create Hi-Z compute shaders.");
        return false;
    }

    render::ComputePipelineDesc copy_pipeline_desc;
    copy_pipeline_desc.compute_shader = copy_shader.get();
    copy_pipeline_desc.thread_group_size = {hiz_group_size, hiz_group_size, 1U};
    copy_pipeline_desc.debug_name = "VICE Runtime Hi-Z depth copy pipeline";
    auto copy_pipeline = device.create_compute_pipeline(copy_pipeline_desc);

    render::ComputePipelineDesc reduce_pipeline_desc = copy_pipeline_desc;
    reduce_pipeline_desc.compute_shader = reduce_shader.get();
    reduce_pipeline_desc.debug_name = "VICE Runtime Hi-Z reduce pipeline";
    auto reduce_pipeline = device.create_compute_pipeline(reduce_pipeline_desc);

    render::SamplerDesc sampler_desc;
    sampler_desc.min_filter = render::Filter::nearest;
    sampler_desc.mag_filter = render::Filter::nearest;
    sampler_desc.mip_filter = render::Filter::nearest;
    sampler_desc.address_u = render::AddressMode::clamp_to_edge;
    sampler_desc.address_v = render::AddressMode::clamp_to_edge;
    sampler_desc.address_w = render::AddressMode::clamp_to_edge;
    sampler_desc.debug_name = "VICE Runtime Hi-Z point sampler";
    auto sampler = device.create_sampler(sampler_desc);

    if (!copy_pipeline || !reduce_pipeline || !sampler) {
        set_diagnostics(diagnostics, "VICE Runtime could not create Hi-Z pipeline resources.");
        return false;
    }

    state_->device = &device;
    state_->copy_shader = std::move(copy_shader);
    state_->reduce_shader = std::move(reduce_shader);
    state_->copy_pipeline = std::move(copy_pipeline);
    state_->reduce_pipeline = std::move(reduce_pipeline);
    state_->sampler = std::move(sampler);
    state_->level_sizes.clear();
    state_->levels.clear();
    state_->constants.clear();
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeHiZPyramid::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || !state_->copy_pipeline ||
        !state_->reduce_pipeline || !state_->sampler) {
        set_diagnostics(diagnostics, "Runtime Hi-Z pyramid is not initialized.");
        return false;
    }

    const auto sizes = runtime_hiz_level_sizes(width, height);
    if (sizes.empty()) {
        set_diagnostics(diagnostics, "Runtime Hi-Z rejected an empty render extent.");
        return false;
    }
    if (sizes == state_->level_sizes && state_->levels.size() == sizes.size() &&
        state_->constants.size() == sizes.size()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    std::vector<std::unique_ptr<render::Texture>> levels;
    std::vector<std::unique_ptr<render::Buffer>> constants;
    levels.reserve(sizes.size());
    constants.reserve(sizes.size());

    for (std::size_t index = 0U; index < sizes.size(); ++index) {
        const auto size = sizes[index];

        render::TextureDesc texture_desc;
        texture_desc.width = size.width;
        texture_desc.height = size.height;
        texture_desc.format = render::TextureFormat::r32_float;
        texture_desc.usage = render::TextureUsage::sampled | render::TextureUsage::storage;
        texture_desc.debug_name = "VICE Runtime Hi-Z level";
        auto texture = state_->device->create_texture(texture_desc);

        render::BufferDesc constants_desc;
        constants_desc.size = sizeof(HiZConstants);
        constants_desc.usage = render::BufferUsage::uniform;
        constants_desc.memory = render::MemoryLocation::cpu_to_gpu;
        constants_desc.debug_name = "VICE Runtime Hi-Z dispatch constants";
        auto dispatch_constants = state_->device->create_buffer(constants_desc);

        if (!texture || !dispatch_constants) {
            set_diagnostics(diagnostics, "VICE Runtime could not allocate Hi-Z pyramid resources.");
            return false;
        }
        levels.push_back(std::move(texture));
        constants.push_back(std::move(dispatch_constants));
    }

    state_->level_sizes = sizes;
    state_->levels = std::move(levels);
    state_->constants = std::move(constants);
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeHiZPyramid::build(
    render::CommandList& commands,
    render::Texture& camera_depth,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr || state_->levels.empty() ||
        state_->levels.size() != state_->level_sizes.size() ||
        state_->constants.size() != state_->levels.size() || !state_->copy_pipeline ||
        !state_->reduce_pipeline || !state_->sampler) {
        set_diagnostics(diagnostics, "Runtime Hi-Z pyramid is not ready to build.");
        return false;
    }
    const auto base_size = state_->level_sizes.front();
    if (camera_depth.format() != render::TextureFormat::depth32_float ||
        !render::has_usage(camera_depth.usage(), render::TextureUsage::sampled) ||
        camera_depth.width() != base_size.width || camera_depth.height() != base_size.height) {
        set_diagnostics(diagnostics, "Runtime Hi-Z depth source does not match the pyramid contract.");
        return false;
    }

    for (std::size_t index = 0U; index < state_->levels.size(); ++index) {
        const auto target_size = state_->level_sizes[index];
        const auto source_size = index == 0U ? base_size : state_->level_sizes[index - 1U];
        HiZConstants constants{
            .source_width = source_size.width,
            .source_height = source_size.height,
            .target_width = target_size.width,
            .target_height = target_size.height,
        };
        if (!state_->constants[index]->write(
                0U,
                std::as_bytes(std::span{&constants, 1})
            )) {
            set_diagnostics(diagnostics, "VICE Runtime could not write Hi-Z dispatch constants.");
            return false;
        }

        render::Texture* const source = index == 0U
            ? &camera_depth
            : state_->levels[index - 1U].get();
        render::Texture* const target = state_->levels[index].get();
        render::ComputePipeline* const pipeline = index == 0U
            ? state_->copy_pipeline.get()
            : state_->reduce_pipeline.get();

        if (source == nullptr || target == nullptr || pipeline == nullptr ||
            !commands.set_compute_pipeline(*pipeline) ||
            !commands.set_compute_constant_buffer(
                detail::hiz_constants_binding,
                *state_->constants[index],
                0U,
                sizeof(HiZConstants)
            ) ||
            !commands.set_compute_texture(detail::hiz_source_binding, *source) ||
            !commands.set_compute_storage_texture(detail::hiz_output_binding, *target) ||
            !commands.set_compute_sampler(detail::hiz_sampler_binding, *state_->sampler) ||
            !commands.dispatch(render::ComputeDispatchDesc{
                .group_count_x = dispatch_groups(target_size.width),
                .group_count_y = dispatch_groups(target_size.height),
                .group_count_z = 1U,
            })) {
            set_diagnostics(diagnostics, "VICE Runtime could not encode the Hi-Z pyramid build.");
            return false;
        }
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

std::uint32_t RuntimeHiZPyramid::level_count() const noexcept {
    if (state_ == nullptr || state_->levels.size() > std::numeric_limits<std::uint32_t>::max()) {
        return 0U;
    }
    return static_cast<std::uint32_t>(state_->levels.size());
}

RuntimeHiZLevelSize RuntimeHiZPyramid::level_size(const std::uint32_t level_index) const noexcept {
    if (state_ == nullptr || level_index >= state_->level_sizes.size()) return {};
    return state_->level_sizes[level_index];
}

render::Texture* RuntimeHiZPyramid::level(const std::uint32_t level_index) const noexcept {
    if (state_ == nullptr || level_index >= state_->levels.size()) return nullptr;
    return state_->levels[level_index].get();
}

} // namespace vice::runtime

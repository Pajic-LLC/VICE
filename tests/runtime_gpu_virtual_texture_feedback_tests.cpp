#include <vice/runtime/gpu_virtual_texture_feedback.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/resource.hpp>

#include <cassert>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] vice::world::ResourceId required_resource(const char* const text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return vice::world::ResourceId{*parsed};
}

} // namespace

int main() {
    using namespace vice;
    using namespace vice::runtime;

    const auto texture = required_resource("71000000-0000-4000-8000-000000000001");

    RuntimeVirtualTextureSamplingPlan sampling;
    sampling.page_keys = {
        RuntimeVirtualTextureTileKey{.texture = texture, .mip_level = 0U, .tile_x = 0U, .tile_y = 0U},
        RuntimeVirtualTextureTileKey{.texture = texture, .mip_level = 0U, .tile_x = 1U, .tile_y = 0U},
        RuntimeVirtualTextureTileKey{.texture = texture, .mip_level = 1U, .tile_x = 1U, .tile_y = 0U},
    };
    sampling.pages = {
        RuntimeGpuVirtualTexturePage{.physical_slot = 0U, .resolved_mip = 2U, .resolved_tile_x = 0U, .resolved_tile_y = 0U},
        RuntimeGpuVirtualTexturePage{.physical_slot = 0U, .resolved_mip = 2U, .resolved_tile_x = 0U, .resolved_tile_y = 0U},
        RuntimeGpuVirtualTexturePage{.physical_slot = 1U, .resolved_mip = 1U, .resolved_tile_x = 1U, .resolved_tile_y = 0U},
    };

    const std::vector<std::uint32_t> flags{1U, 1U, 1U};
    const auto feedback = build_runtime_gpu_virtual_texture_feedback_requests(sampling, flags);
    assert(feedback);
    assert(feedback.requests.size() == 1U);
    const RuntimeVirtualTextureTileKey expected{
        .texture = texture,
        .mip_level = 1U,
        .tile_x = 0U,
        .tile_y = 0U,
    };
    assert(feedback.requests.front() == expected);

    const std::vector<std::uint32_t> exact_flags{0U, 0U, 1U};
    const auto exact = build_runtime_gpu_virtual_texture_feedback_requests(sampling, exact_flags);
    assert(exact);
    assert(exact.requests.empty());

    auto malformed = sampling;
    malformed.page_keys.pop_back();
    const auto misaligned = build_runtime_gpu_virtual_texture_feedback_requests(malformed, flags);
    assert(!misaligned);
    assert(!misaligned.diagnostics.empty());

    const std::vector<std::uint32_t> short_flags{1U};
    const auto short_result = build_runtime_gpu_virtual_texture_feedback_requests(sampling, short_flags);
    assert(!short_result);

    auto excessive = sampling;
    excessive.page_keys = {
        RuntimeVirtualTextureTileKey{.texture = texture, .mip_level = 0U, .tile_x = 0U, .tile_y = 0U},
    };
    excessive.pages = {
        RuntimeGpuVirtualTexturePage{.physical_slot = 0U, .resolved_mip = 33U},
    };
    const std::vector<std::uint32_t> excessive_flags{1U};
    const auto excessive_result = build_runtime_gpu_virtual_texture_feedback_requests(
        excessive, excessive_flags
    );
    assert(!excessive_result);

    return 0;
}

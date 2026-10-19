#include <vice/render/rhi.hpp>

#include <cassert>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

[[nodiscard]] constexpr std::uint32_t mip_extent(
    std::uint32_t extent,
    std::uint32_t level
) noexcept {
    while (level-- > 0U && extent > 1U) extent /= 2U;
    return extent == 0U ? 1U : extent;
}

} // namespace

int main() {
    using namespace vice::render;

    auto device = Device::create();
    assert(device != nullptr);

    TextureDesc desc;
    desc.dimension = TextureDimension::d2;
    desc.width = 8U;
    desc.height = 4U;
    desc.depth = 1U;
    desc.mip_levels = 4U;
    desc.array_layers = 1U;
    desc.sample_count = 1U;
    desc.format = TextureFormat::rgba8_unorm;
    desc.usage = TextureUsage::sampled | TextureUsage::copy_destination;
    desc.debug_name = "VICE mip upload test";

    auto texture = device->create_texture(desc);
    assert(texture != nullptr);
    assert(texture->mip_levels() == 4U);

    for (std::uint32_t mip = 0U; mip < desc.mip_levels; ++mip) {
        const std::uint32_t width = mip_extent(desc.width, mip);
        const std::uint32_t height = mip_extent(desc.height, mip);
        std::vector<std::byte> pixels(static_cast<std::size_t>(width) * height * 4U);
        for (std::size_t index = 0U; index < pixels.size(); ++index) {
            pixels[index] = static_cast<std::byte>((index + mip * 37U) & 0xFFU);
        }
        assert(texture->write_2d(pixels, static_cast<std::uint64_t>(width) * 4U, mip));
    }

    const std::vector<std::byte> invalid(4U, std::byte{0U});
    assert(!texture->write_2d(invalid, 4U, desc.mip_levels));
    assert(device->wait_idle());
    return 0;
}

#include <vice/render/texture_asset.hpp>
#include <vice/tools/texture_cooker.hpp>

#include <cassert>
#include <cstddef>
#include <span>
#include <string_view>
#include <vector>

namespace {

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto data = std::as_bytes(std::span{text.data(), text.size()});
    return {data.begin(), data.end()};
}

} // namespace

int main() {
    using namespace vice;

    constexpr std::string_view source = R"(
vice-texture-source 1
id 71000000-0000-4000-8000-000000000001
image "textures/checker.ppm"
color_space srgb
)";
    const auto parsed = tools::texture_cooker::parse_source_manifest(source);
    assert(parsed.source.has_value());
    assert(parsed.diagnostics.empty());
    assert(parsed.source->image_source.generic_string() == "textures/checker.ppm");
    assert(parsed.source->color_space == render::TextureColorSpace::srgb);

    constexpr std::string_view ppm_p3 = R"(P3
# simple two pixel image
2 1
255
255 0 0  0 128 255
)";
    const auto p3 = tools::texture_cooker::decode_ppm(bytes(ppm_p3));
    assert(p3.image.has_value());
    assert(p3.image->width == 2U);
    assert(p3.image->height == 1U);
    assert(p3.image->rgba8.size() == 8U);
    assert(std::to_integer<unsigned int>(p3.image->rgba8[0]) == 255U);
    assert(std::to_integer<unsigned int>(p3.image->rgba8[5]) == 128U);
    assert(std::to_integer<unsigned int>(p3.image->rgba8[6]) == 255U);
    assert(std::to_integer<unsigned int>(p3.image->rgba8[7]) == 255U);

    std::vector<std::byte> p6 = bytes("P6\n2 1\n255\n");
    const std::byte p6_pixels[]{
        std::byte{255U}, std::byte{0U}, std::byte{0U},
        std::byte{0U}, std::byte{128U}, std::byte{255U},
    };
    p6.insert(p6.end(), std::begin(p6_pixels), std::end(p6_pixels));
    const auto decoded_p6 = tools::texture_cooker::decode_ppm(p6);
    assert(decoded_p6.image.has_value());
    assert(decoded_p6.image->rgba8 == p3.image->rgba8);

    const auto cooked = tools::texture_cooker::cook(*parsed.source, bytes(ppm_p3));
    assert(cooked.success);
    assert(cooked.source_fingerprint != 0U);
    assert(!cooked.bytes.empty());
    const auto cooked_again = tools::texture_cooker::cook(*parsed.source, bytes(ppm_p3));
    assert(cooked_again.success);
    assert(cooked_again.bytes == cooked.bytes);

    const auto decoded = render::decode_texture_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->width == 2U);
    assert(decoded.asset->height == 1U);
    assert(decoded.asset->color_space == render::TextureColorSpace::srgb);
    assert(decoded.asset->rgba8 == p3.image->rgba8);

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_texture_asset(corrupted).asset.has_value());

    constexpr std::string_view traversal = R"(
vice-texture-source 1
id 71000000-0000-4000-8000-000000000001
image "../checker.ppm"
color_space srgb
)";
    assert(!tools::texture_cooker::parse_source_manifest(traversal).source.has_value());

    assert(!tools::texture_cooker::decode_ppm(bytes("P6\n2 2\n255\nabc")).image.has_value());
    assert(!tools::texture_cooker::decode_ppm(bytes("P3\n1 1\n100\n1 2 3\n")).image.has_value());
    assert(!tools::texture_cooker::decode_ppm(bytes("P3\n1 1\n255\n1 2 3 4\n")).image.has_value());

    render::TextureAsset invalid = *decoded.asset;
    invalid.width = 0U;
    assert(!render::encode_texture_asset(invalid));

    return 0;
}

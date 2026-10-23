#include <vice/render/texture_asset.hpp>
#include <vice/tools/texture_cooker.hpp>

#include <algorithm>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::size_t content_fingerprint_offset = 36U;
constexpr std::size_t v2_fingerprint_tail_offset = 44U;
constexpr std::size_t v2_header_size = 61U;
constexpr std::size_t mip_descriptor_size = 32U;

[[nodiscard]] std::vector<std::byte> bytes(const std::string_view text) {
    const auto data = std::as_bytes(std::span{text.data(), text.size()});
    return {data.begin(), data.end()};
}

[[nodiscard]] std::uint64_t fingerprint(const std::span<const std::byte> data) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const auto value : data) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash;
}

void overwrite_u32(
    std::vector<std::byte>& data,
    const std::size_t offset,
    const std::uint32_t value
) {
    assert(offset + sizeof(value) <= data.size());
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        data[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

void overwrite_u64(
    std::vector<std::byte>& data,
    const std::size_t offset,
    const std::uint64_t value
) {
    assert(offset + sizeof(value) <= data.size());
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        data[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

[[nodiscard]] std::vector<std::byte> make_p6(
    const std::uint32_t width,
    const std::uint32_t height
) {
    const std::string header = "P6\n" + std::to_string(width) + " " +
        std::to_string(height) + "\n255\n";
    auto output = bytes(header);
    output.reserve(output.size() + static_cast<std::size_t>(width) * height * 3U);
    for (std::uint32_t y = 0U; y < height; ++y) {
        for (std::uint32_t x = 0U; x < width; ++x) {
            output.push_back(static_cast<std::byte>((x * 17U + y * 3U) & 0xFFU));
            output.push_back(static_cast<std::byte>((x * 5U + y * 11U) & 0xFFU));
            output.push_back(static_cast<std::byte>((x * 7U + y * 13U) & 0xFFU));
        }
    }
    return output;
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
    assert(decoded.asset->version == render::texture_asset_format_version_v2);
    assert(decoded.asset->width == 2U);
    assert(decoded.asset->height == 1U);
    assert(decoded.asset->color_space == render::TextureColorSpace::srgb);
    assert(render::texture_mip_count(*decoded.asset) == 2U);
    assert(decoded.asset->mip_levels.size() == 2U);
    assert(decoded.asset->virtual_tiles.size() == 2U);
    const auto base_mip = render::texture_mip_rgba8(*decoded.asset, 0U);
    assert(base_mip.size() == p3.image->rgba8.size());
    assert(std::equal(base_mip.begin(), base_mip.end(), p3.image->rgba8.begin()));
    const auto final_mip = render::texture_mip_rgba8(*decoded.asset, 1U);
    assert(final_mip.size() == 4U);
    assert(std::to_integer<unsigned int>(final_mip[0]) == 128U);
    assert(std::to_integer<unsigned int>(final_mip[1]) == 64U);
    assert(std::to_integer<unsigned int>(final_mip[2]) == 128U);
    assert(std::to_integer<unsigned int>(final_mip[3]) == 255U);
    assert(decoded.asset->mip_levels[0].payload_offset == 0U);
    assert(decoded.asset->mip_levels[0].payload_size == 8U);
    assert(decoded.asset->mip_levels[1].payload_offset == 8U);
    assert(decoded.asset->mip_levels[1].payload_size == 4U);

    const auto odd_cooked = tools::texture_cooker::cook(*parsed.source, make_p6(257U, 129U));
    const auto odd_repeat = tools::texture_cooker::cook(*parsed.source, make_p6(257U, 129U));
    assert(odd_cooked.success);
    assert(odd_repeat.success);
    assert(odd_repeat.bytes == odd_cooked.bytes);
    const auto odd = render::decode_texture_asset(odd_cooked.bytes);
    assert(odd.asset.has_value());
    assert(render::texture_mip_count(*odd.asset) == 10U);
    assert(odd.asset->mip_levels.front().width == 257U);
    assert(odd.asset->mip_levels.front().height == 129U);
    assert(odd.asset->mip_levels.back().width == 1U);
    assert(odd.asset->mip_levels.back().height == 1U);
    assert(odd.asset->virtual_tiles.size() == 16U);
    assert(odd.asset->mip_levels[0].first_tile == 0U);
    assert(odd.asset->mip_levels[0].tile_count == 6U);
    assert(odd.asset->virtual_tiles[5] == render::VirtualTextureTile{
        .mip_level = 0U,
        .tile_x = 2U,
        .tile_y = 1U,
        .width = 1U,
        .height = 1U,
    });
    assert(odd.asset->mip_levels[1].first_tile == 6U);
    assert(odd.asset->mip_levels[1].tile_count == 2U);
    assert(odd.asset->virtual_tiles[7].width == 1U);
    assert(odd.asset->virtual_tiles[7].height == 65U);
    for (std::size_t index = 1U; index < odd.asset->mip_levels.size(); ++index) {
        const auto& previous = odd.asset->mip_levels[index - 1U];
        const auto& current = odd.asset->mip_levels[index];
        assert(current.payload_offset == previous.payload_offset + previous.payload_size);
        assert(current.first_tile == previous.first_tile + previous.tile_count);
    }
    assert(render::texture_mip_rgba8(*odd.asset, 9U).size() == 4U);

    render::TextureAsset legacy;
    legacy.version = render::texture_asset_format_version_v1;
    legacy.stable_id = parsed.source->stable_id;
    legacy.source_fingerprint = 123U;
    legacy.width = 2U;
    legacy.height = 1U;
    legacy.color_space = render::TextureColorSpace::srgb;
    legacy.rgba8 = p3.image->rgba8;
    const auto legacy_encoded = render::encode_texture_asset(legacy);
    assert(legacy_encoded);
    const auto legacy_decoded = render::decode_texture_asset(legacy_encoded.bytes);
    assert(legacy_decoded.asset.has_value());
    assert(legacy_decoded.asset->version == render::texture_asset_format_version_v1);
    assert(render::texture_mip_count(*legacy_decoded.asset) == 1U);
    assert(legacy_decoded.asset->mip_levels.empty());
    assert(legacy_decoded.asset->virtual_tiles.empty());
    assert(render::texture_mip_rgba8(*legacy_decoded.asset, 0U).size() == 8U);

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_texture_asset(corrupted).asset.has_value());

    auto semantic_corruption = cooked.bytes;
    const std::size_t first_tile_width = v2_header_size +
        decoded.asset->mip_levels.size() * mip_descriptor_size + sizeof(std::uint32_t) * 3U;
    overwrite_u32(semantic_corruption, first_tile_width, 1U);
    overwrite_u64(
        semantic_corruption,
        content_fingerprint_offset,
        fingerprint(std::span<const std::byte>{semantic_corruption}.subspan(v2_fingerprint_tail_offset))
    );
    assert(!render::decode_texture_asset(semantic_corruption).asset.has_value());

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
    invalid = *decoded.asset;
    invalid.virtual_tiles.front().width = 1U;
    assert(!render::encode_texture_asset(invalid));

    return 0;
}

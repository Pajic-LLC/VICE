#include <vice/render/environment_asset.hpp>
#include <vice/tools/environment_cooker.hpp>

#include <cassert>
#include <cstddef>
#include <span>
#include <string>

int main() {
    using namespace vice;

    const std::string manifest =
        "vice-environment 1\n"
        "id 72000000-0000-4000-8000-000000000001\n"
        "image \"textures/environment.ppm\"\n"
        "color_space srgb\n"
        "scope local\n"
        "intensity 1.5\n"
        "rotation_degrees 25\n"
        "irradiance_size 4 2\n"
        "specular_size 8 4\n"
        "specular_mips 4\n"
        "brdf_lut_size 8\n"
        "samples 16\n";
    const auto parsed = tools::environment_cooker::parse_source_manifest(manifest);
    assert(parsed.source.has_value());
    assert(parsed.source->irradiance_width == 4U);
    assert(parsed.source->specular_mip_levels == 4U);
    assert(parsed.source->sample_count == 16U);

    const std::string ppm =
        "P3\n4 2\n255\n"
        "255 80 40  255 180 90  80 140 255  30 50 120\n"
        "20 30 60   60 100 160  180 120 80  255 220 180\n";
    const auto image_bytes = std::as_bytes(std::span<const char>{ppm.data(), ppm.size()});
    const auto cooked = tools::environment_cooker::cook(*parsed.source, image_bytes);
    assert(cooked.success);
    assert(!cooked.bytes.empty());

    const auto decoded = render::decode_environment_asset(cooked.bytes);
    assert(decoded.asset.has_value());
    assert(decoded.asset->irradiance_width == 4U);
    assert(decoded.asset->irradiance_height == 2U);
    assert(decoded.asset->specular_width == 8U);
    assert(decoded.asset->specular_height == 4U);
    assert(decoded.asset->specular_mip_levels == 4U);
    assert(decoded.asset->brdf_lut_size == 8U);
    assert(decoded.asset->scope == render::EnvironmentScope::local_probe);
    assert(decoded.asset->irradiance_rgba16f.size() == 4U * 2U * 8U);
    assert(decoded.asset->specular_rgba16f.size() == (8U * 4U + 4U * 2U + 2U * 1U + 1U) * 8U);
    assert(decoded.asset->brdf_lut_rgba16f.size() == 8U * 8U * 8U);

    auto legacy = cooked.bytes;
    constexpr std::size_t v2_scope_offset = 76U;
    assert(legacy.size() > v2_scope_offset);
    legacy[8] = std::byte{1U};
    legacy[9] = std::byte{0U};
    legacy[10] = std::byte{0U};
    legacy[11] = std::byte{0U};
    legacy.erase(legacy.begin() + static_cast<std::ptrdiff_t>(v2_scope_offset));
    const auto legacy_decoded = render::decode_environment_asset(legacy);
    assert(legacy_decoded.asset.has_value());
    assert(legacy_decoded.asset->version == render::environment_asset_format_version_v1);
    assert(legacy_decoded.asset->scope == render::EnvironmentScope::global);

    const auto cooked_again = tools::environment_cooker::cook(*parsed.source, image_bytes);
    assert(cooked_again.success);
    assert(cooked_again.bytes == cooked.bytes);

    auto corrupted = cooked.bytes;
    corrupted.back() ^= std::byte{0x01U};
    assert(!render::decode_environment_asset(corrupted).asset.has_value());

    const std::string invalid =
        "vice-environment 1\n"
        "id 72000000-0000-4000-8000-000000000002\n"
        "image \"textures/environment.ppm\"\n"
        "color_space linear\n"
        "specular_size 4 2\n"
        "specular_mips 8\n";
    assert(!tools::environment_cooker::parse_source_manifest(invalid).source.has_value());

    return 0;
}

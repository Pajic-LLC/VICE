#include <vice/render/environment_asset.hpp>
#include <vice/tools/environment_cooker.hpp>

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string>
#include <vector>

namespace {

void append_text(std::vector<std::byte>& bytes, const std::string& text) {
    const auto source = std::as_bytes(std::span<const char>{text.data(), text.size()});
    bytes.insert(bytes.end(), source.begin(), source.end());
}

void append_byte(std::vector<std::byte>& bytes, const std::uint8_t value) {
    bytes.push_back(static_cast<std::byte>(value));
}

[[nodiscard]] std::vector<std::byte> constant_rle_hdr() {
    std::vector<std::byte> bytes;
    append_text(
        bytes,
        "#?RADIANCE\n"
        "FORMAT=32-bit_rle_rgbe\n"
        "\n"
        "-Y 2 +X 8\n"
    );

    constexpr std::uint8_t red = 128U;
    constexpr std::uint8_t green = 64U;
    constexpr std::uint8_t blue = 32U;
    constexpr std::uint8_t exponent = 131U;
    for (std::uint32_t y = 0U; y < 2U; ++y) {
        append_byte(bytes, 2U);
        append_byte(bytes, 2U);
        append_byte(bytes, 0U);
        append_byte(bytes, 8U);
        for (const std::uint8_t channel : {red, green, blue, exponent}) {
            append_byte(bytes, 136U);
            append_byte(bytes, channel);
        }
    }
    return bytes;
}

[[nodiscard]] float half_to_float(const std::span<const std::byte> bytes, const std::size_t offset) {
    assert(offset + 1U < bytes.size());
    const std::uint16_t bits = static_cast<std::uint16_t>(
        std::to_integer<std::uint8_t>(bytes[offset]) |
        (static_cast<std::uint16_t>(std::to_integer<std::uint8_t>(bytes[offset + 1U])) << 8U)
    );
    const bool negative = (bits & 0x8000U) != 0U;
    const std::uint32_t exponent = (bits >> 10U) & 0x1FU;
    const std::uint32_t mantissa = bits & 0x03FFU;
    float value = 0.0F;
    if (exponent == 0U) {
        value = mantissa == 0U
            ? 0.0F
            : std::ldexp(static_cast<float>(mantissa) / 1024.0F, -14);
    } else if (exponent == 31U) {
        value = mantissa == 0U ? std::numeric_limits<float>::infinity() : std::numeric_limits<float>::quiet_NaN();
    } else {
        value = std::ldexp(1.0F + static_cast<float>(mantissa) / 1024.0F,
            static_cast<int>(exponent) - 15);
    }
    return negative ? -value : value;
}

} // namespace

int main() {
    using namespace vice;

    const std::string manifest =
        "vice-environment 1\n"
        "id 73000000-0000-4000-8000-000000000001\n"
        "image \"textures/studio.hdr\"\n"
        "color_space linear\n"
        "intensity 1\n"
        "rotation_degrees 0\n"
        "irradiance_size 2 1\n"
        "specular_size 8 4\n"
        "specular_mips 4\n"
        "brdf_lut_size 4\n"
        "samples 8\n";
    const auto parsed = tools::environment_cooker::parse_source_manifest(manifest);
    assert(parsed.source.has_value());

    const auto hdr = constant_rle_hdr();
    const auto cooked = tools::environment_cooker::cook(*parsed.source, hdr);
    assert(cooked.success);
    const auto decoded = render::decode_environment_asset(cooked.bytes);
    assert(decoded.asset.has_value());

    const auto specular = std::span<const std::byte>{decoded.asset->specular_rgba16f};
    const float specular_red = half_to_float(specular, 0U);
    const float specular_green = half_to_float(specular, 2U);
    const float specular_blue = half_to_float(specular, 4U);
    assert(specular_red > 3.9F);
    assert(specular_green > 1.9F);
    assert(specular_blue > 0.9F);

    const auto irradiance = std::span<const std::byte>{decoded.asset->irradiance_rgba16f};
    assert(half_to_float(irradiance, 0U) > 12.0F);

    const auto cooked_again = tools::environment_cooker::cook(*parsed.source, hdr);
    assert(cooked_again.success);
    assert(cooked_again.bytes == cooked.bytes);

    const std::string srgb_hdr_manifest =
        "vice-environment 1\n"
        "id 73000000-0000-4000-8000-000000000002\n"
        "image \"textures/studio.hdr\"\n"
        "color_space srgb\n";
    assert(!tools::environment_cooker::parse_source_manifest(srgb_hdr_manifest).source.has_value());

    auto unsupported_format = hdr;
    const std::string valid_format = "FORMAT=32-bit_rle_rgbe";
    const std::string invalid_format = "FORMAT=32-bit_rle_xyze";
    assert(valid_format.size() == invalid_format.size());
    const auto source_chars = std::span<std::byte>{unsupported_format};
    const std::string all_text{
        reinterpret_cast<const char*>(unsupported_format.data()),
        unsupported_format.size()
    };
    const auto format_offset = all_text.find(valid_format);
    assert(format_offset != std::string::npos);
    for (std::size_t index = 0U; index < invalid_format.size(); ++index) {
        source_chars[format_offset + index] = static_cast<std::byte>(invalid_format[index]);
    }
    assert(!tools::environment_cooker::cook(*parsed.source, unsupported_format).success);

    auto truncated = hdr;
    truncated.pop_back();
    assert(!tools::environment_cooker::cook(*parsed.source, truncated).success);

    return 0;
}

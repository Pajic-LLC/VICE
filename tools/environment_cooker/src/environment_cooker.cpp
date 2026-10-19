#include <vice/tools/environment_cooker.hpp>

#include "radiance_hdr.hpp"

#include <vice/tools/texture_cooker.hpp>
#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace vice::tools::environment_cooker {
namespace {

constexpr float pi = 3.14159265358979323846F;
constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::uint32_t max_irradiance_dimension = 256U;
constexpr std::uint32_t max_specular_dimension = 2'048U;
constexpr std::uint32_t max_brdf_lut_size = 512U;
constexpr std::uint32_t max_sample_count = 1'024U;
constexpr std::uint32_t max_specular_mips = 12U;

struct Vec3 {
    float x = 0.0F;
    float y = 0.0F;
    float z = 0.0F;
};

struct FloatImage {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<Vec3> pixels;
};

[[nodiscard]] Vec3 operator+(const Vec3 left, const Vec3 right) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] Vec3 operator-(const Vec3 left, const Vec3 right) noexcept {
    return {left.x - right.x, left.y - right.y, left.z - right.z};
}

[[nodiscard]] Vec3 operator*(const Vec3 value, const float scalar) noexcept {
    return {value.x * scalar, value.y * scalar, value.z * scalar};
}

Vec3& operator+=(Vec3& left, const Vec3 right) noexcept {
    left = left + right;
    return left;
}

[[nodiscard]] float dot(const Vec3 left, const Vec3 right) noexcept {
    return left.x * right.x + left.y * right.y + left.z * right.z;
}

[[nodiscard]] Vec3 cross(const Vec3 left, const Vec3 right) noexcept {
    return {
        left.y * right.z - left.z * right.y,
        left.z * right.x - left.x * right.z,
        left.x * right.y - left.y * right.x,
    };
}

[[nodiscard]] Vec3 normalize(const Vec3 value) noexcept {
    const float length_squared = dot(value, value);
    if (!std::isfinite(length_squared) || length_squared <= 1.0e-12F) return {0.0F, 1.0F, 0.0F};
    const float inverse = 1.0F / std::sqrt(length_squared);
    return value * inverse;
}

[[nodiscard]] std::uint64_t hash_text(const std::string_view text) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t hash_bytes(
    std::uint64_t hash,
    const std::span<const std::byte> bytes
) noexcept {
    for (const std::byte value : bytes) {
        hash ^= std::to_integer<std::uint8_t>(value);
        hash *= fnv_prime;
    }
    return hash == 0U ? 1U : hash;
}

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool safe_image_path(const std::filesystem::path& path) {
    const auto extension = path.extension();
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        (extension != ".ppm" && extension != ".hdr")) {
        return false;
    }
    for (const auto& part : path) {
        if (part == "." || part == "..") return false;
    }
    return true;
}

[[nodiscard]] constexpr std::uint32_t mip_extent(
    std::uint32_t extent,
    std::uint32_t level
) noexcept {
    while (level-- > 0U && extent > 1U) extent /= 2U;
    return extent == 0U ? 1U : extent;
}

[[nodiscard]] std::uint32_t maximum_mips(
    const std::uint32_t width,
    const std::uint32_t height
) noexcept {
    std::uint32_t size = std::max(width, height);
    std::uint32_t levels = 1U;
    while (size > 1U) {
        size = std::max(1U, size / 2U);
        ++levels;
    }
    return levels;
}

[[nodiscard]] bool valid_source(const EnvironmentSource& source) noexcept {
    const bool hdr_source = source.image_source.extension() == ".hdr";
    return render::valid_environment_id(source.stable_id) && source.manifest_fingerprint != 0U &&
        safe_image_path(source.image_source) &&
        (!hdr_source || source.color_space == render::TextureColorSpace::linear) &&
        std::isfinite(source.intensity) && source.intensity >= 0.0F &&
        std::isfinite(source.rotation_degrees) && source.irradiance_width > 0U &&
        source.irradiance_height > 0U && source.irradiance_width <= max_irradiance_dimension &&
        source.irradiance_height <= max_irradiance_dimension && source.specular_width > 0U &&
        source.specular_height > 0U && source.specular_width <= max_specular_dimension &&
        source.specular_height <= max_specular_dimension && source.specular_mip_levels > 0U &&
        source.specular_mip_levels <= max_specular_mips &&
        source.specular_mip_levels <= maximum_mips(source.specular_width, source.specular_height) &&
        source.brdf_lut_size > 0U && source.brdf_lut_size <= max_brdf_lut_size &&
        source.sample_count > 0U && source.sample_count <= max_sample_count;
}

[[nodiscard]] float srgb_to_linear(const float value) noexcept {
    const float clamped = std::clamp(value, 0.0F, 1.0F);
    return clamped <= 0.04045F
        ? clamped / 12.92F
        : std::pow((clamped + 0.055F) / 1.055F, 2.4F);
}

[[nodiscard]] FloatImage make_float_image(
    const texture_cooker::ImageData& image,
    const render::TextureColorSpace color_space
) {
    FloatImage result;
    result.width = image.width;
    result.height = image.height;
    result.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t index = 0U; index < result.pixels.size(); ++index) {
        const auto component = [&](const std::size_t channel) {
            const float value = static_cast<float>(
                std::to_integer<std::uint8_t>(image.rgba8[index * 4U + channel])
            ) / 255.0F;
            return color_space == render::TextureColorSpace::srgb ? srgb_to_linear(value) : value;
        };
        result.pixels[index] = {component(0U), component(1U), component(2U)};
    }
    return result;
}

[[nodiscard]] FloatImage make_float_image(const detail::RadianceHdrImage& image) {
    FloatImage result;
    result.width = image.width;
    result.height = image.height;
    result.pixels.resize(static_cast<std::size_t>(image.width) * image.height);
    for (std::size_t index = 0U; index < result.pixels.size(); ++index) {
        result.pixels[index] = {
            image.rgb[index * 3U + 0U],
            image.rgb[index * 3U + 1U],
            image.rgb[index * 3U + 2U],
        };
    }
    return result;
}

[[nodiscard]] Vec3 direction_from_uv(const float u, const float v) noexcept {
    const float phi = (u * 2.0F - 1.0F) * pi;
    const float theta = std::clamp(v, 0.0F, 1.0F) * pi;
    const float sin_theta = std::sin(theta);
    return normalize({
        sin_theta * std::cos(phi),
        std::cos(theta),
        sin_theta * std::sin(phi),
    });
}

[[nodiscard]] Vec3 sample_environment(const FloatImage& image, const Vec3 direction) noexcept {
    const Vec3 n = normalize(direction);
    float u = std::atan2(n.z, n.x) / (2.0F * pi) + 0.5F;
    u -= std::floor(u);
    const float v = std::acos(std::clamp(n.y, -1.0F, 1.0F)) / pi;

    const float x = u * static_cast<float>(image.width) - 0.5F;
    const float y = v * static_cast<float>(image.height) - 0.5F;
    const int x0_raw = static_cast<int>(std::floor(x));
    const int y0_raw = static_cast<int>(std::floor(y));
    const float tx = x - std::floor(x);
    const float ty = y - std::floor(y);

    const auto wrap_x = [&](const int value) {
        const int width = static_cast<int>(image.width);
        const int wrapped = value % width;
        return wrapped < 0 ? wrapped + width : wrapped;
    };
    const auto clamp_y = [&](const int value) {
        return std::clamp(value, 0, static_cast<int>(image.height) - 1);
    };
    const auto pixel = [&](const int px, const int py) -> Vec3 {
        return image.pixels[
            static_cast<std::size_t>(clamp_y(py)) * image.width +
            static_cast<std::size_t>(wrap_x(px))
        ];
    };

    const Vec3 top = pixel(x0_raw, y0_raw) * (1.0F - tx) + pixel(x0_raw + 1, y0_raw) * tx;
    const Vec3 bottom = pixel(x0_raw, y0_raw + 1) * (1.0F - tx) +
        pixel(x0_raw + 1, y0_raw + 1) * tx;
    return top * (1.0F - ty) + bottom * ty;
}

[[nodiscard]] float radical_inverse_vdc(std::uint32_t bits) noexcept {
    bits = (bits << 16U) | (bits >> 16U);
    bits = ((bits & 0x55555555U) << 1U) | ((bits & 0xAAAAAAAAU) >> 1U);
    bits = ((bits & 0x33333333U) << 2U) | ((bits & 0xCCCCCCCCU) >> 2U);
    bits = ((bits & 0x0F0F0F0FU) << 4U) | ((bits & 0xF0F0F0F0U) >> 4U);
    bits = ((bits & 0x00FF00FFU) << 8U) | ((bits & 0xFF00FF00U) >> 8U);
    return static_cast<float>(bits) * 2.3283064365386963e-10F;
}

[[nodiscard]] std::array<float, 2U> hammersley(
    const std::uint32_t index,
    const std::uint32_t count
) noexcept {
    return {
        static_cast<float>(index) / static_cast<float>(count),
        radical_inverse_vdc(index),
    };
}

struct Basis {
    Vec3 tangent{};
    Vec3 normal{};
    Vec3 bitangent{};
};

[[nodiscard]] Basis basis_from_normal(const Vec3 normal) noexcept {
    const Vec3 n = normalize(normal);
    const Vec3 reference = std::abs(n.y) < 0.999F ? Vec3{0.0F, 1.0F, 0.0F} : Vec3{1.0F, 0.0F, 0.0F};
    const Vec3 tangent = normalize(cross(reference, n));
    return {tangent, n, cross(n, tangent)};
}

[[nodiscard]] Vec3 from_basis(const Basis& basis, const Vec3 local) noexcept {
    return normalize(
        basis.tangent * local.x + basis.normal * local.y + basis.bitangent * local.z
    );
}

[[nodiscard]] Vec3 cosine_hemisphere(
    const std::array<float, 2U> xi,
    const Vec3 normal
) noexcept {
    const float radius = std::sqrt(xi[0]);
    const float phi = 2.0F * pi * xi[1];
    const Vec3 local{
        radius * std::cos(phi),
        std::sqrt(std::max(0.0F, 1.0F - xi[0])),
        radius * std::sin(phi),
    };
    return from_basis(basis_from_normal(normal), local);
}

[[nodiscard]] Vec3 importance_sample_ggx(
    const std::array<float, 2U> xi,
    const float roughness,
    const Vec3 normal
) noexcept {
    const float alpha = std::max(0.001F, roughness * roughness);
    const float alpha_squared = alpha * alpha;
    const float phi = 2.0F * pi * xi[0];
    const float cosine = std::sqrt(
        std::max(0.0F, (1.0F - xi[1]) / (1.0F + (alpha_squared - 1.0F) * xi[1]))
    );
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    return from_basis(
        basis_from_normal(normal),
        {sine * std::cos(phi), cosine, sine * std::sin(phi)}
    );
}

[[nodiscard]] float geometry_schlick_ggx(const float ndot, const float roughness) noexcept {
    const float k = roughness * roughness * 0.5F;
    return ndot / std::max(ndot * (1.0F - k) + k, 1.0e-5F);
}

[[nodiscard]] float geometry_smith(
    const float ndotv,
    const float ndotl,
    const float roughness
) noexcept {
    return geometry_schlick_ggx(ndotv, roughness) * geometry_schlick_ggx(ndotl, roughness);
}

[[nodiscard]] std::uint16_t float_to_half(const float input) noexcept {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(input);
    const std::uint32_t sign = (bits >> 16U) & 0x8000U;
    const std::uint32_t exponent_bits = (bits >> 23U) & 0xFFU;
    const std::uint32_t mantissa = bits & 0x7FFFFFU;

    if (exponent_bits == 0xFFU) {
        return static_cast<std::uint16_t>(sign | (mantissa == 0U ? 0x7C00U : 0x7E00U));
    }

    int exponent = static_cast<int>(exponent_bits) - 127 + 15;
    if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00U);
    if (exponent <= 0) {
        if (exponent < -10) return static_cast<std::uint16_t>(sign);
        std::uint32_t value = mantissa | 0x800000U;
        const std::uint32_t shift = static_cast<std::uint32_t>(14 - exponent);
        const std::uint32_t rounding = (1U << (shift - 1U));
        value = (value + rounding - 1U + ((value >> shift) & 1U)) >> shift;
        return static_cast<std::uint16_t>(sign | value);
    }

    std::uint32_t rounded = mantissa + 0xFFFU + ((mantissa >> 13U) & 1U);
    if ((rounded & 0x800000U) != 0U) {
        rounded = 0U;
        ++exponent;
        if (exponent >= 31) return static_cast<std::uint16_t>(sign | 0x7C00U);
    }
    return static_cast<std::uint16_t>(
        sign | (static_cast<std::uint32_t>(exponent) << 10U) | (rounded >> 13U)
    );
}

void append_half(std::vector<std::byte>& bytes, const float value) {
    const std::uint16_t half = float_to_half(value);
    bytes.push_back(static_cast<std::byte>(half & 0xFFU));
    bytes.push_back(static_cast<std::byte>((half >> 8U) & 0xFFU));
}

void append_rgba16f(
    std::vector<std::byte>& bytes,
    const float red,
    const float green,
    const float blue,
    const float alpha
) {
    append_half(bytes, red);
    append_half(bytes, green);
    append_half(bytes, blue);
    append_half(bytes, alpha);
}

[[nodiscard]] std::vector<std::byte> build_irradiance(
    const FloatImage& source,
    const EnvironmentSource& settings
) {
    std::vector<std::byte> result;
    result.reserve(static_cast<std::size_t>(settings.irradiance_width) * settings.irradiance_height * 8U);
    for (std::uint32_t y = 0U; y < settings.irradiance_height; ++y) {
        for (std::uint32_t x = 0U; x < settings.irradiance_width; ++x) {
            const Vec3 normal = direction_from_uv(
                (static_cast<float>(x) + 0.5F) / static_cast<float>(settings.irradiance_width),
                (static_cast<float>(y) + 0.5F) / static_cast<float>(settings.irradiance_height)
            );
            Vec3 accumulated{};
            for (std::uint32_t sample = 0U; sample < settings.sample_count; ++sample) {
                const Vec3 direction = cosine_hemisphere(hammersley(sample, settings.sample_count), normal);
                accumulated += sample_environment(source, direction);
            }
            const Vec3 irradiance = accumulated * (pi / static_cast<float>(settings.sample_count));
            append_rgba16f(result, irradiance.x, irradiance.y, irradiance.z, 1.0F);
        }
    }
    return result;
}

[[nodiscard]] std::vector<std::byte> build_specular(
    const FloatImage& source,
    const EnvironmentSource& settings
) {
    std::vector<std::byte> result;
    for (std::uint32_t mip = 0U; mip < settings.specular_mip_levels; ++mip) {
        const std::uint32_t width = mip_extent(settings.specular_width, mip);
        const std::uint32_t height = mip_extent(settings.specular_height, mip);
        const float roughness = settings.specular_mip_levels > 1U
            ? static_cast<float>(mip) / static_cast<float>(settings.specular_mip_levels - 1U)
            : 0.0F;
        for (std::uint32_t y = 0U; y < height; ++y) {
            for (std::uint32_t x = 0U; x < width; ++x) {
                const Vec3 normal = direction_from_uv(
                    (static_cast<float>(x) + 0.5F) / static_cast<float>(width),
                    (static_cast<float>(y) + 0.5F) / static_cast<float>(height)
                );
                if (roughness <= 0.0001F) {
                    const Vec3 radiance = sample_environment(source, normal);
                    append_rgba16f(result, radiance.x, radiance.y, radiance.z, 1.0F);
                    continue;
                }

                const Vec3 view = normal;
                Vec3 accumulated{};
                float weight = 0.0F;
                for (std::uint32_t sample = 0U; sample < settings.sample_count; ++sample) {
                    const Vec3 half_vector = importance_sample_ggx(
                        hammersley(sample, settings.sample_count),
                        roughness,
                        normal
                    );
                    const Vec3 light = normalize(half_vector * (2.0F * dot(view, half_vector)) - view);
                    const float ndotl = std::max(dot(normal, light), 0.0F);
                    if (ndotl <= 0.0F) continue;
                    accumulated += sample_environment(source, light) * ndotl;
                    weight += ndotl;
                }
                const Vec3 radiance = weight > 0.0F ? accumulated * (1.0F / weight) : Vec3{};
                append_rgba16f(result, radiance.x, radiance.y, radiance.z, 1.0F);
            }
        }
    }
    return result;
}

[[nodiscard]] std::array<float, 2U> integrate_brdf(
    const float ndotv,
    const float roughness,
    const std::uint32_t sample_count
) noexcept {
    const Vec3 normal{0.0F, 0.0F, 1.0F};
    const Vec3 view{
        std::sqrt(std::max(0.0F, 1.0F - ndotv * ndotv)),
        0.0F,
        ndotv,
    };
    float a = 0.0F;
    float b = 0.0F;
    for (std::uint32_t sample = 0U; sample < sample_count; ++sample) {
        const Vec3 half_vector = importance_sample_ggx(
            hammersley(sample, sample_count),
            roughness,
            normal
        );
        const Vec3 light = normalize(half_vector * (2.0F * dot(view, half_vector)) - view);
        const float ndotl = std::max(light.z, 0.0F);
        const float ndoth = std::max(half_vector.z, 0.0F);
        const float vdoth = std::max(dot(view, half_vector), 0.0F);
        if (ndotl <= 0.0F || ndoth <= 0.0F || ndotv <= 0.0F) continue;
        const float geometry = geometry_smith(ndotv, ndotl, roughness);
        const float visibility = geometry * vdoth / std::max(ndoth * ndotv, 1.0e-5F);
        const float fresnel = std::pow(1.0F - vdoth, 5.0F);
        a += (1.0F - fresnel) * visibility;
        b += fresnel * visibility;
    }
    const float inverse = 1.0F / static_cast<float>(sample_count);
    return {a * inverse, b * inverse};
}

[[nodiscard]] std::vector<std::byte> build_brdf_lut(const EnvironmentSource& settings) {
    std::vector<std::byte> result;
    result.reserve(static_cast<std::size_t>(settings.brdf_lut_size) * settings.brdf_lut_size * 8U);
    for (std::uint32_t y = 0U; y < settings.brdf_lut_size; ++y) {
        const float roughness = (static_cast<float>(y) + 0.5F) /
            static_cast<float>(settings.brdf_lut_size);
        for (std::uint32_t x = 0U; x < settings.brdf_lut_size; ++x) {
            const float ndotv = (static_cast<float>(x) + 0.5F) /
                static_cast<float>(settings.brdf_lut_size);
            const auto integrated = integrate_brdf(ndotv, roughness, settings.sample_count);
            append_rgba16f(result, integrated[0], integrated[1], 0.0F, 1.0F);
        }
    }
    return result;
}

} // namespace

ParseResult parse_source_manifest(const std::string_view source_text) {
    ParseResult result;
    if (source_text.empty()) {
        result.diagnostics = "Environment source is empty.";
        return result;
    }

    EnvironmentSource parsed;
    bool header_seen = false;
    bool id_seen = false;
    bool image_seen = false;
    bool color_space_seen = false;
    bool intensity_seen = false;
    bool rotation_seen = false;
    bool irradiance_seen = false;
    bool specular_seen = false;
    bool specular_mips_seen = false;
    bool brdf_seen = false;
    bool samples_seen = false;
    std::istringstream source{std::string{source_text}};
    std::string line;
    std::size_t line_number = 0U;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Environment source line " + std::to_string(line_number) + ": " + message;
    };

    while (std::getline(source, line)) {
        ++line_number;
        if (const auto comment = line.find('#'); comment != std::string::npos) line.erase(comment);
        line = trim(std::move(line));
        if (line.empty()) continue;

        std::istringstream values{line};
        std::string keyword;
        values >> keyword;
        if (!header_seen) {
            std::uint32_t version = 0U;
            if (keyword != "vice-environment" || !(values >> version) ||
                version != environment_source_format_version) {
                fail("expected `vice-environment 1`.");
                return result;
            }
            std::string trailing;
            if (values >> trailing) {
                fail("source header has trailing data.");
                return result;
            }
            header_seen = true;
            continue;
        }

        if (keyword == "id") {
            std::string id_text;
            if (id_seen || !(values >> id_text)) {
                fail(id_seen ? "environment ID is duplicated." : "environment ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("environment ID is invalid.");
                return result;
            }
            std::copy(id->bytes().begin(), id->bytes().end(), parsed.stable_id.begin());
            id_seen = true;
        } else if (keyword == "image") {
            std::string image;
            if (image_seen || !(values >> std::quoted(image))) {
                fail(image_seen ? "image path is duplicated." : "image path is invalid.");
                return result;
            }
            parsed.image_source = std::filesystem::path{image}.lexically_normal();
            if (!safe_image_path(parsed.image_source)) {
                fail("image must be a project-relative .ppm or .hdr path without traversal.");
                return result;
            }
            image_seen = true;
        } else if (keyword == "color_space") {
            std::string value;
            if (color_space_seen || !(values >> value)) {
                fail(color_space_seen ? "color space is duplicated." : "color space is missing.");
                return result;
            }
            if (value == "srgb") parsed.color_space = render::TextureColorSpace::srgb;
            else if (value == "linear") parsed.color_space = render::TextureColorSpace::linear;
            else {
                fail("color space must be `srgb` or `linear`.");
                return result;
            }
            color_space_seen = true;
        } else if (keyword == "intensity") {
            if (intensity_seen || !(values >> parsed.intensity)) {
                fail(intensity_seen ? "intensity is duplicated." : "intensity is invalid.");
                return result;
            }
            intensity_seen = true;
        } else if (keyword == "rotation_degrees") {
            if (rotation_seen || !(values >> parsed.rotation_degrees)) {
                fail(rotation_seen ? "rotation is duplicated." : "rotation is invalid.");
                return result;
            }
            rotation_seen = true;
        } else if (keyword == "irradiance_size") {
            if (irradiance_seen || !(values >> parsed.irradiance_width >> parsed.irradiance_height)) {
                fail(irradiance_seen ? "irradiance size is duplicated." : "irradiance size is invalid.");
                return result;
            }
            irradiance_seen = true;
        } else if (keyword == "specular_size") {
            if (specular_seen || !(values >> parsed.specular_width >> parsed.specular_height)) {
                fail(specular_seen ? "specular size is duplicated." : "specular size is invalid.");
                return result;
            }
            specular_seen = true;
        } else if (keyword == "specular_mips") {
            if (specular_mips_seen || !(values >> parsed.specular_mip_levels)) {
                fail(specular_mips_seen ? "specular mip count is duplicated." : "specular mip count is invalid.");
                return result;
            }
            specular_mips_seen = true;
        } else if (keyword == "brdf_lut_size") {
            if (brdf_seen || !(values >> parsed.brdf_lut_size)) {
                fail(brdf_seen ? "BRDF LUT size is duplicated." : "BRDF LUT size is invalid.");
                return result;
            }
            brdf_seen = true;
        } else if (keyword == "samples") {
            if (samples_seen || !(values >> parsed.sample_count)) {
                fail(samples_seen ? "sample count is duplicated." : "sample count is invalid.");
                return result;
            }
            samples_seen = true;
        } else {
            fail("unknown keyword `" + keyword + "`.");
            return result;
        }

        std::string trailing;
        if (values >> trailing) {
            fail("statement has trailing data.");
            return result;
        }
    }

    if (!header_seen || !id_seen || !image_seen || !color_space_seen) {
        result.diagnostics = "Environment source is missing one or more required fields.";
        return result;
    }
    if (parsed.image_source.extension() == ".hdr" &&
        parsed.color_space != render::TextureColorSpace::linear) {
        result.diagnostics = "Radiance .hdr environment sources must use color_space linear.";
        return result;
    }
    parsed.manifest_fingerprint = hash_text(source_text);
    if (parsed.manifest_fingerprint == 0U) parsed.manifest_fingerprint = 1U;
    if (!valid_source(parsed)) {
        result.diagnostics = "Environment source settings are outside supported limits.";
        return result;
    }
    result.source = std::move(parsed);
    return result;
}

CookResult cook(
    const EnvironmentSource& source,
    const std::span<const std::byte> image_bytes
) {
    CookResult result;
    if (!valid_source(source) || image_bytes.empty()) {
        result.diagnostics = "Environment source is invalid.";
        return result;
    }
    FloatImage source_image;
    if (source.image_source.extension() == ".hdr") {
        const auto decoded = detail::decode_radiance_hdr(image_bytes);
        if (!decoded.image.has_value()) {
            result.diagnostics = decoded.diagnostics;
            return result;
        }
        source_image = make_float_image(*decoded.image);
    } else {
        const auto decoded = texture_cooker::decode_ppm(image_bytes);
        if (!decoded.image.has_value()) {
            result.diagnostics = decoded.diagnostics;
            return result;
        }
        source_image = make_float_image(*decoded.image, source.color_space);
    }
    render::EnvironmentAsset asset;
    asset.stable_id = source.stable_id;
    asset.source_fingerprint = hash_bytes(source.manifest_fingerprint, image_bytes);
    asset.intensity = source.intensity;
    asset.rotation_radians = source.rotation_degrees * pi / 180.0F;
    asset.irradiance_width = source.irradiance_width;
    asset.irradiance_height = source.irradiance_height;
    asset.specular_width = source.specular_width;
    asset.specular_height = source.specular_height;
    asset.specular_mip_levels = source.specular_mip_levels;
    asset.brdf_lut_size = source.brdf_lut_size;
    asset.irradiance_rgba16f = build_irradiance(source_image, source);
    asset.specular_rgba16f = build_specular(source_image, source);
    asset.brdf_lut_rgba16f = build_brdf_lut(source);

    const auto encoded = render::encode_environment_asset(asset);
    if (!encoded) {
        result.diagnostics = encoded.diagnostics;
        return result;
    }
    result.success = true;
    result.source_fingerprint = asset.source_fingerprint;
    result.bytes = encoded.bytes;
    return result;
}

bool write_asset(
    const std::filesystem::path& path,
    const std::span<const std::byte> bytes,
    std::string* const diagnostics
) {
    const auto fail = [&](std::string message) {
        if (diagnostics != nullptr) *diagnostics = std::move(message);
        return false;
    };
    if (path.empty() || bytes.empty()) return fail("Environment output path or payload is empty.");

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return fail("Could not create environment output directory: " + error.message());
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) return fail("Could not open temporary environment asset.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            std::filesystem::remove(temporary, error);
            return fail("Could not write temporary environment asset.");
        }
    }
    error.clear();
    if (std::filesystem::exists(path, error)) {
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not inspect existing environment asset: " + error.message());
        }
        std::filesystem::remove(path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not replace environment asset: " + error.message());
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return fail("Could not finalize environment asset: " + error.message());
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::tools::environment_cooker

#include <vice/tools/texture_cooker.hpp>

#include <vice/world/persistent_id.hpp>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>

namespace vice::tools::texture_cooker {
namespace {

constexpr std::uint64_t fnv_offset_basis = 14'695'981'039'346'656'037ULL;
constexpr std::uint64_t fnv_prime = 1'099'511'628'211ULL;
constexpr std::uint32_t max_dimension = 16'384U;

[[nodiscard]] std::uint64_t hash_text(const std::string_view text) noexcept {
    std::uint64_t hash = fnv_offset_basis;
    for (const unsigned char value : text) {
        hash ^= value;
        hash *= fnv_prime;
    }
    return hash;
}

[[nodiscard]] std::uint64_t hash_image(
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
    if (path.empty() || path.is_absolute() || path.has_root_name() || path.has_root_directory() ||
        path.extension() != ".ppm") {
        return false;
    }
    for (const auto& part : path) {
        if (part == "." || part == "..") return false;
    }
    return true;
}

class PpmTokenizer final {
public:
    explicit PpmTokenizer(const std::span<const std::byte> bytes) : bytes_{bytes} {}

    [[nodiscard]] std::optional<std::string_view> token() {
        skip_space_and_comments();
        if (position_ >= bytes_.size()) return std::nullopt;
        const std::size_t start = position_;
        while (position_ < bytes_.size()) {
            const unsigned char value = std::to_integer<unsigned char>(bytes_[position_]);
            if (std::isspace(value) != 0 || value == '#') break;
            ++position_;
        }
        if (start == position_) return std::nullopt;
        return std::string_view{
            reinterpret_cast<const char*>(bytes_.data() + start),
            position_ - start,
        };
    }

    [[nodiscard]] bool consume_binary_separator() {
        if (position_ >= bytes_.size()) return false;
        const unsigned char first = std::to_integer<unsigned char>(bytes_[position_]);
        if (std::isspace(first) == 0) return false;
        ++position_;
        if (first == '\r' && position_ < bytes_.size() &&
            std::to_integer<unsigned char>(bytes_[position_]) == '\n') {
            ++position_;
        }
        return true;
    }

    [[nodiscard]] std::span<const std::byte> remainder() const noexcept {
        return bytes_.subspan(position_);
    }

private:
    void skip_space_and_comments() {
        for (;;) {
            while (position_ < bytes_.size() &&
                std::isspace(std::to_integer<unsigned char>(bytes_[position_])) != 0) {
                ++position_;
            }
            if (position_ >= bytes_.size() ||
                std::to_integer<unsigned char>(bytes_[position_]) != '#') {
                return;
            }
            while (position_ < bytes_.size()) {
                const unsigned char value = std::to_integer<unsigned char>(bytes_[position_++]);
                if (value == '\n') break;
            }
        }
    }

    std::span<const std::byte> bytes_;
    std::size_t position_ = 0U;
};

[[nodiscard]] bool parse_u32(const std::string_view token, std::uint32_t& value) noexcept {
    if (token.empty()) return false;
    const auto [end, error] = std::from_chars(token.data(), token.data() + token.size(), value);
    return error == std::errc{} && end == token.data() + token.size();
}

[[nodiscard]] bool valid_dimensions(
    const std::uint32_t width,
    const std::uint32_t height,
    std::size_t& rgb_bytes
) noexcept {
    if (width == 0U || height == 0U || width > max_dimension || height > max_dimension) {
        return false;
    }
    const std::uint64_t pixels = static_cast<std::uint64_t>(width) * height;
    if (pixels > std::numeric_limits<std::size_t>::max() / 3U) return false;
    rgb_bytes = static_cast<std::size_t>(pixels * 3U);
    return true;
}

} // namespace

ParseResult parse_source_manifest(const std::string_view source_text) {
    ParseResult result;
    if (source_text.empty()) {
        result.diagnostics = "Texture source is empty.";
        return result;
    }

    TextureSource parsed;
    bool header_seen = false;
    bool id_seen = false;
    bool image_seen = false;
    bool color_space_seen = false;
    std::istringstream source{std::string{source_text}};
    std::string line;
    std::size_t line_number = 0U;

    const auto fail = [&](const std::string& message) {
        result.diagnostics = "Texture source line " + std::to_string(line_number) + ": " + message;
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
            if (keyword != "vice-texture-source" || !(values >> version) ||
                version != texture_source_format_version) {
                fail("expected `vice-texture-source 1`.");
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
                fail(id_seen ? "texture ID is duplicated." : "texture ID is missing.");
                return result;
            }
            const auto id = world::PersistentId::parse(id_text);
            if (!id.has_value() || id->is_nil()) {
                fail("texture ID is invalid.");
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
                fail("image must be a project-relative .ppm path without traversal.");
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
        result.diagnostics = "Texture source is missing one or more required fields.";
        return result;
    }
    parsed.manifest_fingerprint = hash_text(source_text);
    if (parsed.manifest_fingerprint == 0U) parsed.manifest_fingerprint = 1U;
    result.source = std::move(parsed);
    return result;
}

ImageDecodeResult decode_ppm(const std::span<const std::byte> bytes) {
    ImageDecodeResult result;
    PpmTokenizer tokenizer{bytes};
    const auto magic = tokenizer.token();
    const auto width_token = tokenizer.token();
    const auto height_token = tokenizer.token();
    const auto maximum_token = tokenizer.token();
    if (!magic.has_value() || !width_token.has_value() || !height_token.has_value() ||
        !maximum_token.has_value()) {
        result.diagnostics = "PPM header is incomplete.";
        return result;
    }

    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::uint32_t maximum = 0U;
    std::size_t rgb_bytes = 0U;
    if ((*magic != "P3" && *magic != "P6") || !parse_u32(*width_token, width) ||
        !parse_u32(*height_token, height) || !parse_u32(*maximum_token, maximum) ||
        maximum != 255U || !valid_dimensions(width, height, rgb_bytes)) {
        result.diagnostics = "PPM must be P3 or P6, use max value 255, and have valid dimensions.";
        return result;
    }

    ImageData image;
    image.width = width;
    image.height = height;
    image.rgba8.resize(static_cast<std::size_t>(width) * height * 4U);

    if (*magic == "P6") {
        if (!tokenizer.consume_binary_separator()) {
            result.diagnostics = "P6 PPM is missing the binary payload separator.";
            return result;
        }
        const auto payload = tokenizer.remainder();
        if (payload.size() != rgb_bytes) {
            result.diagnostics = "P6 PPM pixel payload size does not match its dimensions.";
            return result;
        }
        for (std::size_t pixel = 0U; pixel < static_cast<std::size_t>(width) * height; ++pixel) {
            image.rgba8[pixel * 4U + 0U] = payload[pixel * 3U + 0U];
            image.rgba8[pixel * 4U + 1U] = payload[pixel * 3U + 1U];
            image.rgba8[pixel * 4U + 2U] = payload[pixel * 3U + 2U];
            image.rgba8[pixel * 4U + 3U] = std::byte{255U};
        }
    } else {
        const std::size_t component_count = static_cast<std::size_t>(width) * height * 3U;
        for (std::size_t component = 0U; component < component_count; ++component) {
            const auto token = tokenizer.token();
            std::uint32_t value = 0U;
            if (!token.has_value() || !parse_u32(*token, value) || value > 255U) {
                result.diagnostics = "P3 PPM contains invalid or incomplete pixel data.";
                return result;
            }
            const std::size_t pixel = component / 3U;
            const std::size_t channel = component % 3U;
            image.rgba8[pixel * 4U + channel] = static_cast<std::byte>(value);
            if (channel == 2U) image.rgba8[pixel * 4U + 3U] = std::byte{255U};
        }
        if (tokenizer.token().has_value()) {
            result.diagnostics = "P3 PPM contains trailing pixel data.";
            return result;
        }
    }

    result.image = std::move(image);
    return result;
}

CookResult cook(
    const TextureSource& source,
    const std::span<const std::byte> image_bytes
) {
    CookResult result;
    if (!render::valid_texture_id(source.stable_id) || source.manifest_fingerprint == 0U ||
        !safe_image_path(source.image_source) || image_bytes.empty()) {
        result.diagnostics = "Texture source is invalid.";
        return result;
    }
    const auto decoded = decode_ppm(image_bytes);
    if (!decoded.image.has_value()) {
        result.diagnostics = decoded.diagnostics;
        return result;
    }

    render::TextureAsset asset;
    asset.stable_id = source.stable_id;
    asset.source_fingerprint = hash_image(source.manifest_fingerprint, image_bytes);
    asset.width = decoded.image->width;
    asset.height = decoded.image->height;
    asset.color_space = source.color_space;
    asset.rgba8 = decoded.image->rgba8;
    const auto encoded = render::encode_texture_asset(asset);
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
    if (path.empty() || bytes.empty()) return fail("Texture output path or payload is empty.");

    std::error_code error;
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path(), error);
        if (error) return fail("Could not create texture output directory: " + error.message());
    }
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output{temporary, std::ios::binary | std::ios::trunc};
        if (!output) return fail("Could not open temporary texture asset.");
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
        if (!output) {
            std::filesystem::remove(temporary, error);
            return fail("Could not write temporary texture asset.");
        }
    }
    error.clear();
    if (std::filesystem::exists(path, error)) {
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not inspect existing texture asset: " + error.message());
        }
        std::filesystem::remove(path, error);
        if (error) {
            std::filesystem::remove(temporary, error);
            return fail("Could not replace texture asset: " + error.message());
        }
    }
    std::filesystem::rename(temporary, path, error);
    if (error) {
        std::filesystem::remove(temporary, error);
        return fail("Could not finalize texture asset: " + error.message());
    }
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::tools::texture_cooker

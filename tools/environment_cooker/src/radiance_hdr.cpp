#include "radiance_hdr.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace vice::tools::environment_cooker::detail {
namespace {

constexpr std::uint32_t max_hdr_dimension = 16'384U;
constexpr std::uint64_t max_hdr_pixels = 67'108'864ULL;

[[nodiscard]] std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

[[nodiscard]] bool read_line(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    std::string& line
) {
    if (cursor >= bytes.size()) return false;
    const std::size_t start = cursor;
    while (cursor < bytes.size() && bytes[cursor] != static_cast<std::byte>('\n')) ++cursor;
    const std::size_t end = cursor;
    if (cursor < bytes.size()) ++cursor;

    line.clear();
    line.reserve(end - start);
    for (std::size_t index = start; index < end; ++index) {
        const char value = static_cast<char>(std::to_integer<unsigned char>(bytes[index]));
        if (value == '\0') return false;
        line.push_back(value);
    }
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return true;
}

[[nodiscard]] bool read_byte(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    std::uint8_t& value
) noexcept {
    if (cursor >= bytes.size()) return false;
    value = std::to_integer<std::uint8_t>(bytes[cursor++]);
    return true;
}

[[nodiscard]] bool read_rgbe(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    std::array<std::uint8_t, 4U>& pixel
) noexcept {
    for (auto& component : pixel) {
        if (!read_byte(bytes, cursor, component)) return false;
    }
    return true;
}

struct Orientation {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool file_rows_top_to_bottom = true;
    bool file_columns_left_to_right = true;
};

[[nodiscard]] bool parse_resolution(
    const std::string_view line,
    Orientation& orientation
) {
    std::istringstream values{std::string{line}};
    std::string y_axis;
    std::string x_axis;
    std::uint32_t height = 0U;
    std::uint32_t width = 0U;
    std::string trailing;
    if (!(values >> y_axis >> height >> x_axis >> width) || (values >> trailing)) return false;
    if ((y_axis != "-Y" && y_axis != "+Y") || (x_axis != "+X" && x_axis != "-X")) {
        return false;
    }
    if (width == 0U || height == 0U || width > max_hdr_dimension || height > max_hdr_dimension ||
        static_cast<std::uint64_t>(width) * height > max_hdr_pixels) {
        return false;
    }
    orientation.width = width;
    orientation.height = height;
    orientation.file_rows_top_to_bottom = y_axis == "-Y";
    orientation.file_columns_left_to_right = x_axis == "+X";
    return true;
}

[[nodiscard]] bool decode_new_scanline(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    const std::uint32_t width,
    std::vector<std::array<std::uint8_t, 4U>>& scanline,
    std::string& diagnostics
) {
    std::array<std::vector<std::uint8_t>, 4U> channels;
    for (auto& channel : channels) channel.resize(width);

    for (std::size_t channel_index = 0U; channel_index < channels.size(); ++channel_index) {
        std::uint32_t x = 0U;
        while (x < width) {
            std::uint8_t code = 0U;
            if (!read_byte(bytes, cursor, code) || code == 0U) {
                diagnostics = "Radiance HDR scanline RLE is truncated or invalid.";
                return false;
            }
            if (code > 128U) {
                const std::uint32_t run = static_cast<std::uint32_t>(code - 128U);
                std::uint8_t value = 0U;
                if (run == 0U || run > width - x || !read_byte(bytes, cursor, value)) {
                    diagnostics = "Radiance HDR scanline RLE run exceeds its scanline.";
                    return false;
                }
                std::fill_n(channels[channel_index].begin() + x, run, value);
                x += run;
            } else {
                const std::uint32_t literal = code;
                if (literal > width - x) {
                    diagnostics = "Radiance HDR scanline RLE literal exceeds its scanline.";
                    return false;
                }
                for (std::uint32_t index = 0U; index < literal; ++index) {
                    if (!read_byte(bytes, cursor, channels[channel_index][x++])) {
                        diagnostics = "Radiance HDR scanline RLE literal is truncated.";
                        return false;
                    }
                }
            }
        }
    }

    scanline.resize(width);
    for (std::uint32_t x = 0U; x < width; ++x) {
        scanline[x] = {
            channels[0][x],
            channels[1][x],
            channels[2][x],
            channels[3][x],
        };
    }
    return true;
}

[[nodiscard]] bool decode_old_scanline(
    const std::span<const std::byte> bytes,
    std::size_t& cursor,
    const std::uint32_t width,
    const std::array<std::uint8_t, 4U> first,
    std::vector<std::array<std::uint8_t, 4U>>& scanline,
    std::string& diagnostics
) {
    scanline.clear();
    scanline.reserve(width);
    std::array<std::uint8_t, 4U> pixel = first;
    std::uint32_t repeat_shift = 0U;

    while (scanline.size() < width) {
        if (pixel[0] == 1U && pixel[1] == 1U && pixel[2] == 1U) {
            if (scanline.empty() || pixel[3] == 0U || repeat_shift >= 32U) {
                diagnostics = "Radiance HDR legacy RLE contains an invalid repeat marker.";
                return false;
            }
            const std::uint64_t repeat = static_cast<std::uint64_t>(pixel[3]) << repeat_shift;
            if (repeat == 0U || repeat > width - scanline.size()) {
                diagnostics = "Radiance HDR legacy RLE repeat exceeds its scanline.";
                return false;
            }
            const auto previous = scanline.back();
            scanline.insert(scanline.end(), static_cast<std::size_t>(repeat), previous);
            repeat_shift += 8U;
        } else {
            scanline.push_back(pixel);
            repeat_shift = 0U;
        }
        if (scanline.size() == width) break;
        if (!read_rgbe(bytes, cursor, pixel)) {
            diagnostics = "Radiance HDR pixel payload is truncated.";
            return false;
        }
    }
    return true;
}

[[nodiscard]] std::array<float, 3U> rgbe_to_rgb(
    const std::array<std::uint8_t, 4U> pixel
) noexcept {
    if (pixel[3] == 0U) return {0.0F, 0.0F, 0.0F};
    const float scale = std::ldexp(1.0F, static_cast<int>(pixel[3]) - 136);
    return {
        static_cast<float>(pixel[0]) * scale,
        static_cast<float>(pixel[1]) * scale,
        static_cast<float>(pixel[2]) * scale,
    };
}

} // namespace

RadianceHdrDecodeResult decode_radiance_hdr(const std::span<const std::byte> bytes) {
    RadianceHdrDecodeResult result;
    if (bytes.empty()) {
        result.diagnostics = "Radiance HDR source is empty.";
        return result;
    }

    std::size_t cursor = 0U;
    std::string line;
    if (!read_line(bytes, cursor, line) || (line != "#?RADIANCE" && line != "#?RGBE")) {
        result.diagnostics = "Radiance HDR source is missing a supported magic header.";
        return result;
    }

    bool format_seen = false;
    bool header_terminated = false;
    while (read_line(bytes, cursor, line)) {
        line = trim(std::move(line));
        if (line.empty()) {
            header_terminated = true;
            break;
        }
        if (line.rfind("FORMAT=", 0U) == 0U) {
            if (format_seen || line != "FORMAT=32-bit_rle_rgbe") {
                result.diagnostics = format_seen
                    ? "Radiance HDR source contains multiple FORMAT declarations."
                    : "Radiance HDR source uses an unsupported pixel format.";
                return result;
            }
            format_seen = true;
        }
    }
    if (!header_terminated || !format_seen) {
        result.diagnostics = !header_terminated
            ? "Radiance HDR header is truncated."
            : "Radiance HDR source is missing FORMAT=32-bit_rle_rgbe.";
        return result;
    }

    do {
        if (!read_line(bytes, cursor, line)) {
            result.diagnostics = "Radiance HDR source is missing its resolution line.";
            return result;
        }
        line = trim(std::move(line));
    } while (line.empty());

    Orientation orientation;
    if (!parse_resolution(line, orientation)) {
        result.diagnostics = "Radiance HDR resolution/orientation is unsupported or invalid.";
        return result;
    }

    RadianceHdrImage image;
    image.width = orientation.width;
    image.height = orientation.height;
    image.rgb.resize(static_cast<std::size_t>(image.width) * image.height * 3U);

    std::vector<std::array<std::uint8_t, 4U>> scanline;
    for (std::uint32_t file_y = 0U; file_y < image.height; ++file_y) {
        std::array<std::uint8_t, 4U> first{};
        if (!read_rgbe(bytes, cursor, first)) {
            result.diagnostics = "Radiance HDR scanline header is truncated.";
            return result;
        }

        const bool new_rle_candidate = image.width >= 8U && image.width <= 32'767U &&
            first[0] == 2U && first[1] == 2U && (first[2] & 0x80U) == 0U;
        if (new_rle_candidate) {
            const std::uint32_t encoded_width =
                (static_cast<std::uint32_t>(first[2]) << 8U) | first[3];
            if (encoded_width != image.width) {
                result.diagnostics = "Radiance HDR RLE scanline width does not match the resolution.";
                return result;
            }
            if (!decode_new_scanline(bytes, cursor, image.width, scanline, result.diagnostics)) {
                return result;
            }
        } else if (!decode_old_scanline(
                       bytes,
                       cursor,
                       image.width,
                       first,
                       scanline,
                       result.diagnostics
                   )) {
            return result;
        }

        const std::uint32_t destination_y = orientation.file_rows_top_to_bottom
            ? file_y
            : image.height - 1U - file_y;
        for (std::uint32_t file_x = 0U; file_x < image.width; ++file_x) {
            const std::uint32_t destination_x = orientation.file_columns_left_to_right
                ? file_x
                : image.width - 1U - file_x;
            const auto rgb = rgbe_to_rgb(scanline[file_x]);
            const std::size_t destination =
                (static_cast<std::size_t>(destination_y) * image.width + destination_x) * 3U;
            image.rgb[destination + 0U] = rgb[0];
            image.rgb[destination + 1U] = rgb[1];
            image.rgb[destination + 2U] = rgb[2];
        }
    }

    result.image = std::move(image);
    return result;
}

} // namespace vice::tools::environment_cooker::detail

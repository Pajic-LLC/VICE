#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace vice::tools::environment_cooker::detail {

struct RadianceHdrImage {
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    std::vector<float> rgb;
};

struct RadianceHdrDecodeResult {
    std::optional<RadianceHdrImage> image;
    std::string diagnostics;
};

[[nodiscard]] RadianceHdrDecodeResult decode_radiance_hdr(std::span<const std::byte> bytes);

} // namespace vice::tools::environment_cooker::detail

#include <vice/tools/shader_cooker.hpp>

#include <charconv>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>

namespace vice::tools::shader_cooker {
namespace {

[[nodiscard]] bool parse_dimension(
    std::string_view text,
    std::uint32_t& value
) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1U);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1U);
    }
    if (text.empty()) {
        return false;
    }
    const auto [end, error] = std::from_chars(
        text.data(),
        text.data() + text.size(),
        value
    );
    return error == std::errc{} && end == text.data() + text.size() && value > 0U;
}

} // namespace

std::optional<ComputeThreadGroupReflection> parse_dxc_compute_thread_group_size(
    const std::string_view assembly
) noexcept {
    constexpr std::string_view marker = "NumThreads";
    std::size_t position = assembly.find(marker);
    while (position != std::string_view::npos) {
        std::size_t cursor = position + marker.size();
        while (cursor < assembly.size() &&
            std::isspace(static_cast<unsigned char>(assembly[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= assembly.size() || assembly[cursor] != '=') {
            position = assembly.find(marker, position + marker.size());
            continue;
        }
        ++cursor;
        while (cursor < assembly.size() &&
            std::isspace(static_cast<unsigned char>(assembly[cursor])) != 0) {
            ++cursor;
        }
        if (cursor >= assembly.size() || assembly[cursor] != '(') {
            return std::nullopt;
        }
        const std::size_t close = assembly.find(')', cursor + 1U);
        if (close == std::string_view::npos) {
            return std::nullopt;
        }

        const std::string_view dimensions = assembly.substr(cursor + 1U, close - cursor - 1U);
        const std::size_t first_comma = dimensions.find(',');
        if (first_comma == std::string_view::npos) {
            return std::nullopt;
        }
        const std::size_t second_comma = dimensions.find(',', first_comma + 1U);
        if (second_comma == std::string_view::npos ||
            dimensions.find(',', second_comma + 1U) != std::string_view::npos) {
            return std::nullopt;
        }

        ComputeThreadGroupReflection result;
        if (!parse_dimension(dimensions.substr(0U, first_comma), result.x) ||
            !parse_dimension(
                dimensions.substr(first_comma + 1U, second_comma - first_comma - 1U),
                result.y
            ) ||
            !parse_dimension(dimensions.substr(second_comma + 1U), result.z)) {
            return std::nullopt;
        }
        return result;
    }
    return std::nullopt;
}

} // namespace vice::tools::shader_cooker

#include <vice/render/mesh_asset.hpp>
#include <vice/tools/mesh_cooker.hpp>

#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace vice::tools::mesh_cooker {
namespace {

constexpr std::size_t stream_footer_bytes = 40U;
constexpr std::size_t core_size_footer_offset = 8U;

[[nodiscard]] std::uint32_t read_u32(
    const std::span<const std::byte> bytes,
    const std::size_t offset
) {
    if (offset + sizeof(std::uint32_t) > bytes.size()) return 0U;
    std::uint32_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint32_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])
        ) << (index * 8U);
    }
    return value;
}

[[nodiscard]] std::uint64_t read_u64(
    const std::span<const std::byte> bytes,
    const std::size_t offset
) {
    if (offset + sizeof(std::uint64_t) > bytes.size()) return 0U;
    std::uint64_t value = 0U;
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        value |= static_cast<std::uint64_t>(
            std::to_integer<std::uint8_t>(bytes[offset + index])
        ) << (index * 8U);
    }
    return value;
}

void overwrite_u32(
    std::vector<std::byte>& bytes,
    const std::size_t offset,
    const std::uint32_t value
) {
    for (std::size_t index = 0U; index < sizeof(value); ++index) {
        bytes[offset + index] = static_cast<std::byte>((value >> (index * 8U)) & 0xFFU);
    }
}

} // namespace

CookResult cook_v6_fixture(const MeshSource& source) {
    CookResult result = cook(source);
    if (!result.success || result.bytes.size() < stream_footer_bytes) return result;
    const auto bytes = std::span<const std::byte>{result.bytes};
    if (read_u32(bytes, render::mesh_asset_magic_size) != render::mesh_asset_format_version_v7) {
        result.success = false;
        result.diagnostics = "v6 fixture compatibility expected a v7 cooker result.";
        result.bytes.clear();
        return result;
    }
    const auto footer_offset = result.bytes.size() - stream_footer_bytes;
    const auto core_size = read_u64(bytes, footer_offset + core_size_footer_offset);
    if (core_size < render::mesh_asset_magic_size + sizeof(std::uint32_t) ||
        core_size > result.bytes.size()) {
        result.success = false;
        result.diagnostics = "v6 fixture compatibility could not locate the v7 core payload.";
        result.bytes.clear();
        return result;
    }

    result.bytes.resize(static_cast<std::size_t>(core_size));
    overwrite_u32(
        result.bytes,
        render::mesh_asset_magic_size,
        render::mesh_asset_format_version_v6
    );
    const auto decoded = render::decode_mesh_asset(result.bytes);
    if (!decoded.asset.has_value() || decoded.asset->version != render::mesh_asset_format_version_v6) {
        result.success = false;
        result.diagnostics = "v6 fixture compatibility produced an invalid legacy asset.";
        result.bytes.clear();
        return result;
    }
    result.success = true;
    result.diagnostics.clear();
    return result;
}

} // namespace vice::tools::mesh_cooker

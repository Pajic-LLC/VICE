#pragma once

#include <vice/render/mesh_asset.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace vice::render {

struct VirtualGeometryStreamPageEntry {
    std::uint32_t page_index = 0U;
    std::uint32_t vertex_count = 0U;
    std::uint32_t index_count = 0U;
    std::uint64_t payload_offset = 0U;
    std::uint64_t payload_size = 0U;
    std::uint64_t payload_fingerprint = 0U;

    friend bool operator==(const VirtualGeometryStreamPageEntry&, const VirtualGeometryStreamPageEntry&) = default;
};

struct VirtualGeometryStreamDirectory {
    std::uint64_t core_size = 0U;
    std::vector<VirtualGeometryStreamPageEntry> pages;

    friend bool operator==(const VirtualGeometryStreamDirectory&, const VirtualGeometryStreamDirectory&) = default;
};

struct VirtualGeometryStreamDirectoryResult {
    std::optional<VirtualGeometryStreamDirectory> directory;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return directory.has_value();
    }
};

struct VirtualGeometryStreamPage {
    std::vector<MeshVertex> vertices;
    std::vector<std::uint32_t> indices;

    [[nodiscard]] std::uint64_t byte_size() const noexcept {
        return static_cast<std::uint64_t>(vertices.size()) * sizeof(MeshVertex) +
            static_cast<std::uint64_t>(indices.size()) * sizeof(std::uint32_t);
    }

    friend bool operator==(const VirtualGeometryStreamPage&, const VirtualGeometryStreamPage&) = default;
};

struct VirtualGeometryStreamPageResult {
    std::optional<VirtualGeometryStreamPage> page;
    std::string diagnostics;

    [[nodiscard]] explicit operator bool() const noexcept {
        return page.has_value();
    }
};

[[nodiscard]] VirtualGeometryStreamDirectoryResult load_virtual_geometry_stream_directory(
    const std::filesystem::path& path
);

[[nodiscard]] VirtualGeometryStreamPageResult load_virtual_geometry_stream_page(
    const std::filesystem::path& path,
    const VirtualGeometryStreamDirectory& directory,
    std::uint32_t page_index
);

[[nodiscard]] VirtualGeometryStreamPageResult load_virtual_geometry_stream_page(
    const std::filesystem::path& path,
    std::uint32_t page_index
);

} // namespace vice::render

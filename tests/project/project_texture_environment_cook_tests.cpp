#include <vice/project/workspace.hpp>
#include <vice/tools/project_environment_cook.hpp>
#include <vice/tools/project_texture_cook.hpp>

#include <array>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string_view>

namespace {

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "project texture/environment cook test failed: " << message << '\n';
    std::exit(1);
}

void write_text(const std::filesystem::path& path, const std::string_view text) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "create test directory");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open text file");
    output << text;
    require(static_cast<bool>(output), "write text file");
}

void write_ppm(const std::filesystem::path& path) {
    std::error_code error;
    std::filesystem::create_directories(path.parent_path(), error);
    require(!error, "create image directory");
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    require(static_cast<bool>(output), "open PPM");
    output << "P6\n2 2\n255\n";
    constexpr std::array<unsigned char, 12> pixels{
        255U, 0U, 0U,
        0U, 255U, 0U,
        0U, 0U, 255U,
        255U, 255U, 255U,
    };
    output.write(reinterpret_cast<const char*>(pixels.data()), static_cast<std::streamsize>(pixels.size()));
    require(static_cast<bool>(output), "write PPM");
}

} // namespace

int main() {
    using namespace vice;

    std::error_code error;
    const auto root = std::filesystem::temp_directory_path(error) /
        "vice_project_texture_environment_cook_tests";
    require(!error, "resolve temporary directory");
    std::filesystem::remove_all(root, error);
    error.clear();

    const auto manifest_path = root / "project" / "vice.viceproj";
    const auto asset_root = root / "project" / "assets";
    const auto world_source = asset_root / "worlds" / "main.vworld";
    const auto image_path = asset_root / "images" / "checker.ppm";
    const auto texture_source = asset_root / "textures" / "checker.vtex";
    const auto environment_source = asset_root / "environments" / "sky.venv";
    const auto cache_base = root / "cache";

    write_text(
        manifest_path,
        "vice-project 1\n"
        "id 85000000-0000-4000-8000-000000000001\n"
        "name \"Shared Texture Environment Cook\"\n"
        "game_name \"Shared Texture Environment Cook\"\n"
        "game_target \"shared-texture-environment-cook\"\n"
        "assets \"assets\"\n"
        "startup_world \"worlds/main.vworld\"\n"
    );
    write_text(world_source, "vice-world-source 1\n");
    write_ppm(image_path);
    write_text(
        texture_source,
        "vice-texture-source 1\n"
        "id 85000000-0000-4000-8000-000000000002\n"
        "image \"images/checker.ppm\"\n"
        "color_space srgb\n"
    );
    write_text(
        environment_source,
        "vice-environment 1\n"
        "id 85000000-0000-4000-8000-000000000003\n"
        "image \"images/checker.ppm\"\n"
        "color_space srgb\n"
        "scope global\n"
        "intensity 1\n"
        "rotation_degrees 0\n"
        "irradiance_size 1 1\n"
        "specular_size 1 1\n"
        "specular_mips 1\n"
        "brdf_lut_size 1\n"
        "samples 1\n"
    );

    const auto opened = project::open_workspace(manifest_path, cache_base);
    require(static_cast<bool>(opened), "project workspace opens");

    const auto textures = tools::cook_project_textures(*opened.workspace);
    if (!textures) std::cerr << textures.diagnostics << '\n';
    require(static_cast<bool>(textures), "texture cook succeeds");
    require(textures.diagnostics.empty(), "texture diagnostics clear");
    require(textures.textures.size() == 1U, "one texture cooked");
    require(textures.textures.front().width == 2U && textures.textures.front().height == 2U,
        "texture dimensions preserved");
    require(textures.textures.front().resource_id.value.to_string() ==
        "85000000-0000-4000-8000-000000000002", "texture stable ID preserved");
    require(std::filesystem::is_regular_file(textures.textures.front().cooked), "cooked texture exists");
    auto tile_archive = textures.textures.front().cooked;
    tile_archive.replace_extension(".vtilec");
    require(std::filesystem::is_regular_file(tile_archive), "virtual texture archive exists");
    const auto texture_ids = tools::cooked_texture_ids(textures);
    require(texture_ids.size() == 1U && texture_ids.front() == textures.textures.front().resource_id,
        "texture IDs derive from shared result");

    const auto environments = tools::cook_project_environments(*opened.workspace);
    require(static_cast<bool>(environments), "environment cook succeeds");
    require(environments.diagnostics.empty(), "environment diagnostics clear");
    require(environments.environments.size() == 1U, "one environment cooked");
    require(std::filesystem::is_regular_file(environments.environments.front().cooked),
        "cooked environment exists");

    write_text(
        asset_root / "textures" / "duplicate.vtex",
        "vice-texture-source 1\n"
        "id 85000000-0000-4000-8000-000000000002\n"
        "image \"images/checker.ppm\"\n"
        "color_space srgb\n"
    );
    const auto duplicate_texture = tools::cook_project_textures(*opened.workspace);
    require(!static_cast<bool>(duplicate_texture), "duplicate texture stable ID rejected");
    require(duplicate_texture.diagnostics.find("Duplicate or invalid") != std::string::npos,
        "duplicate texture diagnostic is explicit");

    write_text(
        asset_root / "environments" / "second.venv",
        "vice-environment 1\n"
        "id 85000000-0000-4000-8000-000000000004\n"
        "image \"images/checker.ppm\"\n"
        "color_space srgb\n"
        "scope global\n"
        "irradiance_size 1 1\n"
        "specular_size 1 1\n"
        "specular_mips 1\n"
        "brdf_lut_size 1\n"
        "samples 1\n"
    );
    const auto duplicate_global = tools::cook_project_environments(*opened.workspace);
    require(!static_cast<bool>(duplicate_global), "second global environment rejected");
    require(duplicate_global.diagnostics.find("at most one global environment") != std::string::npos,
        "global environment diagnostic is explicit");

    std::filesystem::remove_all(root, error);
    require(!error, "remove test directory");
    return 0;
}

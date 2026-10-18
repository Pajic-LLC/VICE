#include <vice/render/texture_asset.hpp>
#include <vice/runtime/texture.hpp>
#include <vice/world/persistent_id.hpp>
#include <vice/world/resource.hpp>

#include <cassert>
#include <filesystem>
#include <fstream>
#include <span>

namespace {

vice::world::PersistentId required_id(const char* text) {
    const auto parsed = vice::world::PersistentId::parse(text);
    assert(parsed.has_value());
    return *parsed;
}

void write_bytes(const std::filesystem::path& path, const std::span<const std::byte> bytes) {
    std::ofstream output{path, std::ios::binary | std::ios::trunc};
    assert(output);
    output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    assert(output);
}

} // namespace

int main() {
    using namespace vice;

    const auto root = std::filesystem::temp_directory_path() / "vice_runtime_texture_tests";
    std::error_code error;
    std::filesystem::remove_all(root, error);
    error.clear();
    std::filesystem::create_directories(root, error);
    assert(!error);

    const auto id = required_id("71000000-0000-4000-8000-000000000001");
    render::TextureAsset asset;
    asset.stable_id = id.bytes();
    asset.source_fingerprint = 99U;
    asset.width = 2U;
    asset.height = 1U;
    asset.color_space = render::TextureColorSpace::srgb;
    asset.rgba8 = {
        std::byte{255U}, std::byte{0U}, std::byte{0U}, std::byte{255U},
        std::byte{0U}, std::byte{128U}, std::byte{255U}, std::byte{255U},
    };
    const auto encoded = render::encode_texture_asset(asset);
    assert(encoded);
    write_bytes(root / "surface.vtexc", encoded.bytes);

    runtime::CookedTextureCatalog catalog;
    assert(catalog.load(root));
    assert(catalog.records().size() == 1U);
    const world::ResourceId resource{id};
    const auto* found = catalog.find(resource);
    assert(found != nullptr);
    assert(found->asset.width == 2U);
    assert(found->asset.color_space == render::TextureColorSpace::srgb);

    write_bytes(root / "duplicate.vtexc", encoded.bytes);
    assert(!catalog.load(root));
    assert(catalog.diagnostics().find(id.to_string()) != std::string::npos);

    std::filesystem::remove_all(root, error);
    assert(!error);
    return 0;
}

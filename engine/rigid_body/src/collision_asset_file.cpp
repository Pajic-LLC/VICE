#include <vice/rigid_body/collision_asset.hpp>

#include <fstream>
#include <iterator>
#include <span>
#include <vector>

namespace vice::rigid_body {

CollisionAssetDecodeResult load_collision_asset(const std::filesystem::path& path) {
    CollisionAssetDecodeResult result;
    std::ifstream input{path, std::ios::binary};
    if (!input) {
        result.diagnostics = "Could not open cooked collision asset: " + path.string();
        return result;
    }
    const std::vector<char> raw{
        std::istreambuf_iterator<char>{input},
        std::istreambuf_iterator<char>{},
    };
    if (!input.eof() && input.fail()) {
        result.diagnostics = "Could not read cooked collision asset: " + path.string();
        return result;
    }
    return decode_collision_asset(std::as_bytes(std::span<const char>{raw}));
}

} // namespace vice::rigid_body

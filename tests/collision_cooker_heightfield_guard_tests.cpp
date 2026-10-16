#include <vice/tools/collision_cooker.hpp>

#include <array>
#include <cassert>
#include <string>

int main() {
    using namespace vice;

    rigid_body::HeightFieldShapeDesc terrain;
    terrain.sample_count = 4;
    terrain.samples.assign(16U, 0.0F);

    const std::array shapes{
        rigid_body::CollisionAssetShape{
            .id = rigid_body::ShapeId{1},
            .desc = terrain,
        },
    };

    const auto result = tools::collision_cooker::cook(shapes);
    assert(!result.success);
    assert(result.bytes.empty());
    assert(result.diagnostics.find("VICECOLL version 1") != std::string::npos);

    return 0;
}

#include "powered_hinge_fixture.hpp"
int main() {
    auto fixture = joint_fixture();
    world::World w;
    require(w.restore(fixture), "restore");
    auto owner = *w.find(joint_id(3)), body = *w.find(joint_id(2));
    const auto original = world::encode_world_snapshot(w.snapshot()).bytes;
    auto encoded = world::decode_world_snapshot(original);
    require(encoded && encoded.snapshot->entities.back().powered_hinge ==
                           fixture.entities.back().powered_hinge,
            "v23 preserves frames and drive");
    require(world::encode_world_snapshot(*encoded.snapshot).bytes == original,
            "deterministic binary");
    auto text = tools::world_cooker::serialize_source_manifest(w.snapshot());
    auto parsed = tools::world_cooker::parse_source_manifest(text);
    require(parsed.snapshot &&
                tools::world_cooker::serialize_source_manifest(*parsed.snapshot) == text,
            "deterministic source");
    require(!w.destroy_entity(body) && !w.clear_rigid_box(body) && !w.clear_spatial_state(body),
            "live endpoints cannot disappear");
    auto h = *w.powered_hinge(owner);
    h.drive.max_torque = std::numeric_limits<float>::quiet_NaN();
    require(!w.set_powered_hinge(owner, h), "NaN rejected");
    h = *w.powered_hinge(owner);
    h.second_frame.normal = {0, 0, 1};
    require(!w.set_powered_hinge(owner, h), "parallel frame axes rejected");
    h = *w.powered_hinge(owner);
    h.drive.target_angle = 2;
    require(!w.set_powered_hinge(owner, h), "out-of-limit target rejected");
    h = *w.powered_hinge(owner);
    h.second = joint_id(99);
    auto bad = fixture;
    bad.entities.back().powered_hinge = h;
    require(!w.restore(bad) && world::encode_world_snapshot(w.snapshot()).bytes == original,
            "invalid restore has no side effects");
    tools::world_authoring::Transaction tx;
    tx.operations = {tools::world_authoring::SetPoweredHinge{joint_id(3), {}},
                     tools::world_authoring::SetRigidBox{joint_id(2), {}}};
    require(tools::world_authoring::apply(fixture, tx).success,
            "atomic explicit joint/body removal");
    tx.operations = {tools::world_authoring::SetRigidBox{joint_id(2), {}}};
    require(!tools::world_authoring::apply(fixture, tx).success, "endpoint-only removal rejected");
    auto truncated = original;
    truncated.pop_back();
    require(!world::decode_world_snapshot(truncated), "truncation rejected");
    world::World empty;
    auto legacy = world::encode_world_snapshot(empty.snapshot()).bytes;
    legacy.erase(legacy.end() - 34, legacy.end() - 26);
    legacy[8] = std::byte{22};
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < legacy.size(); ++i) {
        hash ^= std::to_integer<unsigned char>(legacy[i]);
        hash *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        legacy[32 + i] = static_cast<std::byte>(hash >> (i * 8));
    require(static_cast<bool>(world::decode_world_snapshot(legacy)),
            "v22 remains readable with no joint table");
    std::cout << "powered hinge persistence/validation passed\n";
}

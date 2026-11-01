#include "swing_twist_fixture.hpp"
int main() {
    const auto fixture = swing_fixture();
    world::World w;
    require(w.restore(fixture), "swing fixture");
    const auto owner = *w.find(joint_id(3)), body = *w.find(joint_id(2));
    require(w.snapshot(world::ComponentPayloadSnapshot::render).swing_twist_joints.empty(),
            "render projection skips joint topology");
    const auto bytes = world::encode_world_snapshot(w.snapshot()).bytes;
    const auto read = world::decode_world_snapshot(bytes);
    require(read && read.snapshot->swing_twist_joints.size() == 1 &&
                read.snapshot->swing_twist_joints[0].joint == fixture.swing_twist_joints[0].joint,
            "binary preserves independent axes");
    require(world::encode_world_snapshot(*read.snapshot).bytes == bytes, "deterministic binary");
    const auto text = tools::world_cooker::serialize_source_manifest(w.snapshot());
    auto source = tools::world_cooker::parse_source_manifest(text);
    require(source.snapshot && source.snapshot->swing_twist_joints.size() == 1 &&
                tools::world_cooker::serialize_source_manifest(*source.snapshot) == text,
            "source round trip");
    require(!w.destroy_entity(body) && !w.clear_rigid_box(body) && !w.clear_spatial_state(body),
            "endpoint lifetime");
    auto h = *w.swing_twist_joint(owner);
    auto invalid = h;
    invalid.configuration.drive.target_orientation = {0, 0, 0, 0};
    require(!w.set_swing_twist_joint(owner, invalid), "zero quaternion rejected");
    invalid = h;
    invalid.configuration.drive.target_orientation = swing_target(0, 1.5F, 0);
    require(!w.set_swing_twist_joint(owner, invalid), "cone violation rejected");
    invalid = h;
    invalid.configuration.drive.target_orientation = swing_target(1.5F, 0, 0);
    require(!w.set_swing_twist_joint(owner, invalid), "twist violation rejected");
    invalid = h;
    invalid.configuration.drive.max_twist_torque = std::numeric_limits<float>::quiet_NaN();
    require(!w.set_swing_twist_joint(owner, invalid), "NaN rejected");
    auto bad = fixture;
    bad.swing_twist_joints.push_back(bad.swing_twist_joints[0]);
    require(!w.restore(bad) && world::encode_world_snapshot(w.snapshot()).bytes == bytes,
            "duplicate restore atomic");
    bad = fixture;
    bad.swing_twist_joints[0].joint.second = joint_id(99);
    require(!w.restore(bad) && world::encode_world_snapshot(w.snapshot()).bytes == bytes,
            "missing endpoint restore atomic");
    bad = fixture;
    bad.entities.back().powered_hinge = joint_fixture().entities.back().powered_hinge;
    require(!w.restore(bad), "one joint type per owner");
    const auto revision = w.powered_hinge_revision();
    h.configuration.drive.target_orientation = swing_target(-.2F, -.3F, .2F);
    require(w.set_swing_twist_joint(owner, h) && w.powered_hinge_revision() == revision,
            "drive edits preserve binding");
    h.first_frame.anchor.x = .1F;
    require(w.set_swing_twist_joint(owner, h) && w.powered_hinge_revision() != revision,
            "structural edits invalidate binding");
    tools::world_authoring::Transaction tx;
    tx.operations = {tools::world_authoring::SetRigidBox{joint_id(2), {}}};
    require(!tools::world_authoring::apply(fixture, tx), "endpoint removal fails transactionally");
    tx.operations.insert(tx.operations.begin(),
                         tools::world_authoring::SetSwingTwistJoint{joint_id(3), {}});
    require(tools::world_authoring::apply(fixture, tx).success, "explicit joint then body removal");
    require(w.clear_swing_twist_joint(owner) && w.clear_rigid_box(body), "endpoint index cleared");
    world::World empty;
    auto old = world::encode_world_snapshot(empty.snapshot()).bytes;
    old.erase(old.end() - 34, old.end() - 26);
    old[8] = std::byte{23};
    std::uint64_t hash = 14695981039346656037ULL;
    for (std::size_t i = 40; i < old.size(); ++i) {
        hash ^= std::to_integer<unsigned char>(old[i]);
        hash *= 1099511628211ULL;
    }
    for (unsigned i = 0; i < 8; ++i)
        old[32 + i] = static_cast<std::byte>(hash >> (i * 8));
    require(static_cast<bool>(world::decode_world_snapshot(old)), "v23 reader compatibility");
    world::World stress;
    require(stress.restore(swing_fixture()), "stress fixture");
    // Shared endpoints exercise sparse lookup, canonical ordering and incident-reference cleanup.
    for (unsigned i = 4; i < 1028; ++i) {
        auto id = joint_id(3);
        auto data = id.bytes();
        data[12] = static_cast<std::uint8_t>(i >> 8);
        data[13] = static_cast<std::uint8_t>(i);
        data[15] = 0;
        id = world::PersistentId{data};
        const auto entity = *stress.create_entity(id);
        require(stress.set_swing_twist_joint(entity, fixture.swing_twist_joints[0].joint),
                "stress attach");
    }
    const auto packed = world::encode_world_snapshot(stress.snapshot());
    require(static_cast<bool>(packed), "stress encode");
    const auto unpacked = world::decode_world_snapshot(packed.bytes);
    require(unpacked && unpacked.snapshot->swing_twist_joints.size() == 1025 &&
                world::encode_world_snapshot(*unpacked.snapshot).bytes == packed.bytes,
            "stress deterministic round trip");
    std::cout << "Sparse joints=1025 encoded_bytes=" << packed.bytes.size()
              << " record_bytes=" << sizeof(world::PoweredSwingTwist)
              << " owned_record_bytes=" << sizeof(world::OwnedSwingTwist)
              << " snapshot_entity_bytes=" << sizeof(world::SnapshotEntity) << '\n';
    std::cout << "Swing/twist persistence and authoring passed\n";
}

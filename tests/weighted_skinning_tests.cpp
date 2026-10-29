#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <vice/presentation/assets.hpp>
namespace {
using namespace vice;
void require(bool ok, const char *s) {
    if (!ok) {
        std::cerr << "Skinning: " << s << '\n';
        std::exit(1);
    }
}
bool near(float a, float b) { return std::abs(a - b) < 1e-4F; }
presentation::Rig rig() {
    presentation::Rig r;
    r.id = {*world::PersistentId::parse("d1000000-0000-4000-8000-000000000001")};
    for (unsigned i = 0; i < 8; ++i)
        r.nodes.push_back({"joint" + std::to_string(i), -1, {{float(i), 0, 0}, {}, 1}, {}});
    r.nodes.push_back({"mesh",
                       -1,
                       {{20, 0, 0}, {}, 1},
                       {*world::PersistentId::parse("d1000000-0000-4000-8000-000000000002")}});
    presentation::Clip c;
    c.name = "move";
    c.duration = 2;
    r.clips.push_back(c);
    presentation::Skin skin;
    skin.node = 8;
    for (unsigned i = 0; i < 8; ++i) {
        auto bind = math::identity();
        bind(0, 3) = -float(i);
        skin.joints.push_back({i, bind});
    }
    presentation::SkinWeights v;
    for (unsigned i = 0; i < 8; ++i) {
        v.joints[i] = static_cast<std::uint16_t>(i);
        v.weights[i] = 0.125F;
    }
    skin.vertices.push_back(v);
    r.skins.push_back(skin);
    return r;
}
} // namespace
int main() {
    using namespace vice;
    auto r = rig();
    require(presentation::valid(r), "eight-joint rig");
    std::vector<presentation::Pose> pose;
    std::vector<math::Mat4> palette;
    require(presentation::sample_pose(r, 0, 0, pose) &&
                presentation::skin_palette(r.skins[0], pose, palette),
            "rest palette");
    std::vector<presentation::SkinVertex> input{{{2, 1, 0}, {0, 1, 0}}}, out;
    require(presentation::deform_skin(r.skins[0], palette, input, out) &&
                near(out[0].position.x, 2) && near(out[0].position.y, 1),
            "inverse binds cancel joint transforms; mesh node transform is not applied twice");
    for (auto &p : pose)
        p.translation.y += 4;
    require(presentation::skin_palette(r.skins[0], pose, palette) &&
                presentation::deform_skin(r.skins[0], palette, input, out) &&
                near(out[0].position.y, 5),
            "all eight influences contribute");
    palette.assign(8, math::identity());
    for (auto &m : palette) {
        m(0, 0) = 2;
        m(1, 1) = 1;
    }
    input[0].normal = math::normalize({1, 1, 0});
    require(presentation::deform_skin(r.skins[0], palette, input, out) &&
                near(out[0].normal.x, 0.4472136F) && near(out[0].normal.y, 0.8944272F),
            "inverse transpose normals under nonuniform affine skin matrix");
    const auto preserved = out;
    auto malformed = r.skins[0];
    malformed.vertices[0].weights[0] = -1;
    require(!presentation::deform_skin(malformed, palette, input, out) &&
                out[0].position.x == preserved[0].position.x,
            "invalid weights leave output unchanged");
    malformed = r.skins[0];
    malformed.vertices[0].joints[0] = 99;
    require(!presentation::deform_skin(malformed, palette, input, out), "invalid joint index");
    malformed = r.skins[0];
    malformed.vertices[0].weights[0] = std::numeric_limits<float>::quiet_NaN();
    require(!presentation::valid_skin(r, malformed), "nonfinite weight rejected");
    malformed = r.skins[0];
    malformed.vertices[0].joints[1] = 0;
    require(!presentation::valid_skin(r, malformed), "duplicate positive influences rejected");
    auto bad = r;
    bad.skins.push_back(bad.skins[0]);
    require(!presentation::valid(bad), "duplicate skin node");
    bad = r;
    bad.skins[0].joints[0].inverse_bind(3, 0) = 1;
    require(!presentation::valid(bad), "nonaffine inverse bind rejected");
    bad = r;
    bad.skins[0].joints[0].node = 99;
    require(!presentation::valid(bad), "missing persistent asset joint");
    // Cubic tangents are derivatives and must be scaled by the interval duration.
    presentation::Track t;
    t.node = 0;
    t.path = presentation::Path::translation;
    t.interpolation = presentation::Interpolation::cubic_spline;
    presentation::Key a, b;
    a.time = 0;
    b.time = 2;
    a.value = {0, 0, 0, 0};
    b.value = {0, 0, 0, 0};
    a.out_tangent = {2, 0, 0, 0};
    t.keys = {a, b};
    r.clips[0].tracks = {t};
    require(presentation::valid(r) && presentation::sample_pose(r, 0, 1, pose) &&
                near(pose[0].translation.x, 0.5F),
            "interval-scaled cubic Hermite interpolation");
    require(presentation::sample_pose(r, 0, 2, pose) && near(pose[0].translation.x, 0),
            "cubic endpoint clamp");
    auto rotation = t;
    rotation.path = presentation::Path::rotation;
    rotation.keys[0].value = {0, 0, 0, 1};
    rotation.keys[1].value = {0, 0, 1, 0};
    rotation.keys[0].out_tangent = {};
    r.clips[0].tracks = {rotation};
    require(presentation::valid(r) && presentation::sample_pose(r, 0, 1, pose) &&
                near(pose[0].rotation.z, 0.7071068F) && near(pose[0].rotation.w, 0.7071068F),
            "cubic quaternion normalization");
    const auto text = presentation::serialize_rig(r);
    require(text.starts_with("vice-rig 2"), "versioned skin and cubic format");
    const auto folder = std::filesystem::temp_directory_path() /
                        ("vice-skin-" + world::PersistentId::random().to_string());
    std::filesystem::create_directories(folder);
    struct Cleanup {
        std::filesystem::path p;
        ~Cleanup() {
            std::error_code ec;
            std::filesystem::remove_all(p, ec);
        }
    } cleanup{folder};
    const auto path = folder / "rig.vrig";
    {
        std::ofstream file(path);
        file << text;
    }
    const auto read = presentation::read_rig(path);
    require(read && presentation::serialize_rig(*read) == text, "deterministic rig round trip");
    {
        std::ofstream file(path);
        file << text.substr(0, text.size() - 12);
    }
    require(!presentation::read_rig(path), "truncated tangent record rejected");
    auto legacy = r;
    legacy.skins.clear();
    legacy.clips[0].tracks.clear();
    const auto old = presentation::serialize_rig(legacy);
    require(old.starts_with("vice-rig 1"), "legacy rigid assets keep byte-compatible format");
    {
        std::ofstream file(path);
        file << old;
    }
    require(presentation::read_rig(path).has_value(), "v1 rig reader retained");
    std::cout << "Weighted skinning, bind matrices, eight influences, normals, cubic curves and "
                 "versioned assets passed\n";
}

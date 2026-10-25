#include <vice/runtime/reflection_surface_guide.hpp>
#include <vice/runtime/scene.hpp>

#include <array>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

namespace {

int failures = 0;

void require(const bool condition, const std::string_view message) {
    if (condition) return;
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
}

[[nodiscard]] bool near(const float left, const float right, const float epsilon = 1.0e-4F) {
    return std::abs(left - right) <= epsilon;
}

[[nodiscard]] std::string read_text(const char* path) {
    std::ifstream stream{path, std::ios::binary};
    return std::string{
        std::istreambuf_iterator<char>{stream},
        std::istreambuf_iterator<char>{},
    };
}

void test_encoding_contract() {
    const auto neutral = vice::runtime::runtime_reflection_surface_guide_neutral();
    require(near(neutral.red, 0.5F), "neutral guide normal X must encode +Z");
    require(near(neutral.green, 0.5F), "neutral guide normal Y must encode +Z");
    require(near(neutral.blue, 1.0F), "neutral guide roughness must be fully rough");
    require(near(neutral.alpha, 0.0F), "neutral guide metallic must be dielectric");

    const auto forward = vice::runtime::encode_runtime_reflection_surface_guide(
        vice::math::Vec3{0.0F, 0.0F, 1.0F},
        0.35F,
        0.8F
    );
    require(near(forward.normal_oct[0], 0.5F), "+Z oct X must be centered");
    require(near(forward.normal_oct[1], 0.5F), "+Z oct Y must be centered");
    require(near(forward.roughness, 0.35F), "roughness must preserve authored PBR value");
    require(near(forward.metallic, 0.8F), "metallic must preserve authored PBR value");

    const auto clamped = vice::runtime::encode_runtime_reflection_surface_guide(
        vice::math::Vec3{0.0F, 1.0F, 0.0F},
        -4.0F,
        3.0F
    );
    require(near(clamped.roughness, 0.0F), "guide roughness must clamp to PBR domain");
    require(near(clamped.metallic, 1.0F), "guide metallic must clamp to PBR domain");

    const auto invalid = vice::runtime::encode_runtime_reflection_surface_guide(
        vice::math::Vec3{},
        0.25F,
        1.0F
    );
    require(near(invalid.normal_oct[0], 0.5F) && near(invalid.normal_oct[1], 0.5F),
        "invalid normals must fall back to neutral encoding");
    require(near(invalid.roughness, 1.0F) && near(invalid.metallic, 0.0F),
        "invalid guide samples must fall back to neutral material values");

    constexpr std::array normals{
        vice::math::Vec3{1.0F, 0.0F, 0.0F},
        vice::math::Vec3{0.0F, 1.0F, 0.0F},
        vice::math::Vec3{0.0F, 0.0F, -1.0F},
        vice::math::Vec3{0.3F, -0.7F, 0.6F},
        vice::math::Vec3{-0.4F, 0.2F, -0.9F},
    };
    for (const auto normal : normals) {
        const auto encoded = vice::runtime::encode_runtime_reflection_surface_guide(
            normal,
            0.5F,
            0.25F
        );
        const auto decoded = vice::runtime::decode_runtime_reflection_surface_guide_normal(encoded);
        const auto expected = vice::math::normalize(normal);
        require(vice::math::dot(decoded, expected) > 0.999F,
            "octahedral guide normal must round-trip across both hemispheres");
    }
}

void test_public_default_state() {
    vice::runtime::RuntimeSceneRenderer renderer;
    require(renderer.reflection_surface_guide() == nullptr,
        "uninitialized Runtime renderer must expose no reflection surface guide");
}

void test_shader_material_parity_contract() {
    const auto shader = read_text(VICE_REFLECTION_SURFACE_GUIDE_SHADER_SOURCE);
    require(!shader.empty(), "reflection surface-guide shader source must be readable");
    require(shader.find("vice_reflection_surface_guide_vs") != std::string::npos,
        "guide shader must expose its vertex entry point");
    require(shader.find("vice_reflection_surface_guide_fs") != std::string::npos,
        "guide shader must expose its fragment entry point");
    require(shader.find("material.normal_texture") != std::string::npos &&
            shader.find("material.normal_scale") != std::string::npos,
        "guide normal must use the production normal-map material fields");
    require(shader.find("material.roughness * orm.g") != std::string::npos,
        "guide roughness must match GPU PBR ORM semantics");
    require(shader.find("material.metallic * orm.b") != std::string::npos,
        "guide metallic must match GPU PBR ORM semantics");
    require(shader.find("VICE_VIRTUAL_TEXTURE_FLAG") != std::string::npos &&
            shader.find("vice_virtual_record") != std::string::npos,
        "guide must preserve virtual-texture material sampling");
    require(shader.find("RWStructuredBuffer") == std::string::npos,
        "guide must not create a second virtual-texture feedback producer");
}

void test_depth_and_order_contract() {
    const auto implementation = read_text(VICE_REFLECTION_SURFACE_GUIDE_CPP_SOURCE);
    const auto bridge = read_text(VICE_REFLECTION_SURFACE_GUIDE_BRIDGE_SOURCE);
    const auto legacy = read_text(VICE_RUNTIME_SCENE_LEGACY_SOURCE);

    require(implementation.find("render::TextureFormat::rgba16_float") != std::string::npos,
        "guide target must use the compact portable RGBA16F format");
    require(implementation.find(".write_enabled = false") != std::string::npos &&
            implementation.find(".compare = render::CompareOp::equal") != std::string::npos,
        "guide pass must depth-test against authoritative depth without rewriting it");
    require(implementation.find("render_pass.clear_depth_attachment = false") != std::string::npos,
        "guide pass must preserve the existing camera depth attachment");
    require(implementation.find("RuntimeReflectionSurfaceGuideFrame{.depth = current_depth}") !=
            std::string::npos &&
            implementation.find("frame.draws.empty()") != std::string::npos,
        "unsupported/direct-only frames must clear the guide to its neutral fallback");
    require(bridge.find("runtime_reflection_surface_guide_needs_camera_depth_prepass") !=
            std::string::npos,
        "reflection guide must independently request authoritative current depth");

    const auto hook_position = bridge.find("#define encode_motion_vectors");
    const auto ao_position = bridge.find(
        "::vice::runtime::detail::runtime_ray_ambient_occlusion_after_motion_vectors(",
        hook_position
    );
    const auto guide_position = bridge.find(
        "::vice::runtime::detail::runtime_reflection_surface_guide_after_motion_vectors(",
        hook_position
    );
    require(hook_position != std::string::npos && ao_position != std::string::npos &&
            guide_position != std::string::npos && ao_position < guide_position,
        "guide must run from the shared pre-PBR motion/depth stage after AO temporal reuse");
    require(bridge.find("gpu_pbr_plan.batch_meshes") != std::string::npos &&
            bridge.find("gpu_cluster_pbr_plan.batch_meshes") != std::string::npos,
        "guide must cover both indexed and cluster GPU PBR submission paths");
    require(legacy.find("Current depth is authoritative here") != std::string::npos &&
            legacy.find("if (!encode_motion_vectors()) return false;") != std::string::npos,
        "guide scheduling hook must remain attached to authoritative pre-PBR depth/motion timing");
}

} // namespace

int main() {
    test_encoding_contract();
    test_public_default_state();
    test_shader_material_parity_contract();
    test_depth_and_order_contract();

    if (failures != 0) {
        std::cerr << failures << " reflection surface-guide test(s) failed\n";
        return 1;
    }
    std::cout << "Runtime reflection surface-guide tests passed\n";
    return 0;
}

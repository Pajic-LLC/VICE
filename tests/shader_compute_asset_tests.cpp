#include <vice/render/shader_asset.hpp>

#include <cassert>
#include <string>
#include <string_view>

namespace {

constexpr std::string_view valid_compute_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "main",
  "stage": "compute",
  "artifacts": {
    "dxil": "shader.dxil",
    "dxc_assembly": "shader.dxc.txt",
    "metallib": null,
    "metal_reflection": null
  },
  "metal_binding": {
    "version": 2,
    "model": null,
    "descriptor_entry_size": 24,
    "locations": []
  },
  "reflection": {
    "version": 1,
    "thread_group_size": {"x": 8, "y": 4, "z": 2},
    "resources": []
  }
})JSON";

} // namespace

int main() {
    using namespace vice::render;

    const CookedShaderManifest manifest = parse_cooked_shader_manifest(valid_compute_manifest);
    assert(manifest.valid);
    assert(manifest.diagnostics.empty());
    assert(manifest.stage == ShaderStage::compute);
    assert(manifest.compute_thread_group_size.has_value());
    assert(manifest.compute_thread_group_size->x == 8U);
    assert(manifest.compute_thread_group_size->y == 4U);
    assert(manifest.compute_thread_group_size->z == 2U);

    std::string missing{valid_compute_manifest};
    const std::string field =
        "    \"thread_group_size\": {\"x\": 8, \"y\": 4, \"z\": 2},\n";
    const std::size_t position = missing.find(field);
    assert(position != std::string::npos);
    missing.erase(position, field.size());
    const CookedShaderManifest missing_group = parse_cooked_shader_manifest(missing);
    assert(!missing_group.valid);
    assert(missing_group.diagnostics.find("thread-group") != std::string::npos);

    std::string zero{valid_compute_manifest};
    const std::size_t x_position = zero.find("\"x\": 8");
    assert(x_position != std::string::npos);
    zero.replace(x_position, std::string{"\"x\": 8"}.size(), "\"x\": 0");
    const CookedShaderManifest zero_group = parse_cooked_shader_manifest(zero);
    assert(!zero_group.valid);
    assert(zero_group.diagnostics.find("invalid") != std::string::npos);

    std::string fragment{valid_compute_manifest};
    const std::size_t stage_position = fragment.find("\"stage\": \"compute\"");
    assert(stage_position != std::string::npos);
    fragment.replace(
        stage_position,
        std::string{"\"stage\": \"compute\""}.size(),
        "\"stage\": \"fragment\""
    );
    const CookedShaderManifest wrong_stage = parse_cooked_shader_manifest(fragment);
    assert(!wrong_stage.valid);
    assert(wrong_stage.diagnostics.find("Non-compute") != std::string::npos);

    return 0;
}

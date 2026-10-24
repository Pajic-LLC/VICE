#include <vice/render/shader_asset.hpp>
#include <vice/tools/shader_cooker.hpp>

#include <cassert>
#include <string_view>

int main() {
    namespace render = vice::render;
    namespace cooker = vice::tools::shader_cooker;

    constexpr std::string_view assembly = R"DXC(
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Scene                    RTAccelerationStructure      NA          NA      T0             t0     1
; Albedo                                  texture    f32          2d      T1             t1     1
;
)DXC";
    const auto reflected = cooker::parse_dxc_resource_bindings(assembly);
    assert(reflected.valid);
    assert(reflected.resources.size() == 2U);
    assert(reflected.resources[0].register_class == cooker::ShaderRegisterClass::shader_resource);
    assert(reflected.resources[0].kind == cooker::ShaderResourceKind::acceleration_structure);
    assert(reflected.resources[0].binding == 0U);
    assert(reflected.resources[1].kind == cooker::ShaderResourceKind::generic);
    assert(reflected.resources[1].binding == 1U);

    constexpr std::string_view invalid_register = R"DXC(
; Resource Bindings:
;
; Name                                 Type  Format         Dim      ID      HLSL Bind  Count
; ------------------------------ ---------- ------- ----------- ------- -------------- ------
; Scene                    RTAccelerationStructure      NA          NA      U0             u0     1
;
)DXC";
    const auto rejected = cooker::parse_dxc_resource_bindings(invalid_register);
    assert(!rejected.valid);
    assert(!rejected.diagnostics.empty());

    constexpr std::string_view version_two_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "main",
  "stage": "fragment",
  "artifacts": {
    "dxil": "shader.dxil",
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
    "version": 2,
    "thread_group_size": null,
    "resources": [
      {
        "name": "Scene",
        "class": "shader_resource",
        "kind": "acceleration_structure",
        "binding": 0,
        "space": 0,
        "count": 1,
        "unbounded": false,
        "type": "RTAccelerationStructure",
        "format": "NA",
        "dimension": "NA"
      }
    ]
  }
})JSON";
    const auto version_two = render::parse_cooked_shader_manifest(version_two_manifest);
    assert(version_two.valid);
    assert(version_two.resources.size() == 1U);
    assert(version_two.resources[0].kind == render::ShaderResourceKind::acceleration_structure);
    assert(version_two.resources[0].register_class == render::ShaderRegisterClass::shader_resource);

    constexpr std::string_view version_one_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "main",
  "stage": "fragment",
  "artifacts": {
    "dxil": "shader.dxil",
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
    "thread_group_size": null,
    "resources": [
      {
        "name": "Albedo",
        "class": "shader_resource",
        "binding": 1,
        "space": 0,
        "count": 1,
        "unbounded": false,
        "type": "texture",
        "format": "f32",
        "dimension": "2d"
      }
    ]
  }
})JSON";
    const auto version_one = render::parse_cooked_shader_manifest(version_one_manifest);
    assert(version_one.valid);
    assert(version_one.resources.size() == 1U);
    assert(version_one.resources[0].kind == render::ShaderResourceKind::generic);

    constexpr std::string_view unknown_kind_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "main",
  "stage": "fragment",
  "artifacts": {"dxil": "shader.dxil", "metallib": null, "metal_reflection": null},
  "metal_binding": {"version": 2, "model": null, "descriptor_entry_size": 24, "locations": []},
  "reflection": {
    "version": 2,
    "thread_group_size": null,
    "resources": [
      {"name": "Scene", "class": "shader_resource", "kind": "mystery", "binding": 0, "space": 0, "count": 1, "unbounded": false}
    ]
  }
})JSON";
    const auto unknown_kind = render::parse_cooked_shader_manifest(unknown_kind_manifest);
    assert(!unknown_kind.valid);
    assert(!unknown_kind.diagnostics.empty());

    constexpr std::string_view wrong_class_manifest = R"JSON({
  "format": "vice.shader",
  "version": 4,
  "entry_point": "main",
  "stage": "fragment",
  "artifacts": {"dxil": "shader.dxil", "metallib": null, "metal_reflection": null},
  "metal_binding": {"version": 2, "model": null, "descriptor_entry_size": 24, "locations": []},
  "reflection": {
    "version": 2,
    "thread_group_size": null,
    "resources": [
      {"name": "Scene", "class": "unordered_access", "kind": "acceleration_structure", "binding": 0, "space": 0, "count": 1, "unbounded": false}
    ]
  }
})JSON";
    const auto wrong_class = render::parse_cooked_shader_manifest(wrong_class_manifest);
    assert(!wrong_class.valid);
    assert(!wrong_class.diagnostics.empty());

    const render::ShaderResourceBindingDesc direct_valid{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 2U,
        .space = 0U,
        .count = 1U,
        .unbounded = false,
        .kind = render::ShaderResourceKind::acceleration_structure,
    };
    assert(render::is_valid(direct_valid));
    auto direct_invalid = direct_valid;
    direct_invalid.register_class = render::ShaderRegisterClass::unordered_access;
    assert(!render::is_valid(direct_invalid));

    return 0;
}

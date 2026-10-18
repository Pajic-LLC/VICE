from pathlib import Path


def read(path: str) -> str:
    return Path(path).read_text()


def write(path: str, text: str) -> None:
    target = Path(path)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(text)


def replace_once(path: str, old: str, new: str) -> None:
    text = read(path)
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{path}: expected one match, got {count}: {old[:120]!r}")
    write(path, text.replace(old, new, 1))


replace_once(
    "engine/runtime/include/vice/runtime/scene.hpp",
    "#include <filesystem>\n",
    "#include <cstddef>\n#include <filesystem>\n",
)
replace_once(
    "engine/runtime/include/vice/runtime/scene.hpp",
    '''struct RuntimeCameraView {
    world::PersistentId entity{};
    world::Camera camera{};
    math::Vec3 position{};
    math::Mat4 view_projection{};
};

''',
    '''struct RuntimeCameraView {
    world::PersistentId entity{};
    world::Camera camera{};
    math::Vec3 position{};
    math::Mat4 view_projection{};
};

inline constexpr std::size_t max_runtime_lights = 8U;

struct RuntimeLight {
    world::PersistentId entity{};
    world::Light light{};
    math::Vec3 position{};
    math::Vec3 direction{};
};

[[nodiscard]] std::vector<RuntimeLight> collect_runtime_lights(const world::World& world);

''',
)

replace_once(
    "engine/runtime/src/scene.cpp",
    '''struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    float camera_position[4]{};
    float light_direction[4]{};
    float light_color[4]{};
};

static_assert(sizeof(SceneConstants) == 176U);
''',
    '''struct SceneLightConstants {
    float position_or_direction[4]{};
    float color_intensity[4]{};
    float parameters[4]{};
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
    float camera_position[4]{};
    float light_metadata[4]{};
    std::array<SceneLightConstants, max_runtime_lights> lights{};
};

static_assert(sizeof(SceneLightConstants) == 48U);
static_assert(sizeof(SceneConstants) == 544U);
''',
)
replace_once(
    "engine/runtime/src/scene.cpp",
    '''math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
''',
    '''std::vector<RuntimeLight> collect_runtime_lights(const world::World& world) {
    std::vector<RuntimeLight> lights;
    lights.reserve(max_runtime_lights);
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot.entities) {
        if (lights.size() >= max_runtime_lights) break;
        if (!entity.spatial_state.has_value() || !entity.light.has_value() || !entity.light->enabled) {
            continue;
        }

        const auto authored = entity.authored_transform.value_or(world::AuthoredTransform{});
        RuntimeLight light{
            .entity = entity.persistent_id,
            .light = *entity.light,
            .position = {
                static_cast<float>(entity.spatial_state->global_position.x),
                static_cast<float>(entity.spatial_state->global_position.y),
                static_cast<float>(entity.spatial_state->global_position.z),
            },
        };
        if (light.light.type == world::LightType::directional) {
            light.direction = math::normalize(
                rotate_vector(authored.rotation, {0.0F, 0.0F, -1.0F})
            );
        }
        lights.push_back(light);
    }
    return lights;
}

math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
''',
)
replace_once(
    "engine/runtime/src/scene.cpp",
    '''        const SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},
            .light_direction = {0.431934F, 0.863868F, 0.259161F, 0.0F},
            .light_color = {4.2F, 4.0F, 3.7F, 1.0F},
        };
        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {
''',
    '''        auto runtime_lights = collect_runtime_lights(world);
        if (runtime_lights.empty()) {
            runtime_lights.push_back(RuntimeLight{
                .light = world::Light{
                    .type = world::LightType::directional,
                    .color = {1.0F, 0.952381F, 0.880952F},
                    .intensity = 4.2F,
                    .range = 0.0F,
                    .enabled = true,
                },
                .direction = {-0.431934F, -0.863868F, -0.259161F},
            });
        }

        SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
            .camera_position = {camera_position.x, camera_position.y, camera_position.z, 1.0F},
        };
        constants.light_metadata[0] = static_cast<float>(runtime_lights.size());
        for (std::size_t light_index = 0; light_index < runtime_lights.size(); ++light_index) {
            const auto& source = runtime_lights[light_index];
            auto& destination = constants.lights[light_index];
            if (source.light.type == world::LightType::directional) {
                destination.position_or_direction[0] = source.direction.x;
                destination.position_or_direction[1] = source.direction.y;
                destination.position_or_direction[2] = source.direction.z;
                destination.position_or_direction[3] = 0.0F;
            } else {
                destination.position_or_direction[0] = source.position.x;
                destination.position_or_direction[1] = source.position.y;
                destination.position_or_direction[2] = source.position.z;
                destination.position_or_direction[3] = 1.0F;
            }
            destination.color_intensity[0] = source.light.color.red;
            destination.color_intensity[1] = source.light.color.green;
            destination.color_intensity[2] = source.light.color.blue;
            destination.color_intensity[3] = source.light.intensity;
            destination.parameters[0] = source.light.range;
        }
        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {
''',
)

write(
    "assets/shaders/vice_project_surface.hlsl",
    r'''static const uint VICE_MAX_LIGHTS = 8;

struct ViceLight {
    float4 position_or_direction;
    float4 color_intensity;
    float4 parameters;
};

cbuffer SceneFrame : register(b0, space0) {
    column_major float4x4 view_projection;
    column_major float4x4 model;
    float4 camera_position;
    float4 light_metadata;
    ViceLight lights[VICE_MAX_LIGHTS];
};

cbuffer MaterialParams : register(b1, space0) {
    float4 base_color;
    float metallic;
    float roughness;
    float2 material_padding;
};

Texture2D<float4> base_color_texture : register(t0, space0);
SamplerState base_color_sampler : register(s0, space0);

struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
    float3 normal : NORMAL0;
    float2 uv : TEXCOORD0;
};

struct VertexOut {
    float4 position : SV_Position;
    float3 world_position : TEXCOORD0;
    float3 world_normal : TEXCOORD1;
    float3 color : COLOR0;
    float2 uv : TEXCOORD2;
};

VertexOut vice_surface_vs(VertexIn input) {
    VertexOut output;
    const float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.world_position = world_position.xyz;
    output.world_normal = normalize(mul((float3x3)model, input.normal));
    output.color = input.color;
    output.uv = input.uv;
    return output;
}

float distribution_ggx(float3 normal, float3 half_vector, float alpha) {
    const float alpha_squared = alpha * alpha;
    const float normal_half = saturate(dot(normal, half_vector));
    const float denominator = normal_half * normal_half * (alpha_squared - 1.0) + 1.0;
    return alpha_squared / max(3.14159265 * denominator * denominator, 0.0001);
}

float geometry_schlick_ggx(float normal_view, float k) {
    return normal_view / max(normal_view * (1.0 - k) + k, 0.0001);
}

float3 fresnel_schlick(float cosine, float3 f0) {
    return f0 + (1.0 - f0) * pow(1.0 - saturate(cosine), 5.0);
}

float3 evaluate_light(
    ViceLight light,
    float3 world_position,
    float3 normal,
    float3 view_direction,
    float3 albedo,
    float metallic_value,
    float roughness_value
) {
    float3 to_light;
    float attenuation = 1.0;
    if (light.position_or_direction.w < 0.5) {
        to_light = normalize(-light.position_or_direction.xyz);
    } else {
        const float3 delta = light.position_or_direction.xyz - world_position;
        const float distance = max(length(delta), 0.0001);
        to_light = delta / distance;
        const float range = max(light.parameters.x, 0.0001);
        const float smooth_range = saturate(1.0 - distance / range);
        attenuation = smooth_range * smooth_range;
    }

    const float normal_light = saturate(dot(normal, to_light));
    if (normal_light <= 0.0 || attenuation <= 0.0) return float3(0.0, 0.0, 0.0);

    const float3 half_vector = normalize(view_direction + to_light);
    const float normal_view = saturate(dot(normal, view_direction));
    const float half_view = saturate(dot(half_vector, view_direction));
    const float clamped_roughness = clamp(roughness_value, 0.04, 1.0);
    const float alpha = clamped_roughness * clamped_roughness;
    const float3 f0 = lerp(float3(0.04, 0.04, 0.04), albedo, saturate(metallic_value));
    const float3 fresnel = fresnel_schlick(half_view, f0);
    const float distribution = distribution_ggx(normal, half_vector, alpha);
    const float geometry_k = ((clamped_roughness + 1.0) * (clamped_roughness + 1.0)) / 8.0;
    const float geometry = geometry_schlick_ggx(normal_view, geometry_k) *
        geometry_schlick_ggx(normal_light, geometry_k);
    const float3 specular = distribution * geometry * fresnel /
        max(4.0 * normal_view * normal_light, 0.0001);
    const float3 kd = (1.0 - fresnel) * (1.0 - saturate(metallic_value));
    const float3 diffuse = kd * albedo / 3.14159265;
    const float3 radiance = light.color_intensity.rgb * light.color_intensity.w * attenuation;
    return (diffuse + specular) * radiance * normal_light;
}

float4 vice_surface_fs(VertexOut input) : SV_Target0 {
    const float4 sampled = base_color_texture.Sample(base_color_sampler, input.uv);
    const float4 albedo_sample = sampled * base_color * float4(input.color, 1.0);
    const float3 albedo = albedo_sample.rgb;
    const float3 normal = normalize(input.world_normal);
    const float3 view_direction = normalize(camera_position.xyz - input.world_position);

    float3 direct = float3(0.0, 0.0, 0.0);
    const uint light_count = min((uint)light_metadata.x, VICE_MAX_LIGHTS);
    for (uint index = 0; index < light_count; ++index) {
        direct += evaluate_light(
            lights[index],
            input.world_position,
            normal,
            view_direction,
            albedo,
            metallic,
            roughness
        );
    }

    const float3 ambient = albedo * 0.035;
    return float4(ambient + direct, albedo_sample.a);
}
''',
)

path = "assets/worlds/vice_lab.vworld"
text = read(path)
text = text.replace(
    "# render mat:   70000000-0000-4000-8000-000000000001\n",
    "# render mat:   70000000-0000-4000-8000-000000000001\n"
    "# sun light:    61000000-0000-4000-8000-000000000001\n"
    "# point light:  61000000-0000-4000-8000-000000000002\n",
    1,
)
anchor = "entity 60000000-0000-4000-8000-000000000001 spatial -4 6 16 1 transform -0.17364818 0 0 0.98480775 1 1 1 camera perspective 60 0.05 500 primary\n"
if text.count(anchor) != 1:
    raise SystemExit("VICE Lab camera anchor missing")
text = text.replace(
    anchor,
    anchor
    + "entity 61000000-0000-4000-8000-000000000001 spatial -4 8 6 1 transform -0.25881905 0 0 0.96592583 1 1 1 light directional 1 0.95 0.85 3 enabled\n"
    + "entity 61000000-0000-4000-8000-000000000002 spatial -6 4 2 1 light point 1 0.55 0.30 6 14 enabled\n",
    1,
)
write(path, text)

write(
    "tests/runtime_light_tests.cpp",
    r'''#include <vice/runtime/scene.hpp>
#include <vice/world/world.hpp>

#include <array>
#include <cassert>
#include <cmath>

namespace {

vice::world::PersistentId id(const std::size_t index) {
    static constexpr std::array<const char*, 10> ids{
        "61000000-0000-4000-8000-000000000001",
        "61000000-0000-4000-8000-000000000002",
        "61000000-0000-4000-8000-000000000003",
        "61000000-0000-4000-8000-000000000004",
        "61000000-0000-4000-8000-000000000005",
        "61000000-0000-4000-8000-000000000006",
        "61000000-0000-4000-8000-000000000007",
        "61000000-0000-4000-8000-000000000008",
        "61000000-0000-4000-8000-000000000009",
        "61000000-0000-4000-8000-000000000010",
    };
    const auto parsed = vice::world::PersistentId::parse(ids.at(index));
    assert(parsed.has_value());
    return *parsed;
}

} // namespace

int main() {
    using namespace vice;

    world::World scene;
    assert(scene.register_region(world::RegionDesc{
        .id = world::RegionId{1U},
        .simulation_island = world::SimulationIslandId{1U},
        .bounds = world::RegionBounds{
            .min = {-50.0F, -50.0F, -50.0F},
            .max = {50.0F, 50.0F, 50.0F},
        },
    }));

    for (std::size_t index = 0; index < 10U; ++index) {
        const auto entity = scene.create_entity(id(index));
        assert(entity.has_value());
        assert(scene.set_spatial_state(*entity, world::SpatialState{
            .global_position = {static_cast<double>(index + 1U), 2.0, 3.0},
            .region = world::RegionId{1U},
        }));
        world::Light light;
        light.type = index == 0U ? world::LightType::directional : world::LightType::point;
        light.range = light.type == world::LightType::point ? 20.0F : 0.0F;
        light.intensity = static_cast<float>(index + 1U);
        assert(scene.set_light(*entity, light));
    }

    auto lights = runtime::collect_runtime_lights(scene);
    assert(lights.size() == runtime::max_runtime_lights);
    assert(lights.front().entity == id(0));
    assert(lights.back().entity == id(7));
    assert(lights.front().light.type == world::LightType::directional);
    assert(std::abs(lights.front().direction.x) < 0.0001F);
    assert(std::abs(lights.front().direction.y) < 0.0001F);
    assert(std::abs(lights.front().direction.z + 1.0F) < 0.0001F);

    const auto first = scene.find(id(0));
    assert(first.has_value());
    assert(scene.set_authored_transform(*first, world::AuthoredTransform{
        .rotation = {0.0F, 1.0F, 0.0F, 0.0F},
    }));
    lights = runtime::collect_runtime_lights(scene);
    assert(std::abs(lights.front().direction.z - 1.0F) < 0.0001F);

    const auto second = scene.find(id(1));
    assert(second.has_value());
    assert(scene.set_spatial_state(*second, world::SpatialState{
        .global_position = {17.0, 4.0, -2.0},
        .region = world::RegionId{1U},
    }));
    lights = runtime::collect_runtime_lights(scene);
    assert(lights[1].position.x == 17.0F);
    assert(lights[1].position.y == 4.0F);
    assert(lights[1].position.z == -2.0F);

    auto disabled = scene.light(*first);
    assert(disabled.has_value());
    disabled->enabled = false;
    assert(scene.set_light(*first, *disabled));
    lights = runtime::collect_runtime_lights(scene);
    assert(lights.front().entity == id(1));
    assert(lights.back().entity == id(8));
    return 0;
}
''',
)
replace_once(
    "tests/runtime/CMakeLists.txt",
    "add_test(NAME vice_runtime_render_material_tests COMMAND vice_runtime_render_material_tests)\n\n",
    "add_test(NAME vice_runtime_render_material_tests COMMAND vice_runtime_render_material_tests)\n\n"
    "add_executable(vice_runtime_light_tests ../runtime_light_tests.cpp)\n"
    "target_link_libraries(vice_runtime_light_tests PRIVATE VICE::Runtime)\n"
    "target_compile_features(vice_runtime_light_tests PRIVATE cxx_std_23)\n"
    "vice_enable_warnings(vice_runtime_light_tests)\n"
    "add_test(NAME vice_runtime_light_tests COMMAND vice_runtime_light_tests)\n\n",
)

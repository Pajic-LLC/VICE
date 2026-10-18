#include <vice/runtime/scene.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vice::runtime {
namespace {

constexpr float runtime_fov = 0.82F;

static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0,
        .shader_location = 0,
        .format = render::VertexFormat::float32x3,
        .offset = 0,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0,
        .shader_location = 1,
        .format = render::VertexFormat::float32x3,
        .offset = 12,
    },
};

static constexpr std::array mesh_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(render::MeshVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1,
        .attributes = mesh_attributes,
    },
};

static constexpr std::array vertex_resources{
    render::ShaderResourceBindingDesc{
        .register_class = render::ShaderRegisterClass::constant_buffer,
        .binding = 0,
        .space = 0,
        .count = 1,
        .unbounded = false,
    },
};

constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0,
    .space = 0,
    .array_element = 0,
};

struct SceneConstants {
    math::Mat4 view_projection{};
    math::Mat4 model{};
};

static_assert(sizeof(SceneConstants) == 128U);

constexpr std::string_view hlsl_source = R"(
cbuffer SceneFrame : register(b0) {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position : POSITION0;
    float3 color : COLOR0;
};
struct VertexOut {
    float4 position : SV_Position;
    float3 color : COLOR0;
};
VertexOut vice_runtime_mesh_vs(VertexIn input) {
    VertexOut output;
    float4 world_position = mul(model, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.color = input.color;
    return output;
}
float4 vice_runtime_mesh_fs(VertexOut input) : SV_Target0 {
    return float4(input.color, 1.0);
}
)";

constexpr std::string_view metal_source = R"(
#include <metal_stdlib>
using namespace metal;
struct SceneFrame {
    float4x4 view_projection;
    float4x4 model;
};
struct VertexIn {
    float3 position [[attribute(11)]];
    float3 color [[attribute(12)]];
};
struct VertexOut {
    float4 position [[position]];
    float3 color;
};
vertex VertexOut vice_runtime_mesh_vs(
    VertexIn input [[stage_in]],
    constant SceneFrame& frame [[buffer(0)]]) {
    VertexOut output;
    float4 world_position = frame.model * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.color = input.color;
    return output;
}
fragment float4 vice_runtime_mesh_fs(VertexOut input [[stage_in]]) {
    return float4(input.color, 1.0);
}
)";

[[nodiscard]] constexpr math::Vec3 add(
    const math::Vec3 left,
    const math::Vec3 right
) noexcept {
    return {left.x + right.x, left.y + right.y, left.z + right.z};
}

[[nodiscard]] constexpr math::Vec3 scaled(
    const math::Vec3 value,
    const float scale
) noexcept {
    return {value.x * scale, value.y * scale, value.z * scale};
}

[[nodiscard]] math::Mat4 scale_matrix(const world::AuthoredScale scale) noexcept {
    math::Mat4 result = math::identity();
    result(0, 0) = scale.x;
    result(1, 1) = scale.y;
    result(2, 2) = scale.z;
    return result;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_buffer_with_data(
    render::Device& device,
    const render::BufferUsage usage,
    const std::span<const std::byte> data,
    const std::string_view debug_name
) {
    if (data.empty()) return nullptr;
    render::BufferDesc desc;
    desc.size = data.size();
    desc.usage = usage;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = debug_name;
    auto buffer = device.create_buffer(desc);
    if (!buffer || !buffer->write(0, data)) return nullptr;
    return buffer;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_constant_buffer(render::Device& device) {
    render::BufferDesc desc;
    desc.size = sizeof(SceneConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime scene constants";
    return device.create_buffer(desc);
}

[[nodiscard]] float mesh_radius(const render::MeshAsset& mesh) noexcept {
    float radius_squared = 0.0F;
    for (const auto& vertex : mesh.vertices) {
        const float x = vertex.position[0];
        const float y = vertex.position[1];
        const float z = vertex.position[2];
        radius_squared = std::max(radius_squared, x * x + y * y + z * z);
    }
    return std::sqrt(radius_squared);
}

[[nodiscard]] math::Mat4 fallback_view_projection(
    const std::span<const RenderMeshInstance> instances,
    const CookedMeshCatalog& catalog,
    const float aspect
) noexcept {
    if (instances.empty()) {
        const math::Vec3 target{};
        const math::Vec3 eye{6.0F, 4.5F, 8.0F};
        return math::multiply(
            math::perspective_rh_zo(runtime_fov, aspect, 0.05F, 250.0F),
            math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
        );
    }

    math::Vec3 minimum{
        static_cast<float>(instances.front().spatial.global_position.x),
        static_cast<float>(instances.front().spatial.global_position.y),
        static_cast<float>(instances.front().spatial.global_position.z),
    };
    math::Vec3 maximum = minimum;

    for (const auto& instance : instances) {
        const math::Vec3 position{
            static_cast<float>(instance.spatial.global_position.x),
            static_cast<float>(instance.spatial.global_position.y),
            static_cast<float>(instance.spatial.global_position.z),
        };
        float radius = 0.5F;
        if (const auto* record = catalog.find(instance.mesh); record != nullptr) {
            const float scale = std::max({
                std::abs(instance.authored.scale.x),
                std::abs(instance.authored.scale.y),
                std::abs(instance.authored.scale.z),
            });
            radius = std::max(radius, mesh_radius(record->asset) * scale);
        }
        minimum.x = std::min(minimum.x, position.x - radius);
        minimum.y = std::min(minimum.y, position.y - radius);
        minimum.z = std::min(minimum.z, position.z - radius);
        maximum.x = std::max(maximum.x, position.x + radius);
        maximum.y = std::max(maximum.y, position.y + radius);
        maximum.z = std::max(maximum.z, position.z + radius);
    }

    const math::Vec3 target{
        (minimum.x + maximum.x) * 0.5F,
        (minimum.y + maximum.y) * 0.5F,
        (minimum.z + maximum.z) * 0.5F,
    };
    const math::Vec3 extent{
        maximum.x - minimum.x,
        maximum.y - minimum.y,
        maximum.z - minimum.z,
    };
    const float radius = std::max(1.0F, 0.5F * std::sqrt(math::dot(extent, extent)));
    const float distance = std::max(5.0F, radius / std::tan(runtime_fov * 0.5F) * 1.35F);
    const math::Vec3 direction = math::normalize({1.0F, 0.7F, 1.0F});
    const math::Vec3 eye = add(target, scaled(direction, distance));
    const float far_plane = std::max(250.0F, distance + radius * 6.0F);

    return math::multiply(
        math::perspective_rh_zo(runtime_fov, aspect, 0.05F, far_plane),
        math::look_at_rh(eye, target, {0.0F, 1.0F, 0.0F})
    );
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

bool CookedMeshCatalog::load(const std::filesystem::path& content_root) {
    records_.clear();
    diagnostics_.clear();

    std::error_code error;
    if (content_root.empty() || !std::filesystem::is_directory(content_root, error)) {
        diagnostics_ = error
            ? "Could not inspect runtime content root: " + error.message()
            : "Runtime content root is not available.";
        return false;
    }

    std::unordered_set<world::ResourceId, world::ResourceIdHash> seen;
    std::filesystem::recursive_directory_iterator iterator{
        content_root,
        std::filesystem::directory_options::skip_permission_denied,
        error,
    };
    const std::filesystem::recursive_directory_iterator end;
    if (error) {
        diagnostics_ = "Could not enumerate runtime content: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate runtime content: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmeshc") continue;

        const auto loaded = render::load_mesh_asset(iterator->path());
        if (!loaded.asset.has_value()) {
            diagnostics_ = "Could not load cooked runtime mesh `" +
                iterator->path().generic_string() + "`: " + loaded.diagnostics;
            return false;
        }
        if (!render::valid(loaded.asset->stable_id)) {
            diagnostics_ = "Cooked runtime mesh has no stable ID: " +
                iterator->path().generic_string();
            return false;
        }

        const world::ResourceId resource{world::PersistentId{loaded.asset->stable_id}};
        if (!seen.insert(resource).second) {
            diagnostics_ = "Duplicate cooked runtime mesh stable ID `" +
                resource.value.to_string() + "`.";
            return false;
        }
        records_.push_back(CookedMeshRecord{
            .resource_id = resource,
            .path = iterator->path(),
            .asset = std::move(*loaded.asset),
        });
    }

    return true;
}

const CookedMeshRecord* CookedMeshCatalog::find(const world::ResourceId resource_id) const noexcept {
    const auto iterator = std::find_if(
        records_.begin(),
        records_.end(),
        [resource_id](const CookedMeshRecord& record) {
            return record.resource_id == resource_id;
        }
    );
    return iterator == records_.end() ? nullptr : &*iterator;
}

std::span<const CookedMeshRecord> CookedMeshCatalog::records() const noexcept {
    return records_;
}

const std::string& CookedMeshCatalog::diagnostics() const noexcept {
    return diagnostics_;
}

std::vector<RenderMeshInstance> collect_render_mesh_instances(const world::World& world) {
    std::vector<RenderMeshInstance> instances;
    const auto snapshot = world.snapshot();
    for (const auto& entity : snapshot.entities) {
        if (!entity.spatial_state.has_value()) continue;
        for (const auto& reference : entity.resource_references) {
            if (reference.role != world::ResourceRole::render_mesh || !reference.resource.valid()) continue;
            instances.push_back(RenderMeshInstance{
                .entity = entity.persistent_id,
                .mesh = reference.resource,
                .spatial = *entity.spatial_state,
                .authored = entity.authored_transform.value_or(world::AuthoredTransform{}),
            });
        }
    }
    return instances;
}

math::Mat4 runtime_model_matrix(const RenderMeshInstance& instance) noexcept {
    auto result = math::translation({
        static_cast<float>(instance.spatial.global_position.x),
        static_cast<float>(instance.spatial.global_position.y),
        static_cast<float>(instance.spatial.global_position.z),
    });
    result = math::multiply(
        result,
        math::rotation({
            instance.authored.rotation.x,
            instance.authored.rotation.y,
            instance.authored.rotation.z,
            instance.authored.rotation.w,
        })
    );
    return math::multiply(result, scale_matrix(instance.authored.scale));
}

struct RuntimeSceneRenderer::State {
    struct MeshGpu {
        std::unique_ptr<render::Buffer> vertices;
        std::unique_ptr<render::Buffer> indices;
        std::uint32_t index_count = 0;
    };

    render::Device* device = nullptr;
    CookedMeshCatalog catalog;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::Texture> depth;
    std::unordered_map<world::ResourceId, MeshGpu, world::ResourceIdHash> meshes;
    std::unordered_map<world::PersistentId, std::unique_ptr<render::Buffer>, world::PersistentIdHash> constants;
    std::uint32_t depth_width = 0;
    std::uint32_t depth_height = 0;
    bool initialized = false;
};

RuntimeSceneRenderer::RuntimeSceneRenderer() : state_{std::make_unique<State>()} {}
RuntimeSceneRenderer::~RuntimeSceneRenderer() = default;
RuntimeSceneRenderer::RuntimeSceneRenderer(RuntimeSceneRenderer&&) noexcept = default;
RuntimeSceneRenderer& RuntimeSceneRenderer::operator=(RuntimeSceneRenderer&&) noexcept = default;

bool RuntimeSceneRenderer::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer state is unavailable.");
        return false;
    }
    state_->device = &device;
    if (!state_->catalog.load(content_root)) {
        set_diagnostics(diagnostics, state_->catalog.diagnostics());
        return false;
    }

    const bool metal = device.backend() == render::Backend::metal;
    render::ShaderModuleDesc vertex_desc;
    vertex_desc.stage = render::ShaderStage::vertex;
    vertex_desc.language = metal ? render::ShaderSourceLanguage::metal : render::ShaderSourceLanguage::hlsl;
    vertex_desc.source = metal ? metal_source : hlsl_source;
    vertex_desc.resources = vertex_resources;
    vertex_desc.entry_point = "vice_runtime_mesh_vs";
    vertex_desc.debug_name = "VICE Runtime mesh vertex";

    render::ShaderModuleDesc fragment_desc;
    fragment_desc.stage = render::ShaderStage::fragment;
    fragment_desc.language = vertex_desc.language;
    fragment_desc.source = vertex_desc.source;
    fragment_desc.entry_point = "vice_runtime_mesh_fs";
    fragment_desc.debug_name = "VICE Runtime mesh fragment";

    state_->vertex_shader = device.create_shader_module(vertex_desc);
    state_->fragment_shader = device.create_shader_module(fragment_desc);
    if (!state_->vertex_shader || !state_->fragment_shader) {
        set_diagnostics(diagnostics, "VICE Runtime could not create scene shaders.");
        return false;
    }

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = state_->vertex_shader.get();
    pipeline_desc.fragment_shader = state_->fragment_shader.get();
    pipeline_desc.color_format = color_format;
    pipeline_desc.cull_mode = render::CullMode::none;
    pipeline_desc.depth.format = render::TextureFormat::depth32_float;
    pipeline_desc.depth.test_enabled = true;
    pipeline_desc.depth.write_enabled = true;
    pipeline_desc.depth.compare = render::CompareOp::less;
    pipeline_desc.vertex_buffers = mesh_layouts;
    pipeline_desc.debug_name = "VICE Runtime mesh pipeline";
    state_->pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!state_->pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the scene graphics pipeline.");
        return false;
    }

    for (const auto& record : state_->catalog.records()) {
        auto vertices = create_buffer_with_data(
            device,
            render::BufferUsage::vertex,
            std::as_bytes(std::span{record.asset.vertices}),
            "VICE Runtime mesh vertices"
        );
        auto indices = create_buffer_with_data(
            device,
            render::BufferUsage::index,
            std::as_bytes(std::span{record.asset.indices}),
            "VICE Runtime mesh indices"
        );
        if (!vertices || !indices) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not upload cooked mesh `" + record.path.generic_string() + "`."
            );
            return false;
        }
        state_->meshes.emplace(
            record.resource_id,
            State::MeshGpu{
                .vertices = std::move(vertices),
                .indices = std::move(indices),
                .index_count = static_cast<std::uint32_t>(record.asset.indices.size()),
            }
        );
    }

    state_->initialized = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::resize(
    const platform::WindowMetrics metrics,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        state_->depth.reset();
        state_->depth_width = 0;
        state_->depth_height = 0;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->depth && state_->depth_width == metrics.drawable_width &&
        state_->depth_height == metrics.drawable_height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    render::TextureDesc depth_desc;
    depth_desc.width = metrics.drawable_width;
    depth_desc.height = metrics.drawable_height;
    depth_desc.format = render::TextureFormat::depth32_float;
    depth_desc.usage = render::TextureUsage::depth_attachment;
    depth_desc.debug_name = "VICE Runtime scene depth";
    auto depth = state_->device->create_texture(depth_desc);
    if (!depth) {
        set_diagnostics(diagnostics, "VICE Runtime could not create the scene depth buffer.");
        return false;
    }
    state_->depth = std::move(depth);
    state_->depth_width = metrics.drawable_width;
    state_->depth_height = metrics.drawable_height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeSceneRenderer::render(
    const world::World& world,
    render::Swapchain& swapchain,
    const platform::WindowMetrics metrics,
    const render::Color clear_color,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->initialized || state_->device == nullptr || !state_->pipeline) {
        set_diagnostics(diagnostics, "Runtime scene renderer is not initialized.");
        return false;
    }
    if (metrics.drawable_width == 0U || metrics.drawable_height == 0U) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!resize(metrics, diagnostics)) return false;

    const auto instances = collect_render_mesh_instances(world);
    for (const auto& instance : instances) {
        if (!state_->meshes.contains(instance.mesh)) {
            set_diagnostics(
                diagnostics,
                "Runtime World references missing cooked mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
    }

    const float aspect = static_cast<float>(metrics.drawable_width) /
        static_cast<float>(std::max(1U, metrics.drawable_height));
    const math::Mat4 view_projection = fallback_view_projection(instances, state_->catalog, aspect);

    auto commands = state_->device->create_command_list();
    if (!commands) {
        set_diagnostics(diagnostics, "VICE Runtime could not create scene commands.");
        return false;
    }

    render::RenderPassDesc pass;
    pass.clear_color = clear_color;
    pass.depth_attachment = state_->depth.get();
    pass.clear_depth = 1.0F;
    if (!commands->begin_render_pass(swapchain, pass) ||
        !commands->set_viewport(render::Viewport{
            .width = static_cast<float>(metrics.drawable_width),
            .height = static_cast<float>(metrics.drawable_height),
        }) ||
        !commands->set_scissor_rect(render::ScissorRect{
            .width = metrics.drawable_width,
            .height = metrics.drawable_height,
        }) ||
        !commands->set_graphics_pipeline(*state_->pipeline)) {
        set_diagnostics(diagnostics, "VICE Runtime could not begin the scene render pass.");
        return false;
    }

    for (const auto& instance : instances) {
        auto constants_iterator = state_->constants.find(instance.entity);
        if (constants_iterator == state_->constants.end()) {
            auto constants = create_constant_buffer(*state_->device);
            if (!constants) {
                set_diagnostics(diagnostics, "VICE Runtime could not create scene constants.");
                return false;
            }
            constants_iterator = state_->constants.emplace(instance.entity, std::move(constants)).first;
        }

        const SceneConstants constants{
            .view_projection = view_projection,
            .model = runtime_model_matrix(instance),
        };
        if (!constants_iterator->second->write(0, std::as_bytes(std::span{&constants, 1}))) {
            set_diagnostics(diagnostics, "VICE Runtime could not update scene constants.");
            return false;
        }

        auto& mesh = state_->meshes.at(instance.mesh);
        if (!commands->set_vertex_buffer(0U, *mesh.vertices) ||
            !commands->set_index_buffer(*mesh.indices, render::IndexFormat::uint32) ||
            !commands->set_graphics_constant_buffer(
                frame_binding,
                *constants_iterator->second,
                0,
                sizeof(SceneConstants)
            ) ||
            !commands->draw_indexed(mesh.index_count)) {
            set_diagnostics(
                diagnostics,
                "VICE Runtime could not encode mesh `" + instance.mesh.value.to_string() + "`."
            );
            return false;
        }
    }

    if (!commands->end_render_pass() || !commands->present(swapchain)) {
        set_diagnostics(diagnostics, "VICE Runtime could not finish the scene render pass.");
        return false;
    }
    const auto submission = state_->device->submit(std::move(commands));
    if (submission == render::invalid_submission || !state_->device->wait_for_submission(submission)) {
        set_diagnostics(diagnostics, "VICE Runtime scene submission failed.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

} // namespace vice::runtime

#include "builtin_shader_paths.hpp"
#include <vice/runtime/reflection_surface_guide.hpp>

#include <vice/render/mesh_asset.hpp>
#include <vice/render/shader_asset.hpp>
#include <vice/runtime/gpu_instance_materials.hpp>
#include <vice/runtime/gpu_material_table.hpp>

#include "gpu_material_scene.hpp"
#include "reflection_surface_guide_bridge.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace vice::runtime {
namespace {

constexpr render::ShaderBindingKey frame_binding{
    .register_class = render::ShaderRegisterClass::constant_buffer,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey model_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 0U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey visible_instance_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 1U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey instance_material_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 2U,
    .space = 0U,
    .array_element = 0U,
};
constexpr render::ShaderBindingKey material_record_binding{
    .register_class = render::ShaderRegisterClass::shader_resource,
    .binding = 3U,
    .space = 0U,
    .array_element = 0U,
};

[[nodiscard]] constexpr render::ShaderBindingKey material_texture_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::shader_resource,
        .binding = 0U,
        .space = 1U,
        .array_element = array_element,
    };
}

[[nodiscard]] constexpr render::ShaderBindingKey material_sampler_binding(
    const std::uint32_t array_element
) noexcept {
    return render::ShaderBindingKey{
        .register_class = render::ShaderRegisterClass::sampler,
        .binding = 0U,
        .space = 1U,
        .array_element = array_element,
    };
}

static constexpr std::array mesh_attributes{
    render::VertexAttributeDesc{
        .semantic = "POSITION",
        .semantic_index = 0U,
        .shader_location = 0U,
        .format = render::VertexFormat::float32x3,
        .offset = 0U,
    },
    render::VertexAttributeDesc{
        .semantic = "COLOR",
        .semantic_index = 0U,
        .shader_location = 1U,
        .format = render::VertexFormat::float32x3,
        .offset = 12U,
    },
    render::VertexAttributeDesc{
        .semantic = "NORMAL",
        .semantic_index = 0U,
        .shader_location = 2U,
        .format = render::VertexFormat::float32x3,
        .offset = 24U,
    },
    render::VertexAttributeDesc{
        .semantic = "TEXCOORD",
        .semantic_index = 0U,
        .shader_location = 3U,
        .format = render::VertexFormat::float32x2,
        .offset = 36U,
    },
};

static constexpr std::array mesh_layouts{
    render::VertexBufferLayoutDesc{
        .stride = sizeof(render::MeshVertex),
        .step_mode = render::VertexStepMode::per_vertex,
        .step_rate = 1U,
        .attributes = mesh_attributes,
    },
};

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

[[nodiscard]] bool finite_vec3(const math::Vec3 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float sign_not_zero(const float value) noexcept {
    return value >= 0.0F ? 1.0F : -1.0F;
}

} // namespace

RuntimeReflectionSurfaceGuideSample encode_runtime_reflection_surface_guide(
    const math::Vec3 world_normal,
    const float roughness,
    const float metallic
) noexcept {
    RuntimeReflectionSurfaceGuideSample result{};
    if (!finite_vec3(world_normal) || math::dot(world_normal, world_normal) <= 1.0e-8F ||
        !std::isfinite(roughness) || !std::isfinite(metallic)) {
        return result;
    }

    const auto normal = math::normalize(world_normal);
    const float denominator = std::abs(normal.x) + std::abs(normal.y) + std::abs(normal.z);
    if (!std::isfinite(denominator) || denominator <= 1.0e-8F) return result;

    float x = normal.x / denominator;
    float y = normal.y / denominator;
    if (normal.z < 0.0F) {
        const float old_x = x;
        x = (1.0F - std::abs(y)) * sign_not_zero(old_x);
        y = (1.0F - std::abs(old_x)) * sign_not_zero(y);
    }
    result.normal_oct[0] = x * 0.5F + 0.5F;
    result.normal_oct[1] = y * 0.5F + 0.5F;
    result.roughness = std::clamp(roughness, 0.0F, 1.0F);
    result.metallic = std::clamp(metallic, 0.0F, 1.0F);
    return result;
}

math::Vec3 decode_runtime_reflection_surface_guide_normal(
    const RuntimeReflectionSurfaceGuideSample& sample
) noexcept {
    if (!std::isfinite(sample.normal_oct[0]) || !std::isfinite(sample.normal_oct[1])) {
        return math::Vec3{0.0F, 0.0F, 1.0F};
    }

    float x = std::clamp(sample.normal_oct[0], 0.0F, 1.0F) * 2.0F - 1.0F;
    float y = std::clamp(sample.normal_oct[1], 0.0F, 1.0F) * 2.0F - 1.0F;
    float z = 1.0F - std::abs(x) - std::abs(y);
    if (z < 0.0F) {
        const float old_x = x;
        x = (1.0F - std::abs(y)) * sign_not_zero(old_x);
        y = (1.0F - std::abs(old_x)) * sign_not_zero(y);
    }
    const math::Vec3 decoded{x, y, z};
    if (math::dot(decoded, decoded) <= 1.0e-8F) {
        return math::Vec3{0.0F, 0.0F, 1.0F};
    }
    return math::normalize(decoded);
}

struct RuntimeReflectionSurfaceGuidePass::State {
    render::Device* device = nullptr;
    std::unique_ptr<render::ShaderModule> vertex_shader;
    std::unique_ptr<render::ShaderModule> fragment_shader;
    std::unique_ptr<render::GraphicsPipeline> pipeline;
    std::unique_ptr<render::Texture> texture;
    std::uint32_t width = 0U;
    std::uint32_t height = 0U;
    bool available = false;
};

RuntimeReflectionSurfaceGuidePass::RuntimeReflectionSurfaceGuidePass()
    : state_{std::make_unique<State>()} {}
RuntimeReflectionSurfaceGuidePass::~RuntimeReflectionSurfaceGuidePass() = default;
RuntimeReflectionSurfaceGuidePass::RuntimeReflectionSurfaceGuidePass(
    RuntimeReflectionSurfaceGuidePass&&
) noexcept = default;
RuntimeReflectionSurfaceGuidePass& RuntimeReflectionSurfaceGuidePass::operator=(
    RuntimeReflectionSurfaceGuidePass&&
) noexcept = default;

bool RuntimeReflectionSurfaceGuidePass::initialize(
    render::Device& device,
    const std::filesystem::path& vertex_shader_manifest,
    const std::filesystem::path& fragment_shader_manifest,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide state is unavailable.");
        return false;
    }

    state_->device = &device;
    state_->vertex_shader.reset();
    state_->fragment_shader.reset();
    state_->pipeline.reset();
    state_->texture.reset();
    state_->width = 0U;
    state_->height = 0U;
    state_->available = false;

    if (!device.capabilities().ray_tracing || vertex_shader_manifest.empty() ||
        fragment_shader_manifest.empty()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    auto vertex = render::load_cooked_shader(device, vertex_shader_manifest);
    auto fragment = render::load_cooked_shader(device, fragment_shader_manifest);
    if (!vertex || !fragment || vertex.module == nullptr || fragment.module == nullptr ||
        vertex.module->stage() != render::ShaderStage::vertex ||
        fragment.module->stage() != render::ShaderStage::fragment) {
        set_diagnostics(diagnostics, "VICE Runtime could not load reflection surface-guide shaders.");
        return false;
    }

    render::GraphicsPipelineDesc pipeline_desc;
    pipeline_desc.vertex_shader = vertex.module.get();
    pipeline_desc.fragment_shader = fragment.module.get();
    pipeline_desc.color_format = render::TextureFormat::rgba16_float;
    pipeline_desc.topology = render::PrimitiveTopology::triangle_list;
    pipeline_desc.cull_mode = render::CullMode::back;
    pipeline_desc.front_face = render::FrontFace::counter_clockwise;
    pipeline_desc.blend = render::BlendMode::opaque;
    pipeline_desc.depth = render::DepthStateDesc{
        .format = render::TextureFormat::depth32_float,
        .test_enabled = true,
        .write_enabled = false,
        .compare = render::CompareOp::equal,
    };
    pipeline_desc.vertex_buffers = mesh_layouts;
    pipeline_desc.debug_name = "VICE Runtime reflection surface guide";

    auto pipeline = device.create_graphics_pipeline(pipeline_desc);
    if (!pipeline) {
        set_diagnostics(diagnostics, "VICE Runtime could not create reflection surface-guide pipeline.");
        return false;
    }

    state_->vertex_shader = std::move(vertex.module);
    state_->fragment_shader = std::move(fragment.module);
    state_->pipeline = std::move(pipeline);
    state_->available = true;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeReflectionSurfaceGuidePass::resize(
    const std::uint32_t width,
    const std::uint32_t height,
    std::string* const diagnostics
) {
    if (state_ == nullptr || state_->device == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide pass is not initialized.");
        return false;
    }
    if (!state_->available) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (width == 0U || height == 0U) {
        state_->texture.reset();
        state_->width = 0U;
        state_->height = 0U;
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (state_->texture != nullptr && state_->width == width && state_->height == height) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    render::TextureDesc desc;
    desc.width = width;
    desc.height = height;
    desc.format = render::TextureFormat::rgba16_float;
    desc.usage = render::TextureUsage::sampled | render::TextureUsage::color_attachment;
    desc.debug_name = "VICE Runtime reflection surface guide";
    auto texture = state_->device->create_texture(desc);
    if (!texture) {
        set_diagnostics(diagnostics, "VICE Runtime could not allocate reflection surface guide.");
        return false;
    }

    state_->texture = std::move(texture);
    state_->width = width;
    state_->height = height;
    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeReflectionSurfaceGuidePass::encode(
    render::CommandList& commands,
    const RuntimeReflectionSurfaceGuideFrame& frame,
    std::string* const diagnostics
) {
    if (state_ == nullptr || !state_->available || state_->pipeline == nullptr ||
        state_->texture == nullptr || frame.depth == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide frame is incomplete.");
        return false;
    }
    if (frame.depth->format() != render::TextureFormat::depth32_float ||
        frame.depth->width() != state_->width || frame.depth->height() != state_->height ||
        state_->texture->width() != state_->width || state_->texture->height() != state_->height) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide depth source is incompatible.");
        return false;
    }
    if (!frame.draws.empty() &&
        (frame.frame_constants == nullptr || frame.models == nullptr ||
         frame.instance_materials == nullptr || frame.material_records == nullptr ||
         frame.frame_constants_size == 0U || frame.model_count == 0U ||
         frame.instance_material_count == 0U || frame.material_count == 0U)) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide draw resources are incomplete.");
        return false;
    }

    render::RenderPassDesc render_pass;
    render_pass.clear_color = runtime_reflection_surface_guide_neutral();
    render_pass.depth_attachment = frame.depth;
    render_pass.clear_color_attachment = true;
    render_pass.clear_depth_attachment = false;
    if (!commands.begin_render_pass(*state_->texture, render_pass)) {
        set_diagnostics(diagnostics, "VICE Runtime could not begin reflection surface-guide pass.");
        return false;
    }

    if (frame.draws.empty()) {
        if (!commands.end_render_pass()) {
            set_diagnostics(diagnostics, "VICE Runtime could not clear reflection surface guide.");
            return false;
        }
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    const render::BufferViewDesc model_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(frame.model_count) * sizeof(math::Mat4),
        .structure_stride = sizeof(math::Mat4),
    };
    const render::BufferViewDesc instance_material_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(frame.instance_material_count) *
            sizeof(RuntimeGpuInstanceMaterialRecord),
        .structure_stride = sizeof(RuntimeGpuInstanceMaterialRecord),
    };
    const render::BufferViewDesc material_view{
        .offset = 0U,
        .size = static_cast<std::uint64_t>(frame.material_count) * sizeof(RuntimeGpuMaterialRecord),
        .structure_stride = sizeof(RuntimeGpuMaterialRecord),
    };

    bool encoded = commands.set_viewport(render::Viewport{
        .x = 0.0F,
        .y = 0.0F,
        .width = static_cast<float>(state_->width),
        .height = static_cast<float>(state_->height),
        .min_depth = 0.0F,
        .max_depth = 1.0F,
    }) && commands.set_scissor_rect(render::ScissorRect{
        .x = 0,
        .y = 0,
        .width = state_->width,
        .height = state_->height,
    }) && commands.set_graphics_pipeline(*state_->pipeline) &&
        commands.set_graphics_constant_buffer(
            frame_binding,
            *frame.frame_constants,
            0U,
            frame.frame_constants_size
        ) && commands.set_graphics_buffer(model_binding, *frame.models, model_view) &&
        commands.set_graphics_buffer(
            instance_material_binding,
            *frame.instance_materials,
            instance_material_view
        ) && commands.set_graphics_buffer(
            material_record_binding,
            *frame.material_records,
            material_view
        );

    for (std::size_t index = 0U; index < frame.material_textures.size() && encoded; ++index) {
        encoded = frame.material_textures[index] != nullptr &&
            commands.set_graphics_texture(
                material_texture_binding(static_cast<std::uint32_t>(index)),
                *frame.material_textures[index]
            );
    }
    for (std::size_t index = 0U; index < frame.material_samplers.size() && encoded; ++index) {
        encoded = frame.material_samplers[index] != nullptr &&
            commands.set_graphics_sampler(
                material_sampler_binding(static_cast<std::uint32_t>(index)),
                *frame.material_samplers[index]
            );
    }

    render::Buffer* bound_visible = nullptr;
    for (const auto& draw : frame.draws) {
        if (!encoded) break;
        if (draw.vertices == nullptr || draw.indices == nullptr ||
            draw.visible_scene_instances == nullptr || draw.indirect_arguments == nullptr) {
            encoded = false;
            break;
        }
        if (bound_visible != draw.visible_scene_instances) {
            const render::BufferViewDesc visible_view{
                .offset = 0U,
                .size = draw.visible_scene_instances->size(),
                .structure_stride = sizeof(std::uint32_t),
            };
            encoded = commands.set_graphics_buffer(
                visible_instance_binding,
                *draw.visible_scene_instances,
                visible_view
            );
            bound_visible = draw.visible_scene_instances;
        }
        if (encoded) {
            encoded = commands.set_vertex_buffer_resolved(0U, *draw.vertices) &&
                commands.set_index_buffer_resolved(*draw.indices, render::IndexFormat::uint32) &&
                commands.draw_indexed_indirect(
                    *draw.indirect_arguments,
                    draw.indirect_argument_offset
                );
        }
    }

    const bool ended = commands.end_render_pass();
    if (!encoded || !ended) {
        set_diagnostics(diagnostics, "VICE Runtime could not encode reflection surface guide.");
        return false;
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

bool RuntimeReflectionSurfaceGuidePass::available() const noexcept {
    return state_ != nullptr && state_->available;
}

render::Texture* RuntimeReflectionSurfaceGuidePass::texture() const noexcept {
    return state_ == nullptr ? nullptr : state_->texture.get();
}

} // namespace vice::runtime

namespace vice::runtime::detail {
namespace {

struct RuntimeReflectionSurfaceGuideSceneState {
    const RuntimeSceneRenderer* owner = nullptr;
    render::Device* device = nullptr;
    std::unique_ptr<RuntimeReflectionSurfaceGuidePass> pass;
};

thread_local RuntimeReflectionSurfaceGuideSceneState reflection_surface_guide_scene_state;

[[nodiscard]] bool ensure_reflection_surface_guide_pass(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    std::string* const diagnostics
) {
    auto& state = reflection_surface_guide_scene_state;
    if (state.owner == owner && state.device == &device && state.pass != nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }

    state.pass.reset();
    state.owner = owner;
    state.device = &device;
    state.pass = std::make_unique<RuntimeReflectionSurfaceGuidePass>();
#ifdef VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_VERTEX_MANIFEST
#ifdef VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_FRAGMENT_MANIFEST
    return state.pass->initialize(
        device,
        ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_VERTEX_MANIFEST),
        ::vice::runtime::detail::builtin_shader_manifest(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_FRAGMENT_MANIFEST),
        diagnostics
    );
#else
    return state.pass->initialize(device, {}, {}, diagnostics);
#endif
#else
    return state.pass->initialize(device, {}, {}, diagnostics);
#endif
}

} // namespace

bool runtime_reflection_surface_guide_after_motion_vectors(
    const RuntimeSceneRenderer* const owner,
    render::Device& device,
    render::CommandList& commands,
    render::Texture* const current_depth,
    render::Buffer* const frame_constants,
    const std::uint64_t frame_constants_size,
    const RuntimeGpuInstanceTableResult instance_table,
    const RuntimeGpuDrawBatchResult& direct_batches,
    const RuntimeGpuClusterInstanceRemapResult cluster_instances,
    const RuntimeGpuDrawBatchResult& cluster_batches,
    const RuntimeGpuMaterialSceneView material_scene,
    const std::span<const world::ResourceId> direct_meshes,
    const std::span<const world::ResourceId> cluster_meshes,
    const std::function<RuntimeReflectionSurfaceGuideMeshBuffers(world::ResourceId)>& resolve_mesh,
    std::string* const diagnostics
) noexcept {
    if (!device.capabilities().ray_tracing || current_depth == nullptr) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (owner == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide owner is unavailable.");
        return false;
    }
    if (!ensure_reflection_surface_guide_pass(owner, device, diagnostics)) return false;
    auto& state = reflection_surface_guide_scene_state;
    if (state.pass == nullptr || !state.pass->available()) {
        if (diagnostics != nullptr) diagnostics->clear();
        return true;
    }
    if (!state.pass->resize(current_depth->width(), current_depth->height(), diagnostics)) return false;

    if (direct_meshes.empty() && cluster_meshes.empty()) {
        return state.pass->encode(
            commands,
            RuntimeReflectionSurfaceGuideFrame{.depth = current_depth},
            diagnostics
        );
    }

    if (frame_constants == nullptr || instance_table.models == nullptr ||
        material_scene.materials.records == nullptr ||
        material_scene.instance_materials.records == nullptr || material_scene.plan == nullptr) {
        set_diagnostics(diagnostics, "Runtime reflection surface-guide scene resources are unavailable.");
        return false;
    }

    std::vector<RuntimeReflectionSurfaceGuideDraw> draws;
    draws.reserve(direct_meshes.size() + cluster_meshes.size());
    if (!direct_meshes.empty()) {
        if (direct_batches.compacted_instance_indices == nullptr ||
            direct_batches.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "Runtime reflection surface-guide direct batches are unavailable.");
            return false;
        }
        for (std::size_t index = 0U; index < direct_meshes.size(); ++index) {
            const auto mesh = resolve_mesh(direct_meshes[index]);
            draws.push_back(RuntimeReflectionSurfaceGuideDraw{
                .vertices = mesh.vertices,
                .indices = mesh.indices,
                .visible_scene_instances = direct_batches.compacted_instance_indices,
                .indirect_arguments = direct_batches.indirect_arguments,
                .indirect_argument_offset = static_cast<std::uint64_t>(index) *
                    sizeof(render::IndexedIndirectDrawArguments),
            });
        }
    }
    if (!cluster_meshes.empty()) {
        if (cluster_instances.scene_instance_indices == nullptr ||
            cluster_batches.indirect_arguments == nullptr) {
            set_diagnostics(diagnostics, "Runtime reflection surface-guide cluster batches are unavailable.");
            return false;
        }
        for (std::size_t index = 0U; index < cluster_meshes.size(); ++index) {
            const auto mesh = resolve_mesh(cluster_meshes[index]);
            draws.push_back(RuntimeReflectionSurfaceGuideDraw{
                .vertices = mesh.vertices,
                .indices = mesh.indices,
                .visible_scene_instances = cluster_instances.scene_instance_indices,
                .indirect_arguments = cluster_batches.indirect_arguments,
                .indirect_argument_offset = static_cast<std::uint64_t>(index) *
                    sizeof(render::IndexedIndirectDrawArguments),
            });
        }
    }

    const RuntimeReflectionSurfaceGuideFrame frame{
        .depth = current_depth,
        .frame_constants = frame_constants,
        .frame_constants_size = frame_constants_size,
        .models = instance_table.models,
        .model_count = instance_table.instance_count,
        .instance_materials = material_scene.instance_materials.records,
        .instance_material_count = material_scene.instance_materials.instance_count,
        .material_records = material_scene.materials.records,
        .material_count = material_scene.materials.material_count,
        .material_textures = material_scene.resources.textures,
        .material_samplers = material_scene.resources.samplers,
        .draws = draws,
    };
    return state.pass->encode(commands, frame, diagnostics);
}

render::Texture* runtime_reflection_surface_guide_texture(
    const RuntimeSceneRenderer* const owner
) noexcept {
    const auto& state = reflection_surface_guide_scene_state;
    if (owner == nullptr || state.owner != owner || state.pass == nullptr) return nullptr;
    return state.pass->texture();
}

} // namespace vice::runtime::detail

namespace vice::runtime {

render::Texture* RuntimeSceneRenderer::reflection_surface_guide() const noexcept {
    return detail::runtime_reflection_surface_guide_texture(this);
}

} // namespace vice::runtime

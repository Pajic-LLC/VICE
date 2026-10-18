#include "material_pipeline.hpp"

#include <vice/render/material_asset.hpp>
#include <vice/render/shader_asset.hpp>

#include <algorithm>
#include <array>
#include <cstddef>
#include <filesystem>
#include <span>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace vice::runtime {
namespace {

struct MaterialConstants {
    float base_color[4]{};
};

static_assert(sizeof(MaterialConstants) == 16U);

[[nodiscard]] bool valid_material_resources(
    const render::ShaderModule& module,
    const std::uint32_t required_binding
) noexcept {
    bool required_found = false;
    for (const auto& resource : module.resources()) {
        if (resource.register_class != render::ShaderRegisterClass::constant_buffer ||
            resource.space != 0U || (resource.binding != 0U && resource.binding != 1U) ||
            resource.count != 1U || resource.unbounded) {
            return false;
        }
        if (resource.binding == required_binding) required_found = true;
    }
    return required_found;
}

[[nodiscard]] std::unique_ptr<render::Buffer> create_material_constants(
    render::Device& device,
    const render::Color color
) {
    render::BufferDesc desc;
    desc.size = sizeof(MaterialConstants);
    desc.usage = render::BufferUsage::uniform;
    desc.memory = render::MemoryLocation::cpu_to_gpu;
    desc.debug_name = "VICE Runtime material constants";
    auto buffer = device.create_buffer(desc);
    if (!buffer) return nullptr;

    const MaterialConstants constants{{color.red, color.green, color.blue, color.alpha}};
    if (!buffer->write(0, std::as_bytes(std::span{&constants, 1}))) return nullptr;
    return buffer;
}

void set_diagnostics(std::string* const diagnostics, std::string message) {
    if (diagnostics != nullptr) *diagnostics = std::move(message);
}

} // namespace

bool CookedRenderMaterialCatalog::load(const std::filesystem::path& content_root) {
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
        diagnostics_ = "Could not enumerate runtime render materials: " + error.message();
        return false;
    }

    for (; iterator != end; iterator.increment(error)) {
        if (error) {
            diagnostics_ = "Could not enumerate runtime render materials: " + error.message();
            return false;
        }
        if (!iterator->is_regular_file() || iterator->path().extension() != ".vmatc") continue;

        auto loaded = render::load_render_material_asset(iterator->path());
        if (!loaded.asset.has_value()) {
            diagnostics_ = "Could not load cooked runtime render material `" +
                iterator->path().generic_string() + "`: " + loaded.diagnostics;
            return false;
        }
        const world::ResourceId resource{
            world::PersistentId{loaded.asset->stable_id},
        };
        if (!resource.valid()) {
            diagnostics_ = "Cooked runtime render material has no stable ID: " +
                iterator->path().generic_string();
            return false;
        }
        if (!seen.insert(resource).second) {
            diagnostics_ = "Duplicate cooked runtime render material stable ID `" +
                resource.value.to_string() + "`.";
            return false;
        }
        records_.push_back(CookedRenderMaterialRecord{
            .resource_id = resource,
            .path = iterator->path(),
            .asset = std::move(*loaded.asset),
        });
    }
    return true;
}

const CookedRenderMaterialRecord* CookedRenderMaterialCatalog::find(
    const world::ResourceId resource_id
) const noexcept {
    const auto found = std::find_if(
        records_.begin(),
        records_.end(),
        [resource_id](const CookedRenderMaterialRecord& record) {
            return record.resource_id == resource_id;
        }
    );
    return found == records_.end() ? nullptr : &*found;
}

std::span<const CookedRenderMaterialRecord> CookedRenderMaterialCatalog::records() const noexcept {
    return records_;
}

const std::string& CookedRenderMaterialCatalog::diagnostics() const noexcept {
    return diagnostics_;
}

namespace detail {

struct RuntimeMaterialLibrary::State {
    struct MaterialGpu {
        std::unique_ptr<render::ShaderModule> vertex_shader;
        std::unique_ptr<render::ShaderModule> fragment_shader;
        std::unique_ptr<render::GraphicsPipeline> pipeline;
        std::unique_ptr<render::Buffer> constants;
    };

    CookedRenderMaterialCatalog catalog;
    std::unordered_map<world::ResourceId, MaterialGpu, world::ResourceIdHash> materials;
};

RuntimeMaterialLibrary::RuntimeMaterialLibrary() : state_{std::make_unique<State>()} {}
RuntimeMaterialLibrary::~RuntimeMaterialLibrary() = default;
RuntimeMaterialLibrary::RuntimeMaterialLibrary(RuntimeMaterialLibrary&&) noexcept = default;
RuntimeMaterialLibrary& RuntimeMaterialLibrary::operator=(RuntimeMaterialLibrary&&) noexcept = default;

bool RuntimeMaterialLibrary::initialize(
    render::Device& device,
    const render::TextureFormat color_format,
    const std::span<const render::VertexBufferLayoutDesc> vertex_layouts,
    const std::filesystem::path& content_root,
    std::string* const diagnostics
) {
    if (state_ == nullptr) {
        set_diagnostics(diagnostics, "Runtime material library state is unavailable.");
        return false;
    }
    state_->materials.clear();
    if (!state_->catalog.load(content_root)) {
        set_diagnostics(diagnostics, state_->catalog.diagnostics());
        return false;
    }

    for (const auto& record : state_->catalog.records()) {
        const auto vertex_manifest = (
            record.path.parent_path() / record.asset.vertex_shader_manifest
        ).lexically_normal();
        const auto fragment_manifest = (
            record.path.parent_path() / record.asset.fragment_shader_manifest
        ).lexically_normal();
        auto vertex = render::load_cooked_shader(device, vertex_manifest);
        if (!vertex) {
            set_diagnostics(
                diagnostics,
                "Could not load vertex shader for render material `" +
                    record.resource_id.value.to_string() + "`: " + vertex.diagnostics
            );
            return false;
        }
        auto fragment = render::load_cooked_shader(device, fragment_manifest);
        if (!fragment) {
            set_diagnostics(
                diagnostics,
                "Could not load fragment shader for render material `" +
                    record.resource_id.value.to_string() + "`: " + fragment.diagnostics
            );
            return false;
        }
        if (vertex.module->stage() != render::ShaderStage::vertex ||
            fragment.module->stage() != render::ShaderStage::fragment ||
            !valid_material_resources(*vertex.module, 0U) ||
            !valid_material_resources(*fragment.module, 1U)) {
            set_diagnostics(
                diagnostics,
                "Render material `" + record.resource_id.value.to_string() +
                    "` does not satisfy the VICE b0 scene / b1 material shader contract."
            );
            return false;
        }

        render::GraphicsPipelineDesc pipeline_desc;
        pipeline_desc.vertex_shader = vertex.module.get();
        pipeline_desc.fragment_shader = fragment.module.get();
        pipeline_desc.color_format = color_format;
        pipeline_desc.cull_mode = render::CullMode::none;
        pipeline_desc.depth.format = render::TextureFormat::depth32_float;
        pipeline_desc.depth.test_enabled = true;
        pipeline_desc.depth.write_enabled = true;
        pipeline_desc.depth.compare = render::CompareOp::less;
        pipeline_desc.vertex_buffers = vertex_layouts;
        pipeline_desc.debug_name = "VICE Runtime project material pipeline";
        auto pipeline = device.create_graphics_pipeline(pipeline_desc);
        if (!pipeline) {
            set_diagnostics(
                diagnostics,
                "Renderer rejected pipeline for render material `" +
                    record.resource_id.value.to_string() + "`."
            );
            return false;
        }
        auto constants = create_material_constants(device, record.asset.base_color);
        if (!constants) {
            set_diagnostics(
                diagnostics,
                "Could not create constants for render material `" +
                    record.resource_id.value.to_string() + "`."
            );
            return false;
        }

        state_->materials.emplace(
            record.resource_id,
            State::MaterialGpu{
                .vertex_shader = std::move(vertex.module),
                .fragment_shader = std::move(fragment.module),
                .pipeline = std::move(pipeline),
                .constants = std::move(constants),
            }
        );
    }

    if (diagnostics != nullptr) diagnostics->clear();
    return true;
}

RuntimeMaterialBinding RuntimeMaterialLibrary::find(
    const world::ResourceId resource_id
) const noexcept {
    if (state_ == nullptr) return {};
    const auto found = state_->materials.find(resource_id);
    if (found == state_->materials.end()) return {};
    return RuntimeMaterialBinding{
        .pipeline = found->second.pipeline.get(),
        .constants = found->second.constants.get(),
    };
}

const CookedRenderMaterialCatalog& RuntimeMaterialLibrary::catalog() const noexcept {
    return state_->catalog;
}

} // namespace detail
} // namespace vice::runtime

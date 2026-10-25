#pragma once

#include <vice/runtime/gpu_cluster_instance_remap.hpp>
#include <vice/runtime/gpu_draw_batches.hpp>
#include <vice/runtime/gpu_instance_table.hpp>
#include <vice/runtime/ray_reflection_history.hpp>
#include <vice/runtime/ray_reflections.hpp>
#include <vice/runtime/ray_tracing_scene_executor.hpp>
#include <vice/runtime/reflection_surface_guide.hpp>
#include <vice/runtime/scene.hpp>

#include "environment_pipeline.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace vice::runtime::detail {
struct RuntimeGpuMaterialSceneView;
struct RuntimeReflectionSurfaceGuideMeshBuffers { render::Buffer* vertices = nullptr; render::Buffer* indices = nullptr; };
[[nodiscard]] inline bool runtime_reflection_surface_guide_needs_camera_depth_prepass(const render::Device* const device) noexcept {
#if defined(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_VERTEX_MANIFEST) && defined(VICE_RUNTIME_REFLECTION_SURFACE_GUIDE_FRAGMENT_MANIFEST)
    return device != nullptr && device->capabilities().ray_tracing;
#else
    (void)device; return false;
#endif
}
[[nodiscard]] bool runtime_reflection_surface_guide_after_motion_vectors(const RuntimeSceneRenderer*,render::Device&,render::CommandList&,render::Texture*,render::Buffer*,std::uint64_t,RuntimeGpuInstanceTableResult,const RuntimeGpuDrawBatchResult&,RuntimeGpuClusterInstanceRemapResult,const RuntimeGpuDrawBatchResult&,RuntimeGpuMaterialSceneView,std::span<const world::ResourceId>,std::span<const world::ResourceId>,const std::function<RuntimeReflectionSurfaceGuideMeshBuffers(world::ResourceId)>&,std::string* = nullptr) noexcept;
[[nodiscard]] bool runtime_ray_reflections_after_surface_guide(const RuntimeSceneRenderer*,render::Device&,render::CommandList&,render::Texture*,render::Texture*,RuntimeRayTracingSceneExecutor*,const RuntimeCameraView&,float,float,platform::WindowMetrics,RuntimeEnvironmentBinding,std::string* = nullptr) noexcept;
[[nodiscard]] bool runtime_ray_reflection_history_after_trace(const RuntimeSceneRenderer*,render::Device&,render::CommandList&,render::Texture*,render::Buffer*,render::Texture*,render::Texture*,const RuntimeTemporalFrameState&,platform::WindowMetrics,std::string* = nullptr) noexcept;
[[nodiscard]] render::Texture* runtime_reflection_surface_guide_texture(const RuntimeSceneRenderer*) noexcept;
[[nodiscard]] render::Texture* runtime_ray_reflection_radiance(const RuntimeSceneRenderer*) noexcept;
[[nodiscard]] render::Buffer* runtime_ray_reflection_hits(const RuntimeSceneRenderer*) noexcept;
[[nodiscard]] inline bool runtime_ray_reflections_mark_gpu_pbr(const RuntimeSceneRenderer* const owner,render::Buffer* const frame_constants,const std::uint64_t marker_offset,std::string* const diagnostics) noexcept { if(owner==nullptr||frame_constants==nullptr||runtime_ray_reflection_radiance(owner)==nullptr)return true; const float enabled=1.0F; if(frame_constants->write(marker_offset,std::as_bytes(std::span{&enabled,1})))return true; if(diagnostics)*diagnostics="VICE Runtime could not enable ray reflections for GPU PBR."; return false; }
[[nodiscard]] inline bool runtime_ray_reflections_bind_gpu_pbr_texture(render::CommandList& commands,const RuntimeSceneRenderer* const owner,const render::ShaderBindingKey& binding,render::Texture& fallback_texture,std::string* const diagnostics) noexcept { if(owner==nullptr||binding.register_class!=render::ShaderRegisterClass::shader_resource||binding.binding!=17U||binding.space!=2U||binding.array_element!=0U)return true; constexpr render::ShaderBindingKey reflection_binding{.register_class=render::ShaderRegisterClass::shader_resource,.binding=26U,.space=2U,.array_element=0U}; auto* const radiance=runtime_ray_reflection_radiance(owner); render::Texture& bound_texture=radiance!=nullptr?*radiance:fallback_texture; using SetGraphicsTexture=bool(render::CommandList::*)(const render::ShaderBindingKey&,render::Texture&); const auto bind_texture=static_cast<SetGraphicsTexture>(&render::CommandList::set_graphics_texture); if((commands.*bind_texture)(reflection_binding,bound_texture))return true; if(diagnostics)*diagnostics="VICE Runtime could not bind ray-reflection radiance for GPU PBR."; return false; }
} // namespace vice::runtime::detail

#if defined(render)
#ifdef runtime_directional_shadow_requires_depth_prepass
#undef runtime_directional_shadow_requires_depth_prepass
#endif
#define runtime_directional_shadow_requires_depth_prepass(path) ::vice::runtime::runtime_directional_shadow_requires_depth_prepass((path)) || ::vice::runtime::detail::runtime_ray_ambient_occlusion_needs_camera_depth_prepass() || ::vice::runtime::detail::runtime_reflection_surface_guide_needs_camera_depth_prepass(state_->device)
#ifdef encode_motion_vectors
#undef encode_motion_vectors
#endif
#define encode_motion_vectors(...) (encode_motion_vectors(__VA_ARGS__) && ::vice::runtime::detail::runtime_ray_ambient_occlusion_after_motion_vectors(*commands,state_->motion_vectors.get(),current_depth,previous_depth,temporal_frame) && ::vice::runtime::detail::runtime_reflection_surface_guide_after_motion_vectors(this,*state_->device,*commands,current_depth,state_->gpu_pbr_frame.get(),sizeof(GpuPbrSceneConstants),state_->gpu_instances.result(),gpu_pbr_batch_result,state_->gpu_cluster_pbr_instances.result(),gpu_cluster_pbr_batch_result,state_->gpu_material_scene.view(),std::span<const world::ResourceId>{gpu_pbr_plan.batch_meshes},std::span<const world::ResourceId>{gpu_cluster_pbr_plan.batch_meshes},[&](const world::ResourceId resource_id)->::vice::runtime::detail::RuntimeReflectionSurfaceGuideMeshBuffers{const auto found=state_->meshes.find(resource_id);if(found==state_->meshes.end())return{};return{.vertices=found->second.vertices.get(),.indices=found->second.indices.get()};},diagnostics) && ::vice::runtime::detail::runtime_ray_reflections_after_surface_guide(this,*state_->device,*commands,current_depth,::vice::runtime::detail::runtime_reflection_surface_guide_texture(this),&state_->ray_tracing_scene,runtime_camera,jitter.x_ndc,jitter.y_ndc,metrics,state_->environment.binding(),diagnostics) && ::vice::runtime::detail::runtime_ray_reflection_history_after_trace(this,*state_->device,*commands,::vice::runtime::detail::runtime_ray_reflection_radiance(this),::vice::runtime::detail::runtime_ray_reflection_hits(this),previous_scene_color,previous_depth,temporal_frame,metrics,diagnostics) && ::vice::runtime::detail::runtime_ray_reflections_mark_gpu_pbr(this,state_->gpu_pbr_frame.get(),offsetof(GpuPbrSceneConstants,ambient_occlusion_metadata)+sizeof(float),diagnostics))
#ifdef set_graphics_texture
#undef set_graphics_texture
#endif
#define set_graphics_texture(binding,texture) set_graphics_texture((binding),::vice::runtime::detail::runtime_directional_shadow_texture_override((binding),::vice::runtime::detail::runtime_ray_ambient_occlusion_prepare_graphics_texture(*commands,(binding),(texture)))) && ::vice::runtime::detail::runtime_ray_reflections_bind_gpu_pbr_texture(*commands,this,(binding),(texture),diagnostics)
#endif

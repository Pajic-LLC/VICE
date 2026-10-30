#include "../engine/runtime/src/material_pipeline.hpp"
#include <vice/math/math.hpp>
#include <vice/render/material_asset.hpp>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <vector>
#define CHECK(x) do{if(!(x)){std::cerr<<"Material depth line "<<__LINE__<<": " #x<<'\n';return 1;}}while(false)
using namespace vice;
using namespace vice::render;
ShaderBindingKey binding(ShaderRegisterClass c,unsigned n){return {.register_class=c,.binding=n};}
int main(){
 auto device=Device::create();CHECK(device);
 const auto root=std::filesystem::temp_directory_path()/"vice-material-depth";
 std::error_code error;std::filesystem::remove_all(root,error);std::filesystem::create_directories(root,error);CHECK(!error);
 std::filesystem::copy(std::filesystem::path{VICE_DEPTH_TEST_VS}.parent_path(),root/"vertex",std::filesystem::copy_options::recursive,error);CHECK(!error);
 std::filesystem::copy(std::filesystem::path{VICE_DEPTH_TEST_FS}.parent_path(),root/"fragment",std::filesystem::copy_options::recursive,error);CHECK(!error);
 const auto id=*world::PersistentId::parse("70000000-0000-4000-8000-000000009999");
 RenderMaterialAsset asset;asset.version=render_material_asset_format_version_v1;asset.source_fingerprint=1;
 std::copy(id.bytes().begin(),id.bytes().end(),asset.stable_id.begin());asset.base_color={.8F,.4F,.2F,1};
 asset.vertex_shader_manifest="vertex/shader.vice.json";asset.fragment_shader_manifest="fragment/shader.vice.json";
 const auto encoded=encode_render_material_asset(asset);CHECK(encoded);
 {std::ofstream file(root/"surface.vmatc",std::ios::binary);file.write(reinterpret_cast<const char*>(encoded.bytes.data()),static_cast<std::streamsize>(encoded.bytes.size()));CHECK(file);}
 constexpr std::array attrs{VertexAttributeDesc{.semantic="POSITION",.shader_location=0,.format=VertexFormat::float32x3,.offset=0}};
 const std::array layouts{VertexBufferLayoutDesc{.stride=12,.attributes=attrs}};
 runtime::detail::RuntimeMaterialLibrary library;std::string diagnostics;
 CHECK(library.initialize(*device,TextureFormat::rgba16_float,layouts,root,&diagnostics));
 const auto material=library.find({id});CHECK(material.pipeline&&material.depth_pipeline&&material.constants);
 const std::array<float,9> positions{-.9F,-.9F,.3F,.9F,-.9F,.3F,0,.9F,.3F};
 auto vertices=device->create_buffer({.size=sizeof(positions),.usage=BufferUsage::vertex,.memory=MemoryLocation::cpu_to_gpu});CHECK(vertices);
 CHECK(vertices->write(0,std::as_bytes(std::span{positions})));
 std::array<math::Mat4,2> frame{math::identity(),math::identity()};frame[1](0,0)=1.12F;frame[1](1,1)=.91F;frame[1](0,3)=.07F;frame[1](2,3)=.12F;
 auto constants=device->create_buffer({.size=sizeof(frame),.usage=BufferUsage::uniform,.memory=MemoryLocation::cpu_to_gpu});CHECK(constants);CHECK(constants->write(0,std::as_bytes(std::span{frame})));
 auto color=device->create_texture({.width=32,.height=32,.format=TextureFormat::rgba16_float,.usage=TextureUsage::color_attachment|TextureUsage::sampled});CHECK(color);
 auto depth=device->create_texture({.width=32,.height=32,.format=TextureFormat::depth32_float,.usage=TextureUsage::depth_attachment});CHECK(depth);
 constexpr std::array read_resources{ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::shader_resource,.binding=0,.count=1},ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::unordered_access,.binding=0,.count=1}};
 const bool metal=device->backend()==Backend::metal;
 ShaderModuleDesc read_desc{.stage=ShaderStage::compute,.language=metal?ShaderSourceLanguage::metal:ShaderSourceLanguage::hlsl,.source=metal?R"(
#include <metal_stdlib>
using namespace metal;
kernel void read_pixels(texture2d<float,access::read> image [[texture(0)]],device float4* out [[buffer(0)]],uint2 p [[thread_position_in_grid]]){out[p.y*32+p.x]=image.read(p);}
)":R"(
Texture2D<float4> image:register(t0);RWStructuredBuffer<float4> output:register(u0);
[numthreads(8,8,1)]void read_pixels(uint3 p:SV_DispatchThreadID){output[p.y*32+p.x]=image.Load(int3(p.xy,0));}
)",.resources=read_resources,.entry_point="read_pixels"};
 auto read_shader=device->create_shader_module(read_desc);CHECK(read_shader);
 auto read_pipeline=device->create_compute_pipeline({.compute_shader=read_shader.get(),.thread_group_size={8,8,1}});CHECK(read_pipeline);
 constexpr unsigned bytes=32*32*16;
 auto output=device->create_buffer({.size=bytes,.usage=BufferUsage::storage|BufferUsage::copy_source,.memory=MemoryLocation::gpu_only});CHECK(output);
 auto readback=device->create_buffer({.size=bytes,.usage=BufferUsage::copy_destination,.memory=MemoryLocation::gpu_to_cpu});CHECK(readback);
 std::array<std::array<float,4>,1024> reference{},prepassed{};
 auto render=[&](bool prepass,auto &pixels){
  auto commands=device->create_command_list();if(!commands)return false;
  auto bind_frame=[&](){return commands->set_vertex_buffer(0,*vertices)&&commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,0),*constants,0,sizeof(frame));};
  if(prepass && (!commands->begin_depth_render_pass(*depth,1,true)||!commands->set_graphics_pipeline(*material.depth_pipeline)||!bind_frame()||!commands->draw(3)||!commands->end_render_pass()))return false;
  RenderPassDesc pass;pass.clear_color={0,0,0,1};pass.depth_attachment=depth.get();pass.clear_depth_attachment=!prepass;
  if(!commands->begin_render_pass(*color,pass)||!commands->set_graphics_pipeline(*material.pipeline)||!bind_frame()||!commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,1),*material.constants,0,material.constants_size)||!commands->draw(3)||!commands->end_render_pass())return false;
  if(!commands->set_compute_pipeline(*read_pipeline)||!commands->set_compute_texture(binding(ShaderRegisterClass::shader_resource,0),*color)||!commands->set_compute_storage_buffer(binding(ShaderRegisterClass::unordered_access,0),*output,{.size=bytes,.structure_stride=16})||!commands->dispatch({4,4,1})||!commands->copy_buffer(*output,0,*readback,0,bytes))return false;
  const auto submission=device->submit(std::move(commands));return submission!=invalid_submission&&device->wait_for_submission(submission)&&readback->read(0,std::as_writable_bytes(std::span{pixels}));
 };
 CHECK(render(false,reference));CHECK(render(true,prepassed));
 CHECK(reference==prepassed);unsigned visible=0;for(const auto &p:reference)if(p[0]>.7F)++visible;CHECK(visible>150);
 CHECK(device->wait_idle());std::filesystem::remove_all(root,error);return 0;
}

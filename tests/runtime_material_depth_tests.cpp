#include "../engine/runtime/src/material_pipeline.hpp"
#include <vice/math/math.hpp>
#include <vice/render/material_asset.hpp>
#include <vice/render/shader_asset.hpp>
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
 frame[0](0,0)=.83F;frame[0](1,1)=.91F;frame[0](2,2)=.17F;frame[0](2,3)=.02F;frame[0](3,0)=.17F;frame[0](3,1)=.09F;frame[0](3,2)=.21F;
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
 auto render=[&](bool prepass,auto &pixels,unsigned repeats=1,Buffer *replacement=nullptr,Buffer *early_constants=nullptr,bool fresh_depth=false){
  auto commands=device->create_command_list();if(!commands)return false;
  auto bind_frame=[&](){return commands->set_vertex_buffer(0,*vertices)&&commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,0),*constants,0,sizeof(frame));};
  if(prepass && (!commands->begin_depth_render_pass(*depth,1,true)||!commands->set_graphics_pipeline(*material.depth_pipeline)||!bind_frame()||(early_constants&&!commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,0),*early_constants,0,sizeof(frame)))||!commands->draw(3)||!commands->end_render_pass()))return false;
  RenderPassDesc pass;pass.clear_color={0,0,0,1};pass.depth_attachment=depth.get();pass.clear_depth_attachment=!prepass||fresh_depth;
  if(!commands->begin_render_pass(*color,pass)||!commands->set_graphics_pipeline(*material.pipeline)||!bind_frame()||!commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,1),*material.constants,0,material.constants_size) )return false;
  if(replacement && !commands->set_scissor_rect({0,0,16,32}))return false;
  for(unsigned repeat=0;repeat<repeats;++repeat){
   if(replacement && !commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,1),*material.constants,0,material.constants_size))return false;
   if(!commands->draw(3))return false;
  }
  if(replacement && !commands->set_scissor_rect({16,0,16,32}))return false;
  if(replacement && (!commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,1),*replacement,0,16)||!commands->draw(3)))return false;
  if(!commands->end_render_pass())return false;
  if(!commands->set_compute_pipeline(*read_pipeline)||!commands->set_compute_texture(binding(ShaderRegisterClass::shader_resource,0),*color)||!commands->set_compute_storage_buffer(binding(ShaderRegisterClass::unordered_access,0),*output,{.size=bytes,.structure_stride=16})||!commands->dispatch({4,4,1})||!commands->copy_buffer(*output,0,*readback,0,bytes))return false;
  const auto submission=device->submit(std::move(commands));return submission!=invalid_submission&&device->wait_for_submission(submission)&&readback->read(0,std::as_writable_bytes(std::span{pixels}));
 };
 CHECK(render(false,reference));CHECK(render(true,prepassed));
 // Ray consumers may use early depth, but forward visibility must not inherit
 // a slightly nearer independently compiled/custom vertex depth representation.
 auto early_frame=frame;early_frame[1](2,3)-=.2F;
 auto early_constants=device->create_buffer({.size=sizeof(early_frame),.usage=BufferUsage::uniform,.memory=MemoryLocation::cpu_to_gpu});CHECK(early_constants);CHECK(early_constants->write(0,std::as_bytes(std::span{early_frame})));
 decltype(reference) stale_depth{},fresh_depth{};
 CHECK(render(true,stale_depth,1,nullptr,early_constants.get()));CHECK(stale_depth!=reference);
 CHECK(render(true,fresh_depth,1,nullptr,early_constants.get(),true));CHECK(fresh_depth==reference);
 CHECK(reference==prepassed);unsigned visible=0;for(const auto &p:reference)if(p[0]>.7F)++visible;CHECK(visible>150);
 // Identical bindings may be reused across draws, but a resource replacement
 // must invalidate converted argument buffers before the following draw.
 decltype(reference) repeated{},rebound{};
 CHECK(render(false,repeated,1024));CHECK(repeated==reference);
 const std::array<float,4> blue{.1F,.2F,.9F,1.F};
 auto replacement=device->create_buffer({.size=sizeof(blue),.usage=BufferUsage::uniform,.memory=MemoryLocation::cpu_to_gpu});CHECK(replacement);CHECK(replacement->write(0,std::as_bytes(std::span{blue})));
 CHECK(render(false,rebound,4096,replacement.get()));
 for(unsigned y=0;y<32;++y)for(unsigned x=0;x<16;++x)CHECK(rebound[y*32+x]==reference[y*32+x]);
 unsigned changed=0;for(const auto &pixel:rebound)if(pixel[2]>.85F&&pixel[0]<.15F)++changed;
 unsigned expected_changed=0;for(unsigned y=0;y<32;++y)for(unsigned x=16;x<32;++x)if(reference[y*32+x][0]>.7F)++expected_changed;
 CHECK(changed==expected_changed);CHECK(changed>80);
 // Exercise the shipped GPU PBR vertex shader with the same non-identity
 // column-major model that the direct material reads from a constant buffer.
 auto model_vs=load_cooked_shader(*device,VICE_MODEL_TEST_VS);CHECK(model_vs);
 auto model_fs=load_cooked_shader(*device,VICE_DEPTH_TEST_FS);CHECK(model_fs);
 constexpr std::array model_attrs{
  VertexAttributeDesc{.semantic="POSITION",.shader_location=0,.format=VertexFormat::float32x3,.offset=0},
  VertexAttributeDesc{.semantic="COLOR",.shader_location=1,.format=VertexFormat::float32x3,.offset=12},
  VertexAttributeDesc{.semantic="NORMAL",.shader_location=2,.format=VertexFormat::float32x3,.offset=24},
  VertexAttributeDesc{.semantic="TEXCOORD",.shader_location=3,.format=VertexFormat::float32x2,.offset=36}};
 const std::array model_layouts{VertexBufferLayoutDesc{.stride=44,.attributes=model_attrs}};
 GraphicsPipelineDesc model_desc;model_desc.vertex_shader=model_vs.module.get();model_desc.fragment_shader=model_fs.module.get();model_desc.vertex_buffers=model_layouts;model_desc.color_format=TextureFormat::rgba16_float;model_desc.cull_mode=CullMode::none;
 model_desc.depth={.format=TextureFormat::depth32_float,.test_enabled=true,.write_enabled=true,.compare=CompareOp::less_equal};
 auto model_pipeline=device->create_graphics_pipeline(model_desc);CHECK(model_pipeline);
 model_desc.fragment_shader=nullptr;model_desc.color_format=TextureFormat::undefined;
 auto model_depth_pipeline=device->create_graphics_pipeline(model_desc);CHECK(model_depth_pipeline);
 std::array<float,33> model_vertices{};
 for(unsigned i=0;i<3;++i){for(unsigned j=0;j<3;++j)model_vertices[i*11+j]=positions[i*3+j];for(unsigned j=3;j<6;++j)model_vertices[i*11+j]=1;model_vertices[i*11+8]=1;}
 auto model_stream=device->create_buffer({.size=sizeof(model_vertices),.usage=BufferUsage::vertex,.memory=MemoryLocation::cpu_to_gpu});CHECK(model_stream);CHECK(model_stream->write(0,std::as_bytes(std::span{model_vertices})));
 const std::array<math::Mat4,2> model_pair{frame[1],math::translation({100.F,0.F,0.F})};
 auto models=device->create_buffer({.size=sizeof(model_pair),.usage=BufferUsage::storage|BufferUsage::copy_destination,.memory=MemoryLocation::gpu_only});CHECK(models);
 auto models_upload=device->create_buffer({.size=sizeof(model_pair),.usage=BufferUsage::copy_source,.memory=MemoryLocation::cpu_to_gpu});CHECK(models_upload);CHECK(models_upload->write(0,std::as_bytes(std::span{model_pair})));
 const std::array<unsigned,2> instance_index{1,0};
 auto indices=device->create_buffer({.size=sizeof(instance_index),.usage=BufferUsage::storage|BufferUsage::copy_destination,.memory=MemoryLocation::gpu_only});CHECK(indices);
 auto indices_upload=device->create_buffer({.size=sizeof(instance_index),.usage=BufferUsage::copy_source,.memory=MemoryLocation::cpu_to_gpu});CHECK(indices_upload);CHECK(indices_upload->write(0,std::as_bytes(std::span{instance_index})));
 std::array<math::Mat4,64> model_frame{};model_frame[0]=frame[0];
 auto model_constants=device->create_buffer({.size=sizeof(model_frame),.usage=BufferUsage::uniform,.memory=MemoryLocation::cpu_to_gpu});CHECK(model_constants);CHECK(model_constants->write(0,std::as_bytes(std::span{model_frame})));
 auto render_gpu_model=[&](bool prepass)->int {
 auto model_commands=device->create_command_list();CHECK(model_commands);
 CHECK(model_commands->copy_buffer(*models_upload,0,*models,0,sizeof(model_pair)));CHECK(model_commands->copy_buffer(*indices_upload,0,*indices,0,sizeof(instance_index)));
 if(prepass) {
  CHECK(model_commands->begin_depth_render_pass(*depth,1,true));CHECK(model_commands->set_graphics_pipeline(*model_depth_pipeline));CHECK(model_commands->set_vertex_buffer(0,*model_stream));
  CHECK(model_commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,0),*model_constants,0,sizeof(model_frame)));
  CHECK(model_commands->set_graphics_buffer(binding(ShaderRegisterClass::shader_resource,0),*models,{.size=128,.structure_stride=64}));
  CHECK(model_commands->set_graphics_buffer(binding(ShaderRegisterClass::shader_resource,1),*indices,{.offset=4,.size=4,.structure_stride=4}));
  CHECK(model_commands->draw(3));CHECK(model_commands->end_render_pass());
 }
 RenderPassDesc model_pass;model_pass.clear_color={0,0,0,1};model_pass.depth_attachment=depth.get();model_pass.clear_depth_attachment=!prepass;
 CHECK(model_commands->begin_render_pass(*color,model_pass));CHECK(model_commands->set_graphics_pipeline(*model_pipeline));CHECK(model_commands->set_vertex_buffer(0,*model_stream));
 CHECK(model_commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,0),*model_constants,0,sizeof(model_frame)));
 CHECK(model_commands->set_graphics_constant_buffer(binding(ShaderRegisterClass::constant_buffer,1),*material.constants,0,material.constants_size));
 CHECK(model_commands->set_graphics_buffer(binding(ShaderRegisterClass::shader_resource,0),*models,{.size=128,.structure_stride=64}));
 CHECK(model_commands->set_graphics_buffer(binding(ShaderRegisterClass::shader_resource,1),*indices,{.size=8,.structure_stride=4}));
 CHECK(model_commands->draw(3));
 CHECK(model_commands->set_graphics_buffer(binding(ShaderRegisterClass::shader_resource,1),*indices,{.offset=4,.size=4,.structure_stride=4}));
 CHECK(model_commands->draw(3));CHECK(model_commands->end_render_pass());
 CHECK(model_commands->set_compute_pipeline(*read_pipeline));CHECK(model_commands->set_compute_texture(binding(ShaderRegisterClass::shader_resource,0),*color));CHECK(model_commands->set_compute_storage_buffer(binding(ShaderRegisterClass::unordered_access,0),*output,{.size=bytes,.structure_stride=16}));CHECK(model_commands->dispatch({4,4,1}));CHECK(model_commands->copy_buffer(*output,0,*readback,0,bytes));
 const auto model_submission=device->submit(std::move(model_commands));CHECK(model_submission!=invalid_submission);CHECK(device->wait_for_submission(model_submission));
 decltype(reference) model_pixels{};CHECK(readback->read(0,std::as_writable_bytes(std::span{model_pixels})));if(model_pixels!=reference){
  for(const auto *image:{&reference,&model_pixels}){unsigned count=0;int xmin=32,ymin=32,xmax=-1,ymax=-1;for(int y=0;y<32;++y)for(int x=0;x<32;++x)if((*image)[y*32+x][0]>.7F){++count;xmin=std::min(xmin,x);xmax=std::max(xmax,x);ymin=std::min(ymin,y);ymax=std::max(ymax,y);}std::cerr<<"MODEL_PIXELS "<<count<<" bounds "<<xmin<<' '<<ymin<<' '<<xmax<<' '<<ymax<<'\n';}
 }CHECK(model_pixels==reference);
 return 0;
 };
 CHECK(render_gpu_model(false)==0);CHECK(render_gpu_model(true)==0);
 CHECK(device->wait_idle());std::filesystem::remove_all(root,error);return 0;
}

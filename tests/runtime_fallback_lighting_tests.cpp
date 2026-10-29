#include "../engine/runtime/src/fallback_shaders.hpp"
#include "../engine/runtime/src/depth_shaders.hpp"
#include <vice/math/math.hpp>
#include <vice/render/mesh_asset.hpp>
#include <array>
#include <cmath>
#include <iostream>
#include <span>
#include <vector>
#define CHECK(c) do { if (!(c)) { std::cerr << "Failed line " << __LINE__ << ": " #c << '\n'; return __LINE__; } } while(false)
using namespace vice;
using namespace vice::render;
constexpr ShaderBindingKey key(ShaderRegisterClass c, unsigned n) { return {.register_class=c,.binding=n}; }
int main() {
    auto device=Device::create(); CHECK(device);
    const bool metal=device->backend()==Backend::metal;
    constexpr std::array vertex_resources{
        ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::constant_buffer,.binding=0,.count=1},
        ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::shader_resource,.binding=1,.count=1},
        ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::shader_resource,.binding=2,.count=1}};
    ShaderModuleDesc vsd{.stage=ShaderStage::vertex,
        .language=metal?ShaderSourceLanguage::metal:ShaderSourceLanguage::hlsl,
        .source=metal?runtime::detail::fallback_metal_source:runtime::detail::fallback_hlsl_source,
        .resources=vertex_resources,.entry_point="vice_runtime_mesh_vs"};
    auto vs=device->create_shader_module(vsd); CHECK(vs);
    auto fsd=vsd; fsd.stage=ShaderStage::fragment;
    fsd.resources=runtime::detail::fallback_fragment_resources; fsd.entry_point="vice_runtime_mesh_fs";
    auto fs=device->create_shader_module(fsd); CHECK(fs);
    auto dsd=vsd; dsd.source=metal?runtime::detail::gpu_shadow_metal_source:runtime::detail::gpu_shadow_hlsl_source;
    dsd.entry_point="vice_runtime_gpu_shadow_vs";
    auto dvs=device->create_shader_module(dsd); CHECK(dvs);
    constexpr std::array attributes{
        VertexAttributeDesc{.semantic="POSITION",.shader_location=0,.format=VertexFormat::float32x3,.offset=0},
        VertexAttributeDesc{.semantic="COLOR",.shader_location=1,.format=VertexFormat::float32x3,.offset=12},
        VertexAttributeDesc{.semantic="NORMAL",.shader_location=2,.format=VertexFormat::float32x3,.offset=24}};
    const std::array layouts{VertexBufferLayoutDesc{.stride=sizeof(MeshVertex),.attributes=attributes}};
    GraphicsPipelineDesc pd; pd.vertex_shader=vs.get();pd.fragment_shader=fs.get();
    pd.vertex_buffers=layouts;pd.color_format=TextureFormat::rgba16_float;pd.cull_mode=CullMode::none;
    pd.depth={.format=TextureFormat::depth32_float,.test_enabled=true,.write_enabled=true,.compare=CompareOp::less_equal};
    auto pipeline=device->create_graphics_pipeline(pd); CHECK(pipeline);
    pd.vertex_shader=dvs.get();pd.fragment_shader=nullptr;pd.color_format=TextureFormat::undefined;
    auto depth_pipeline=device->create_graphics_pipeline(pd); CHECK(depth_pipeline);
    // Read the rendered HDR pixels through the public compute/storage/readback path.
    constexpr std::array read_resources{
        ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::shader_resource,.binding=0,.count=1},
        ShaderResourceBindingDesc{.register_class=ShaderRegisterClass::unordered_access,.binding=0,.count=1}};
    ShaderModuleDesc read_desc{.stage=ShaderStage::compute,.language=vsd.language,
        .source=metal ? R"(
#include <metal_stdlib>
using namespace metal;
kernel void read_pixels(texture2d<float,access::read> image [[texture(0)]],device float4* out [[buffer(0)]],uint2 p [[thread_position_in_grid]]) { out[p.y*32+p.x]=image.read(p); }
)" : R"(
Texture2D<float4> image : register(t0); RWStructuredBuffer<float4> output : register(u0);
[numthreads(8,8,1)] void read_pixels(uint3 p:SV_DispatchThreadID) { output[p.y*32+p.x]=image.Load(int3(p.xy,0)); }
)",.resources=read_resources,.entry_point="read_pixels"};
    auto read_shader=device->create_shader_module(read_desc); CHECK(read_shader);
    auto read_pipeline=device->create_compute_pipeline({.compute_shader=read_shader.get(),.thread_group_size={8,8,1}}); CHECK(read_pipeline);
    auto buffer=[&](std::uint64_t bytes,BufferUsage usage,MemoryLocation memory) { return device->create_buffer({.size=bytes,.usage=usage,.memory=memory}); };
    auto constant=buffer(784,BufferUsage::uniform,MemoryLocation::cpu_to_gpu);CHECK(constant);
    auto models=buffer(64,BufferUsage::storage|BufferUsage::copy_destination,MemoryLocation::gpu_only);CHECK(models);
    auto indices=buffer(4,BufferUsage::storage|BufferUsage::copy_destination,MemoryLocation::gpu_only);CHECK(indices);
    auto upload=buffer(68,BufferUsage::copy_source,MemoryLocation::cpu_to_gpu);CHECK(upload);
    auto model=math::identity();model(0,0)=1.7F;model(1,1)=.65F;
    CHECK(upload->write(0,std::as_bytes(std::span{&model,1})));
    const unsigned zero=0;CHECK(upload->write(64,std::as_bytes(std::span{&zero,1})));
    const std::array<MeshVertex,3> vertices{{
        {{-.5F,-1.3F,.5F},{.8F,.4F,.2F},{.70710678F,0,.70710678F},{0,0}},
        {{ .5F,-1.3F,.5F},{.8F,.4F,.2F},{.70710678F,0,.70710678F},{0,0}},
        {{ 0,1.3F,.5F},{.8F,.4F,.2F},{.70710678F,0,.70710678F},{0,0}}}};
    auto vb=buffer(sizeof(vertices),BufferUsage::vertex,MemoryLocation::cpu_to_gpu);CHECK(vb);
    CHECK(vb->write(0,std::as_bytes(std::span{vertices})));
    auto color=device->create_texture({.width=32,.height=32,.format=TextureFormat::rgba16_float,.usage=TextureUsage::color_attachment|TextureUsage::sampled});CHECK(color);
    auto depth=device->create_texture({.width=32,.height=32,.format=TextureFormat::depth32_float,.usage=TextureUsage::depth_attachment|TextureUsage::sampled});CHECK(depth);
    auto shadow=device->create_texture({.width=32,.height=32,.format=TextureFormat::depth32_float,.usage=TextureUsage::depth_attachment|TextureUsage::sampled});CHECK(shadow);
    auto sampler=device->create_sampler({});CHECK(sampler);
    constexpr unsigned bytes=32*32*16;
    auto output=buffer(bytes,BufferUsage::storage|BufferUsage::copy_source,MemoryLocation::gpu_only);CHECK(output);
    auto readback=buffer(bytes,BufferUsage::copy_destination,MemoryLocation::gpu_to_cpu);CHECK(readback);
    // Same layout as the production frame; test values are authored independently.
    std::array<float,196> frame{};auto identity=math::identity();
    const auto vp=math::multiply(math::perspective_rh_zo(1.05F,1.F,.05F,100.F),math::look_at_rh({0,0,3},{0,0,0},{0,1,0}));
    std::copy(vp.values.begin(),vp.values.end(),frame.begin());
    frame[18]=3;
    frame[20]=1; // light count
    frame[24+2]=-1;frame[24+4]=1;frame[24+5]=1;frame[24+6]=1;frame[24+7]=4;
    auto render_case=[&](float light_z,bool shadowed,std::array<float,4>& pixel)->bool {
        frame[26]=light_z;
        frame[184]=shadowed?1.F:0.F;frame[185]=0;frame[186]=1.F/32;frame[187]=.0015F;
        for(unsigned i=0;i<4;++i){std::copy(identity.values.begin(),identity.values.end(),frame.begin()+120+i*16);frame[188+i]=10.F;}
        frame[194]=-1;
        if(!constant->write(0,std::as_bytes(std::span{frame}))) return false;
        auto commands=device->create_command_list();if(!commands)return false;
        if(!commands->copy_buffer(*upload,0,*models,0,64)||!commands->copy_buffer(*upload,64,*indices,0,4))return false;
        if(!commands->begin_depth_render_pass(*shadow,.1F,true)||!commands->end_render_pass())return false;
        auto bind=[&](){return commands->set_graphics_constant_buffer(key(ShaderRegisterClass::constant_buffer,0),*constant,0,784)&&
            commands->set_graphics_buffer(key(ShaderRegisterClass::shader_resource,1),*models,{.size=64,.structure_stride=64})&&
            commands->set_graphics_buffer(key(ShaderRegisterClass::shader_resource,2),*indices,{.size=4,.structure_stride=4})&&commands->set_vertex_buffer(0,*vb);};
        if(!commands->begin_depth_render_pass(*depth,1,true)||!commands->set_graphics_pipeline(*depth_pipeline)||!bind()||!commands->draw(3)||!commands->end_render_pass())return false;
        RenderPassDesc pass;pass.clear_color={0,0,0,1};pass.depth_attachment=depth.get();pass.clear_depth_attachment=false;
        if(!commands->begin_render_pass(*color,pass)||!commands->set_graphics_pipeline(*pipeline)||!bind())return false;
        for(unsigned i=3;i<7;++i)if(!commands->set_graphics_texture(key(ShaderRegisterClass::shader_resource,i),*shadow))return false;
        if(!commands->set_graphics_sampler(key(ShaderRegisterClass::sampler,1),*sampler)||!commands->draw(3)||!commands->end_render_pass())return false;
        if(!commands->set_compute_pipeline(*read_pipeline)||!commands->set_compute_texture(key(ShaderRegisterClass::shader_resource,0),*color)||!commands->set_compute_storage_buffer(key(ShaderRegisterClass::unordered_access,0),*output,{.size=bytes,.structure_stride=16})||!commands->dispatch({4,4,1})||!commands->copy_buffer(*output,0,*readback,0,bytes))return false;
        auto submission=device->submit(std::move(commands));if(submission==invalid_submission||!device->wait_for_submission(submission))return false;
        std::array<std::array<float,4>,1024> pixels{};
        if(!readback->read(0,std::as_writable_bytes(std::span{pixels})))return false;
        pixel=pixels[16*32+16];
        // A central interior patch must survive the real production depth prepass.
        for(unsigned y=14;y<19;++y)for(unsigned x=14;x<18;++x)if(!std::isfinite(pixels[y*32+x][0])||pixels[y*32+x][0]<.02F)return false;
        return true;
    };
    std::array<float,4> lit{},away{},occluded{};
    CHECK(render_case(-1,false,lit));CHECK(render_case(1,false,away));CHECK(render_case(-1,true,occluded));
    CHECK(lit[0]>away[0]*2);CHECK(lit[0]>occluded[0]*2);
    // Inverse transpose under x scale 1.7: the normal points mostly toward +Z.
    const float expected=.8F*(.0825F+4.F/3.14159265F/std::sqrt(1.F+1.F/(1.7F*1.7F)));
    CHECK(std::abs(lit[0]-expected)<.025F);
    std::array<float,4> point{},outside{};
    frame[27]=1;frame[32]=4;
    CHECK(render_case(1.5F,false,point));
    frame[32]=.1F;
    CHECK(render_case(1.5F,false,outside));
    CHECK(point[0]>outside[0]*2);
    CHECK(std::abs(outside[0]-away[0])<.005F);
    std::cout<<"lit="<<lit[0]<<" away="<<away[0]<<" shadow="<<occluded[0]<<'\n';
    return 0;
}

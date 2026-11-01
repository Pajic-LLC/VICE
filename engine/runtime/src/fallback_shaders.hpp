#pragma once
#include <vice/render/rhi.hpp>
#include <array>
#include <string_view>
namespace vice::runtime::detail {
inline constexpr std::array fallback_fragment_resources{
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::constant_buffer, .binding=0, .space=0, .count=1, .unbounded=false},
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::shader_resource, .binding=3, .space=0, .count=1, .unbounded=false},
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::shader_resource, .binding=4, .space=0, .count=1, .unbounded=false},
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::shader_resource, .binding=5, .space=0, .count=1, .unbounded=false},
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::shader_resource, .binding=6, .space=0, .count=1, .unbounded=false},
    render::ShaderResourceBindingDesc{.register_class=render::ShaderRegisterClass::sampler, .binding=1, .space=0, .count=1, .unbounded=false},
};
inline constexpr std::string_view fallback_hlsl_source=R"(
struct Light { float4 position_or_direction; float4 color_intensity; float4 parameters; };
cbuffer SceneFrame : register(b0) {
    column_major float4x4 view_projection;
    float4 camera_position;
    float4 light_metadata;
    Light lights[8];
    column_major float4x4 shadow_matrices[4];
    float4 shadow_metadata;
    float4 shadow_splits;
    float4 camera_forward;
};
StructuredBuffer<float4x4> instance_models : register(t1);
StructuredBuffer<uint> compacted_instance_indices : register(t2);
Texture2D<float> shadow0 : register(t3);
Texture2D<float> shadow1 : register(t4);
Texture2D<float> shadow2 : register(t5);
Texture2D<float> shadow3 : register(t6);
SamplerState shadow_sampler : register(s1);
struct VertexIn { float3 position : POSITION0; float3 color : COLOR0; float3 normal : NORMAL0; };
struct VertexOut { float4 position : SV_Position; float3 color : COLOR0; float3 normal : TEXCOORD0; float3 world : TEXCOORD1; };
VertexOut vice_runtime_mesh_vs(VertexIn input, uint instance_id : SV_InstanceID) {
    VertexOut output;
    float4x4 m = instance_models[compacted_instance_indices[instance_id]];
    // Cofactor basis gives the inverse transpose up to determinant magnitude.
    float3 a = float3(m[0][0],m[1][0],m[2][0]);
    float3 b = float3(m[0][1],m[1][1],m[2][1]);
    float3 c = float3(m[0][2],m[1][2],m[2][2]);
    float determinant = dot(a,cross(b,c));
    float3 n = input.normal.x*cross(b,c)+input.normal.y*cross(c,a)+input.normal.z*cross(a,b);
    output.normal = normalize(n * (determinant < 0 ? -1.0 : 1.0));
    // Keep the full world float4, matching the production depth shader.
    const float4 world_position = mul(m, float4(input.position, 1.0));
    output.position = mul(view_projection, world_position);
    output.world = world_position.xyz;
    output.color = input.color;
    return output;
}
float shadow_depth(uint cascade, float2 uv) {
    if(cascade==0) return shadow0.SampleLevel(shadow_sampler,uv,0);
    if(cascade==1) return shadow1.SampleLevel(shadow_sampler,uv,0);
    if(cascade==2) return shadow2.SampleLevel(shadow_sampler,uv,0);
    return shadow3.SampleLevel(shadow_sampler,uv,0);
}
float visibility(float3 world, float ndotl) {
    if(shadow_metadata.x < 0.5) return 1;
    float depth = dot(world-camera_position.xyz,camera_forward.xyz);
    uint cascade = depth > shadow_splits.x ? (depth > shadow_splits.y ? (depth > shadow_splits.z ? 3 : 2) : 1) : 0;
    if(depth > shadow_splits.w) return 1;
    float4 clip = mul(shadow_matrices[cascade],float4(world,1));
    float3 p = clip.xyz / clip.w;
    float2 uv = float2(p.x*.5+.5,.5-p.y*.5);
    if(any(uv<0)||any(uv>1)||p.z<0||p.z>1) return 1;
    float bias = shadow_metadata.w * (1.0 + 2.0*(1.0-ndotl));
    float result=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        result += p.z-bias <= shadow_depth(cascade,uv+float2(x,y)*shadow_metadata.z) ? 1.0 : 0.0;
    return result/9.0;
}
float4 vice_runtime_mesh_fs(VertexOut input) : SV_Target0 {
    float3 n=normalize(input.normal);
    float hemi=saturate(n.y*.5+.5);
    float3 irradiance=lerp(float3(.035,.03,.025),float3(.13,.18,.25),hemi);
    for(uint i=0;i<(uint)light_metadata.x;++i) {
        float3 l=-lights[i].position_or_direction.xyz;
        float attenuation=1;
        if(lights[i].position_or_direction.w > .5) {
            l=lights[i].position_or_direction.xyz-input.world;
            float d2=max(dot(l,l),.01);
            float range=max(lights[i].parameters.x,.001);
            float window=saturate(1.0-d2/(range*range));
            attenuation=window*window/d2;
        }
        float ndotl=saturate(dot(n,normalize(l)));
        float shadow=(shadow_metadata.x>.5 && i==(uint)shadow_metadata.y) ? visibility(input.world,ndotl) : 1;
        irradiance += lights[i].color_intensity.rgb * lights[i].color_intensity.a * attenuation * ndotl * shadow / 3.14159265;
    }
    return float4(input.color*irradiance,1);
}
)";
inline constexpr std::string_view fallback_metal_source=R"(#include <metal_stdlib>
using namespace metal;
struct Light { float4 position_or_direction; float4 color_intensity; float4 parameters; };
struct SceneFrame {
    float4x4 view_projection;
    float4 camera_position;
    float4 light_metadata;
    Light lights[8];
    float4x4 shadow_matrices[4];
    float4 shadow_metadata;
    float4 shadow_splits;
    float4 camera_forward;
};
struct VertexIn { float3 position [[attribute(11)]]; float3 color [[attribute(12)]]; float3 normal [[attribute(13)]]; };
struct VertexOut { float4 position [[position]]; float3 color; float3 normal; float3 world; };
vertex VertexOut vice_runtime_mesh_vs(VertexIn input [[stage_in]],constant SceneFrame& frame [[buffer(0)]],device const float4x4* instance_models [[buffer(1)]],device const uint* compacted_instance_indices [[buffer(2)]],uint instance_id [[instance_id]]) {
    VertexOut output;
    float4x4 m=instance_models[compacted_instance_indices[instance_id]];
    // Inverse-transpose normal transform; also correct mirrored instances.
    float3 a=m[0].xyz,b=m[1].xyz,c=m[2].xyz;
    float determinant=dot(a,cross(b,c));
    float3 n=input.normal.x*cross(b,c)+input.normal.y*cross(c,a)+input.normal.z*cross(a,b);
    output.normal=normalize(n*(determinant<0 ? -1.0 : 1.0));
    // Keep this operation order identical to the production depth prepass.
    const float4 world_position = instance_models[compacted_instance_indices[instance_id]] * float4(input.position, 1.0);
    output.position = frame.view_projection * world_position;
    output.world = world_position.xyz;
    output.color=input.color;
    return output;
}
float shadow_depth(uint cascade,float2 uv,depth2d<float> s0,depth2d<float> s1,depth2d<float> s2,depth2d<float> s3,sampler samp) {
    if(cascade==0) return s0.sample(samp,uv,level(0));
    if(cascade==1) return s1.sample(samp,uv,level(0));
    if(cascade==2) return s2.sample(samp,uv,level(0));
    return s3.sample(samp,uv,level(0));
}
float visibility(float3 world,float ndotl,constant SceneFrame& f,depth2d<float> s0,depth2d<float> s1,depth2d<float> s2,depth2d<float> s3,sampler samp) {
    if(f.shadow_metadata.x<.5) return 1;
    float depth=dot(world-f.camera_position.xyz,f.camera_forward.xyz);
    uint cascade=depth>f.shadow_splits.x ? (depth>f.shadow_splits.y ? (depth>f.shadow_splits.z ? 3 : 2) : 1) : 0;
    if(depth>f.shadow_splits.w) return 1;
    float4 clip=f.shadow_matrices[cascade]*float4(world,1);
    float3 p=clip.xyz/clip.w;
    float2 uv=float2(p.x*.5+.5,.5-p.y*.5);
    if(any(uv<0)||any(uv>1)||p.z<0||p.z>1) return 1;
    float bias=f.shadow_metadata.w*(1+2*(1-ndotl));
    float result=0;
    for(int y=-1;y<=1;++y) for(int x=-1;x<=1;++x)
        result += p.z-bias<=shadow_depth(cascade,uv+float2(x,y)*f.shadow_metadata.z,s0,s1,s2,s3,samp) ? 1.0 : 0.0;
    return result/9.0;
}
fragment float4 vice_runtime_mesh_fs(VertexOut input [[stage_in]],constant SceneFrame& f [[buffer(0)]],depth2d<float> s0 [[texture(3)]],depth2d<float> s1 [[texture(4)]],depth2d<float> s2 [[texture(5)]],depth2d<float> s3 [[texture(6)]],sampler samp [[sampler(1)]]) {
    float3 n=normalize(input.normal);
    float hemi=clamp(n.y*.5+.5,0.0,1.0);
    float3 irradiance=mix(float3(.035,.03,.025),float3(.13,.18,.25),hemi);
    for(uint i=0;i<uint(f.light_metadata.x);++i) {
        float3 l=-f.lights[i].position_or_direction.xyz;
        float attenuation=1;
        if(f.lights[i].position_or_direction.w>.5) {
            l=f.lights[i].position_or_direction.xyz-input.world;
            float d2=max(dot(l,l),.01);
            float range=max(f.lights[i].parameters.x,.001);
            float window=clamp(1-d2/(range*range),0.0,1.0);
            attenuation=window*window/d2;
        }
        float ndotl=clamp(dot(n,normalize(l)),0.0,1.0);
        float shadow=(f.shadow_metadata.x>.5 && i==uint(f.shadow_metadata.y)) ? visibility(input.world,ndotl,f,s0,s1,s2,s3,samp) : 1;
        irradiance+=f.lights[i].color_intensity.rgb*f.lights[i].color_intensity.a*attenuation*ndotl*shadow/3.14159265;
    }
    return float4(input.color*irradiance,1);
}
)";
}

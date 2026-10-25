Texture2D<float4> ReflectionRadiance : register(t0, space0);
RWStructuredBuffer<float4> ValidationOutput : register(u0, space0);

[numthreads(1, 1, 1)]
void vice_reflection_radiance_readback_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    if (dispatch_thread_id.x != 0u || dispatch_thread_id.y != 0u || dispatch_thread_id.z != 0u) {
        return;
    }
    ValidationOutput[0] = ReflectionRadiance.Load(int3(0, 0, 0));
}

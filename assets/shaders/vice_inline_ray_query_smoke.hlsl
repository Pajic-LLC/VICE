RaytracingAccelerationStructure SceneTlas : register(t0, space0);
RWByteAddressBuffer QueryResult : register(u0, space0);

[numthreads(1, 1, 1)]
void vice_inline_ray_query_cs(uint3 dispatch_thread_id : SV_DispatchThreadID) {
    RayDesc ray;
    ray.Origin = float3(0.0, 0.0, 0.0);
    ray.Direction = float3(0.0, 0.0, 1.0);
    ray.TMin = 0.001;
    ray.TMax = 1000.0;

    RayQuery<RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH> query;
    query.TraceRayInline(SceneTlas, RAY_FLAG_NONE, 0xFF, ray);
    while (query.Proceed()) {
    }

    uint result = 0U;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        // Keep the persistent custom InstanceID and the TLAS array index distinct.
        // The latter is the portable lookup key for VICE's authoritative runtime-instance map.
        const uint instance_id = query.CommittedInstanceID();
        const uint instance_index = query.CommittedInstanceIndex();
        result = 0x80000000U | ((instance_id & 0x7FFFU) << 16U) | ((instance_index + 1U) & 0xFFFFU);
    }
    QueryResult.Store(dispatch_thread_id.x * 4U, result);
}

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

    uint hit = 0U;
    if (query.CommittedStatus() == COMMITTED_TRIANGLE_HIT) {
        // Validate both identity channels explicitly: the authored persistent custom InstanceID
        // is not the same thing as the TLAS array index used to address VICE's runtime map.
        const uint instance_id = query.CommittedInstanceID();
        const uint instance_index = query.CommittedInstanceIndex();
        hit = instance_id == 1U && instance_index == 0U ? 1U : 0U;
    }
    QueryResult.Store(dispatch_thread_id.x * 4U, hit);
}

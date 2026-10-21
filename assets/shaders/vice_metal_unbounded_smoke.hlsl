StructuredBuffer<uint> InputBuffers[] : register(t0, space0);
RWStructuredBuffer<uint> OutputBuffer : register(u0, space1);

[numthreads(1, 1, 1)]
void vice_metal_unbounded_cs(uint3 dispatch_id : SV_DispatchThreadID) {
    if (dispatch_id.x != 0u || dispatch_id.y != 0u || dispatch_id.z != 0u) {
        return;
    }
    OutputBuffer[0] = InputBuffers[2][0];
}

// to be baked into a header file!!!!
struct SkyboxData
{
    float4x4 invProj;
    float4x4 invViewRot;
    uint     textureIndex;
    uint     samplerIndex;
    uint     _pad0;
    uint     _pad1;
};

struct PushConstants
{
    Ptr<SkyboxData> skybox;
};

[vk::push_constant] PushConstants pc;
[[vk::binding(0, 0)]] SamplerState bindlessSamplers[];
[[vk::binding(1, 0)]] Texture2D bindlessTextures[];

struct VSOutput
{
    float4 position : SV_Position;
    float3 viewDir;
};

static const float2 kFullscreenPositions[3] = {
    float2(-1.0, -1.0),
    float2( 3.0, -1.0),
    float2(-1.0,  3.0),
};

[shader("vertex")]
VSOutput vertexMain(uint vertexIndex : SV_VertexID)
{
    VSOutput output;
    float2 pos = kFullscreenPositions[vertexIndex];
    output.position = float4(pos, 1.0, 1.0);
    float4 viewSpace = mul(pc.skybox->invProj, float4(pos, 1.0, 1.0));
    viewSpace.xyz /= viewSpace.w;
    output.viewDir = mul((float3x3)pc.skybox->invViewRot, viewSpace.xyz);
    return output;
}

static const float kPi    = 3.14159265359;
static const float kTwoPi = 6.28318530718;

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 dir = normalize(input.viewDir);

    uint texIndex     = NonUniformResourceIndex(pc.skybox->textureIndex);
    uint samplerIndex  = NonUniformResourceIndex(pc.skybox->samplerIndex);

    float u = atan2(dir.z, dir.x) / kTwoPi + 0.5;
    float v = acos(clamp(dir.y, -1.0, 1.0)) / kPi;

    float4 color = bindlessTextures[texIndex].SampleLevel(bindlessSamplers[samplerIndex], float2(u, v), 0);
    return float4(color.rgb, 1.0);
}

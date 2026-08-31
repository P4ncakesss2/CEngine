// to be baked into a header file!!!!
struct ObjectData
{
    float4x4 model;
    float4x4 invmodel;
    uint     albedoIndex;
    uint     samplerIndex;
    float2   _pad;
};

struct CameraData
{
    float4x4 viewproj;
};

struct PushConstants
{
    Ptr<CameraData> camera;
    Ptr<ObjectData> object;
};

[vk::push_constant] PushConstants pc;
[[vk::binding(0, 0)]] SamplerState bindlessSamplers[];
[[vk::binding(1, 0)]] Texture2D bindlessTextures[];

struct VSInput
{
    float3 position : POSITION;
    float3 normal : NORMAL;
    float2 uv1;
};

struct VSOutput
{
    float4 position : SV_Position;
    float3 normal : NORMAL;
    float2 uv1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint instanceIndex : SV_InstanceID)
{
    VSOutput output;
    float4 worldPos = mul(pc.object->model, float4(input.position, 1.0));
    output.position = mul(pc.camera->viewproj, worldPos);
    output.normal = normalize(mul((float3x3)pc.object->model, input.normal));
    output.uv1 = input.uv1;
    return output;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);

    uint albedoIndex = NonUniformResourceIndex(pc.object->albedoIndex);
    uint samplerIndex = NonUniformResourceIndex(pc.object->samplerIndex);
    float4 albedo = bindlessTextures[albedoIndex].Sample(bindlessSamplers[samplerIndex], input.uv1);
    return albedo;
}

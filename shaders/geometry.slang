// to be baked into a header file!!!!
struct ObjectData
{
    float4x4 model;
    float4x4 invmodel;
};

struct MaterialData
{
    uint albedoIndex;
    uint samplerIndex;
    uint isTiled;
    uint _pad;
    float2 tiling;
    float2 _pad1;
};

struct CameraData
{
    float4x4 viewproj;
    float3   position;
    float    _pad;
};

struct DirLightData
{
    float3   direction;
    float    _pad0;
    float3   color;
    float    intensity;
};

struct ObjectShadowData
{
    float4x4 lightViewProj;
    float2   atlasUVOrigin;
    float    atlasUVScale;
    uint     enabled;
};

struct PushConstants
{
    Ptr<CameraData>   camera;
    Ptr<ObjectData>   object;
    Ptr<MaterialData> material;
    Ptr<DirLightData> light;
    Ptr<ObjectShadowData> objectShadows;
    uint objectShadowCount;
    uint objectShadowAtlasIndex;
    uint objectShadowSamplerIndex;
};

[vk::push_constant] PushConstants pc;
[[vk::binding(0, 0)]] SamplerState bindlessSamplers[];
[[vk::binding(0, 0)]] SamplerComparisonState bindlessComparisonSamplers[];
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
    float3 worldPos;
    float3 normal : NORMAL;
    float2 uv1;
};

[shader("vertex")]
VSOutput vertexMain(VSInput input, uint instanceIndex : SV_InstanceID)
{
    VSOutput output;

    float4 worldPos = mul(pc.object->model, float4(input.position, 1.0));

    output.position = mul(pc.camera->viewproj, worldPos);
    output.worldPos = worldPos.xyz;
    output.normal = normalize(mul((float3x3)pc.object->model, input.normal));
    output.uv1 = input.uv1;

    return output;
}

float sampleObjectShadows(float3 worldPos, PushConstants pc)
{
    float shadow = 1.0;
    if (pc.objectShadowCount == 0) return shadow;

    uint texIndex     = NonUniformResourceIndex(pc.objectShadowAtlasIndex);
    uint samplerIndex = NonUniformResourceIndex(pc.objectShadowSamplerIndex);

    static const float kTileResolution = 256.0;
    static const float kTexelSizeLocal = 1.0 / kTileResolution;
    static const float kHalfTexelInsetLocal = 0.5 / kTileResolution;

    static const float2 kPoisson4[4] = {
        float2(-0.94201624, -0.39906216),
        float2(0.94558609, -0.76890725),
        float2(-0.09418410, -0.92938870),
        float2(0.34495938, 0.29387760)
    };

    for (uint i = 0; i < pc.objectShadowCount; i++) {
        ObjectShadowData caster = pc.objectShadows[i];
        if (caster.enabled == 0) continue;

        float4 clip = mul(caster.lightViewProj, float4(worldPos, 1.0));
        clip.xyz /= clip.w;

        float2 localUV = clip.xy * 0.5 + 0.5;
        
        if (localUV.x < 0.0 || localUV.x > 1.0 || localUV.y < 0.0 || localUV.y > 1.0) continue;
        if (clip.z > 1.0 || clip.z < 0.0) continue;

        float currentDepth = clip.z - 0.002;
        float shadowAccum = 0.0;

        [unroll]
        for (int tap = 0; tap < 4; tap++) {
            float2 offset = kPoisson4[tap] * kTexelSizeLocal;
            float2 insetLocalUV = clamp(localUV + offset, kHalfTexelInsetLocal, 1.0 - kHalfTexelInsetLocal);
            float2 atlasUV = caster.atlasUVOrigin + insetLocalUV * caster.atlasUVScale;

            shadowAccum += bindlessTextures[texIndex].SampleCmpLevelZero(
                bindlessComparisonSamplers[samplerIndex], atlasUV, currentDepth);
        }

        shadow *= (shadowAccum / 4.0);
    }
    return shadow;
}

[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);

    uint albedoIndex = NonUniformResourceIndex(pc.material->albedoIndex);
    uint samplerIndex = NonUniformResourceIndex(pc.material->samplerIndex);

    float2 uv = input.uv1;

    if (pc.material->isTiled != 0) {
        uv *= pc.material->tiling;
    }
    float4 albedo = bindlessTextures[albedoIndex].Sample(bindlessSamplers[samplerIndex], uv);

    float3 litColor = albedo.rgb;

    DirLightData light;
    light.direction = pc.light->direction;
    light.color     = pc.light->color;
    light.intensity = pc.light->intensity;

    if (light.intensity > 0.0) {
        float3 toLight = normalize(-light.direction);
        float ndotl = max(dot(normal, toLight), 0.0);
        float shadow = sampleObjectShadows(input.worldPos, pc);

        float3 ambient = albedo.rgb * 0.15;
        float3 diffuse = albedo.rgb * light.color * light.intensity * ndotl * shadow;
        litColor = ambient + diffuse;
    }

    return float4(litColor, albedo.a);
}

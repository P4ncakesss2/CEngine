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
    uint isStochasticTiled;
    float2 tiling;
    float2 _pad1;
};

struct CameraData
{
    float4x4 viewproj;
};

struct PushConstants
{
    Ptr<CameraData>   camera;
    Ptr<ObjectData>   object;
    Ptr<MaterialData> material;
};

[vk::push_constant] PushConstants pc;
[[vk::binding(0, 0)]] SamplerState bindlessSamplers[];
[[vk::binding(1, 0)]] Texture2D bindlessTextures[];

static const float3 kSunDirection = normalize(float3(-0.4, -1.0, -0.3));
static const float3 kSunColor     = float3(1.0, 0.96, 0.9);
static const float  kSunIntensity = 1.0;
static const float3 kAmbient      = float3(0.08, 0.08, 0.1);

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


float Hash21(float2 p)
{
    p = frac(p * float2(123.34, 456.21));
    p += dot(p, p + 45.32);
    return frac(p.x * p.y);
}

float2 RotateUV90(float2 uv, uint rotation)
{
    switch (rotation)
    {
        case 1: return float2(1.0 - uv.y, uv.x);
        case 2: return float2(1.0 - uv.x, 1.0 - uv.y);
        case 3: return float2(uv.y, 1.0 - uv.x);
        default: return uv;
    }
}

// Computes the stochastically offset/rotated UV for a specific grid cell
float2 GetWarpedUV(float2 uv, float2 cell)
{
    float r0 = Hash21(cell);
    uint rotation = (uint)(r0 * 4.0);

    float2 randomOffset = float2(
        Hash21(cell + 31.17),
        Hash21(cell + 71.93)
    );

    return RotateUV90(uv, rotation) + randomOffset;
}

float4 SampleStochastic(Texture2D tex, SamplerState samp, float2 uv)
{
    float2 dx = ddx(uv);
    float2 dy = ddy(uv);
    float2 p = floor(uv);
    float2 f = frac(uv);

    float2 w = f * f * (3.0 - 2.0 * f);

    float2 uv00 = GetWarpedUV(uv, p + float2(0.0, 0.0));
    float2 uv10 = GetWarpedUV(uv, p + float2(1.0, 0.0));
    float2 uv01 = GetWarpedUV(uv, p + float2(0.0, 1.0));
    float2 uv11 = GetWarpedUV(uv, p + float2(1.0, 1.0));

    float4 c00 = tex.SampleGrad(samp, uv00, dx, dy);
    float4 c10 = tex.SampleGrad(samp, uv10, dx, dy);
    float4 c01 = tex.SampleGrad(samp, uv01, dx, dy);
    float4 c11 = tex.SampleGrad(samp, uv11, dx, dy);

    float4 c0 = lerp(c00, c10, w.x);
    float4 c1 = lerp(c01, c11, w.x);
    return lerp(c0, c1, w.y);
}


[shader("fragment")]
float4 fragmentMain(VSOutput input) : SV_Target
{
    float3 normal = normalize(input.normal);

    uint albedoIndex = NonUniformResourceIndex(pc.material->albedoIndex);
    uint samplerIndex = NonUniformResourceIndex(pc.material->samplerIndex);

    float2 uv = input.uv1;
    float4 albedo = float4(1.0, 1.0, 1.0, 1.0);

    if (pc.material->isTiled != 0)
    {
        uv *= pc.material->tiling;

        if (pc.material->isStochasticTiled != 0)
        {
            // Call our new stochastic blending function
            albedo = SampleStochastic(
                bindlessTextures[albedoIndex], 
                bindlessSamplers[samplerIndex], 
                uv
            );
        }
        else
        {
            albedo = bindlessTextures[albedoIndex].Sample(bindlessSamplers[samplerIndex], uv);
        }
    }
    else
    {
        albedo = bindlessTextures[albedoIndex].Sample(bindlessSamplers[samplerIndex], uv);
    }

    float ndotl = max(dot(normal, -kSunDirection), 0.0);
    float3 lighting = kAmbient + kSunColor * kSunIntensity * ndotl;

    return float4(albedo.rgb * lighting, albedo.a);
}
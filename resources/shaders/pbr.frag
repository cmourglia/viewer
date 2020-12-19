layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;

#ifdef HAS_NORMAL_MAP
#endif

layout (location = 0) out vec4 out_color;

uniform mat4 u_view;

uniform vec3 u_eye;

uniform vec3 u_albedo;

#ifdef HAS_ROUGHNESS
uniform float u_roughness;
#endif

#ifdef HAS_METALLIC
uniform float u_metallic;
#endif

#ifdef HAS_EMISSIVE
uniform vec3 u_emissive;
#endif

#ifdef HAS_ALBEDO_TEXTURE
uniform sampler2D s_albedo;
#endif

#ifdef HAS_ROUGHNESS_TEXTURE
uniform sampler2D s_roughness;
#endif

#ifdef HAS_METALLIC_TEXTURE
uniform sampler2D s_metallic;
#endif

#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
uniform sampler2D s_metallicRoughness;
#endif

#ifdef HAS_EMISSIVE_TEXTURE
uniform sampler2D s_emissive;
#endif

#ifdef HAS_NORMAL_MAP
uniform sampler2D s_normal;
#endif

#ifdef HAS_AMBIENT_OCCLUSION_MAP
uniform sampler2D s_ambientOcclusion;
#endif

uniform samplerCube s_irradianceMap;
uniform samplerCube s_radianceMap;
uniform sampler2D s_iblDFG;

#define PI 3.14159265359
#define INV_PI 0.31830988618
#define MIN_PERCEPTUAL_ROUGHNESS 0.045

vec3 GetAlbedo() {
    vec3 result = vec3(0.0);

#ifdef HAS_ALBEDO_TEXTURE
#   ifdef HAS_ALBEDO
    result = u_albedo * pow(texture(s_albedo, in_texcoord).rgb, vec3(2.2));
#   else
    result = texture(s_albedo, in_texcoord).rgb;
#   endif
#else
#   ifdef HAS_ALBEDO
    result = u_albedo;
#   endif
#endif

    return result;
}

float GetAlpha()
{
#ifdef HAS_ALBEDO_TEXTURE
    return texture(s_albedo, in_texcoord).a;
#endif
    return 1.0f;
}

vec2 GetMetallicRoughness()
{
    vec2 result = vec2(1, 1);

#ifdef HAS_METALLIC
    result.x *= u_metallic;
#endif

#ifdef HAS_ROUGHNESS
    result.y *= u_roughness;
#endif

#ifdef HAS_METALLIC_TEXTURE
    result.x *= texture(s_metallic, in_texcoord).r;
#endif

#ifdef HAS_ROUGHNESS_TEXTURE
    result.y *= texture(s_roughness, in_texcoord).r;
#endif

#ifdef HAS_METALLIC_ROUGHNESS_TEXTURE
    result *= texture(s_metallicRoughness, in_texcoord).bg;
#endif

    // return result;
    return result;
}

vec3 GetEmissive() {
    vec3 result = vec3(0.0);

#ifdef HAS_EMISSIVE_TEXTURE
#   ifdef HAS_EMISSIVE
    result = u_emissive * pow(texture(s_emissive, in_texcoord).rgb, vec3(2.2));
#   else
    result = texture(s_emissive, in_texcoord).rgb;
#   endif
#else
#   ifdef HAS_EMISSIVE
    result = u_emissive;
#   endif
#endif

    return result;
}

#ifdef HAS_NORMAL_MAP
// http://www.thetenthplanet.de/archives/1180
mat3 CotangentFrame(vec3 N, vec3 p, vec2 uv)
{
    // get edge vectors of the pixel triangle
    vec3 dp1  = dFdx(p);
    vec3 dp2  = dFdy(p);
    vec2 duv1 = dFdx(uv);
    vec2 duv2 = dFdy(uv);

    // solve the linear system
    vec3 dp2perp = cross(dp2, N);
    vec3 dp1perp = cross(N, dp1);
    vec3 T = dp2perp * duv1.x + dp1perp * duv2.x;
    vec3 B = dp2perp * duv1.y + dp1perp * duv2.y;

    // construct a scale invariant frame
    float invmax = inversesqrt(max(dot(T, T), dot(B, B)));
    return mat3(T * invmax, B * invmax, N);
}
#endif

vec3 GetNormal()
{
    return normalize(in_normal);
#ifdef HAS_NORMAL_MAP
    vec3 normal = normalize(in_normal);
    vec3 normalTex = texture(s_normal, in_texcoord).rgb * 2.0 - vec3(1.0);

    vec3 p = u_eye - in_position;
    vec3 N = normal;
    vec2 uv = in_texcoord;

    mat3 TBN = CotangentFrame(N, p, uv);
    return normalize(TBN * normalTex);
#else
    return normalize(in_normal);
#endif
}

float GetAmbientOcclusion()
{
#ifdef HAS_AMBIENT_OCCLUSION_MAP
    return texture(s_ambientOcclusion, in_texcoord).r;
#else
    return 1.0f;
#endif
}

vec3 GetLightPos(int index)
{
    if (index == 0) {
        return vec3(-10, 10, 10);
    }
    if (index == 1) {
        return vec3(10, 10, 10);
    }
    if (index == 2) {
        return vec3(-10, -10, 10);
    }
    if (index == 3) {
        return vec3(10, -10, 10);
    }
}

vec3 GetLightColor(int index) {
    return vec3(150, 150, 150);
}

vec3 GetViewPos()
{
    return u_eye;
}

vec3 GetFragPos()
{
    return in_position;
}

vec3 GetDiffuseColor(vec3 albedo, float metallic)
{
    return albedo * (1.0 - metallic);
}

vec3 GetF0(vec3 albedo, float metallic, float reflectance)
{
    return albedo * metallic + (reflectance * (1.0 - metallic));
}

float GetDielectricF0(float reflectance)
{
    return 0.16 * reflectance * reflectance;
}

// Utils
float Pow5(float x)
{
    const float x2 = x * x;
    return x2 * x2 * x;
}

float saturate(float x)
{
    return clamp(x, 0, 1);
}

vec3 F_Schlick(vec3 f0, float VoH)
{
    return f0 + (1 - f0) * pow(1 - VoH, 5.0);
}

float D_GGX(float a, float NoH)
{
    float a2 = a * a;
    float f = (NoH * a2 - NoH) * NoH + 1.0;
    return a2 / (PI * f * f);
}

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a, float NoV, float NoL)
{
    float V_SmithV = NoL * (NoV * (1 - a) + a);
    float V_SmithL = NoV * (NoL * (1 - a) + a);
    return 0.5 / (V_SmithL + V_SmithV);
}

float Fd_Lambert()
{
    return INV_PI;
}

struct PixelParams
{
    vec3 diffuseColor;
    vec3 f0;
    float metallic;
    float roughness; // perceptualRoughness * perceptualRoughness
    float perceptualRoughness; // max(roughness, MIN_PERCEPTUAL_ROUGHNESS)
    float reflectance; // 0.04
    vec3 dfg;
    vec3 energyCompensation;
};

vec3 EnvDFGPolynomial(vec3 f0, float roughness, float NoV)
{
    float x = 1 - roughness;
    float y = NoV;

    float b1 = -0.1688;
    float b2 = 1.895;
    float b3 = 0.9903;
    float b4 = -4.853;
    float b5 = 8.404;
    float b6 = -5.069;
    float bias = saturate(min(b1 * x + b2 * x * x, b3 + b4 * y + b5 * y * y + b6 * y * y * y));

    float d0 = 0.6045;
    float d1 = 1.699;
    float d2 = -0.5228;
    float d3 = -3.603;
    float d4 = 1.404;
    float d5 = 0.1939;
    float d6 = 2.661;
    float delta = saturate(d0 + d1 * x + d2 * y + d3 * x * x + d4 * x * y + d5 * y * y + d6 * x * x * x);
    float scale = delta - bias;

    bias *= saturate(50.0 * f0.y);
    return f0 * scale + bias;
}

vec3 SpecularDFG(const PixelParams params)
{
    return params.f0 * params.dfg.x + params.dfg.y;
}

void GetPixelParams(inout PixelParams params, float NoV)
{
    vec3 albedo = GetAlbedo();
    vec2 metallicRoughness = GetMetallicRoughness();

    params.metallic = metallicRoughness.x; // Useful ?
    params.reflectance = 0.04;
    params.perceptualRoughness = clamp(metallicRoughness.y, MIN_PERCEPTUAL_ROUGHNESS, 1);
    params.roughness = params.perceptualRoughness * params.perceptualRoughness;
    params.diffuseColor = albedo * (1.0 - metallicRoughness.x);
    params.f0 = albedo * params.metallic + (params.reflectance * (1.0 - params.metallic));

    params.dfg = textureLod(s_iblDFG, vec2(NoV, params.roughness), 0.0).rgb;
    params.energyCompensation = 1.0 + params.f0 * (1.0 / params.dfg.y - 1.0);
}

vec3 EvaluateIBL(in vec3 n, in vec3 v, in PixelParams params)
{
    // specular layer
    vec3 E = SpecularDFG(params);
    vec3 r = reflect(-v, n);
    vec3 specularIndirect = textureLod(s_radianceMap, r, params.perceptualRoughness * 8.0).rgb;
    // vec3 specularIndirect = textureLod(s_radianceMap, r, 0).rgb;
    vec3 Fr = specularIndirect * E;

    // diffuse layer
    vec3 diffuseIrradiance = texture(s_irradianceMap, n).rgb;
    vec3 Fd = params.diffuseColor * diffuseIrradiance * (1.0 - E);

    return Fr + Fd;
}

vec3 BRDF(in vec3 n, in vec3 v, in vec3 l, in PixelParams params)
{
    vec3 h = normalize(v + l);

    float NoV = max(dot(n, v), 1e-4);
    float NoL = saturate(dot(n, l));
    float NoH = saturate(dot(n, h));
    float LoH = saturate(dot(l, h));
    float VoH = saturate(dot(h, v));

    vec3 F = F_Schlick(params.f0, NoH);
    float D = D_GGX(params.roughness, NoH);
    float Vis = Vis_SmithJointApprox(params.roughness, NoV, NoL);

    vec3 Fr = (D * Vis) * F;
    vec3 Fd = params.diffuseColor * Fd_Lambert();

    return Fr + Fd;
}

vec3 EvaluateDirectLighting(in vec3 n, in vec3 v, in PixelParams params)
{
    vec3 Lo = vec3(0.0);

    for (int i = 0; i < 4; ++i)
    {
        vec3 lightVec = GetLightPos(i) - GetFragPos();

        vec3 l = normalize(lightVec);

        float attenuation = 1.0 / dot(lightVec, lightVec);

        vec3 illuminance = GetLightColor(i) * saturate(dot(n, l)) * attenuation;
        vec3 luminance = BRDF(n, v, l, params) * illuminance;

        Lo += luminance;
    }

    return Lo;
}

void main()
{
    vec3 n = GetNormal();
    vec3 v = normalize(GetViewPos() - GetFragPos());

    if (GetAlpha() < 0.5) discard;

    PixelParams params;
    GetPixelParams(params, max(dot(n, v), 1e-4));

    vec3 color = EvaluateIBL(n, v, params);
    color += EvaluateDirectLighting(n, v, params);
    color += GetEmissive();
    color *= GetAmbientOcclusion();

    // tonemapping
    color = color / (color + 1.0);
    // gamma
    color = pow(color, vec3(1.0 / 2.2));

    out_color = vec4(color, 1.0);
}
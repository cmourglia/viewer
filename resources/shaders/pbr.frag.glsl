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

#if defined(HAS_EMISSIVE) || defined(HAS_EMISSIVE_TEXTURE)
uniform float u_emissiveFactor;
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

#define MIN_PERCEPTUAL_ROUGHNESS 0.045

#include "math_utils.glsl"
#include "pbr_utils.glsl"

vec3 GetAlbedo() {
    vec3 result = vec3(0.0);

#if defined(HAS_ALBEDO_TEXTURE) && defined(HAS_ALBEDO)
    result = u_albedo * pow(texture(s_albedo, in_texcoord).rgb, vec3(2.2));
#elif defined(HAS_ALBEDO_TEXTURE)
    result = pow(texture(s_albedo, in_texcoord).rgb, vec3(2.2));
#elif defined(HAS_ALBEDO)
    result = u_albedo;
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

    return result;
}

vec3 GetEmissive() {
    vec3 result = vec3(0.0);

#ifdef HAS_EMISSIVE_TEXTURE
#   ifdef HAS_EMISSIVE
    result = u_emissive * texture(s_emissive, in_texcoord).rgb;
#   else
    result = texture(s_emissive, in_texcoord).rgb;
#   endif
#else
#   ifdef HAS_EMISSIVE
    result = u_emissive;
#   endif
#endif

#if defined(HAS_EMISSIVE_TEXTURE) || defined(HAS_EMISSIVE)
    result *= u_emissiveFactor;
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

    if (index == 0) {
        return vec3(225,186,49);
    }
    if (index == 1) {
        return vec3(9,184,20);
    }
    if (index == 2) {
        return vec3(239,49,56);
    }
    if (index == 3) {
        return vec3(123,198,168);
    }

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
    params.perceptualRoughness = clamp(metallicRoughness.y, MIN_PERCEPTUAL_ROUGHNESS, 1.0);
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

    for (int i = 0; i < 0; ++i)
    {
        vec3 lightVec = GetLightPos(i) - GetFragPos();

        vec3 l = normalize(lightVec);

        float attenuation = 1.0 / dot(lightVec, lightVec);

        vec3 illuminance = GetLightColor(i) * 0.01 * saturate(dot(n, l)) * attenuation;
        vec3 luminance = BRDF(n, v, l, params) * illuminance;

        Lo += luminance;
    }

    return Lo;
}

vec3 tonemap_Uchimura(vec3 x, float P, float a, float m, float l, float c, float b) {
    // Uchimura 2017, "HDR theory and practice"
    // Math: https://www.desmos.com/calculator/gslcdxvipg
    // Source: https://www.slideshare.net/nikuque/hdr-theory-and-practicce-jp
    float l0 = ((P - m) * l) / a;
    float L0 = m - m / a;
    float L1 = m + (1.0 - m) / a;
    float S0 = m + l0;
    float S1 = m + a * l0;
    float C2 = (a * P) / (P - S1);
    float CP = -C2 / P;

    vec3 w0 = 1.0 - smoothstep(0.0, m, x);
    vec3 w2 = step(m + l0, x);
    vec3 w1 = 1.0 - w0 - w2;

    vec3 T = m * pow(x / m, vec3(c)) + b;
    vec3 S = P - (P - S1) * exp(CP * (x - S0));
    vec3 L = m + a * (x - m);

    return T * w0 + L * w1 + S * w2;
}

vec3 tonemap_Uchimura(vec3 x) {
    const float P = 1.0;  // max display brightness
    const float a = 1.0;  // contrast
    const float m = 0.22; // linear section start
    const float l = 0.4;  // linear section length
    const float c = 1.33; // black
    const float b = 0.0;  // pedestal
    return tonemap_Uchimura(x, P, a, m, l, c, b);
}

vec3 gamma(vec3 v) {
    vec3 r;

    for (int i = 0; i < 3; ++i) {
        float f = v[i];
        if (f <= 0.0031308f) {
            r[i] = f * 12.92f;
        } else {
            r[i] = 1.055f * pow(f, 1.0f / 2.4f) - 0.055f;
        }
    }
    return r;
}

void main()
{
    vec3 n = GetNormal();
    vec3 v = normalize(GetViewPos() - GetFragPos());

    if (GetAlpha() < 0.1) discard;

    PixelParams params;
    GetPixelParams(params, max(dot(n, v), 1e-4));

    vec3 color = EvaluateIBL(n, v, params) + EvaluateDirectLighting(n, v, params) + GetEmissive();
    color *= GetAmbientOcclusion();

    out_color = vec4(color, 1.0);
}
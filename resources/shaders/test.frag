layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;

#ifdef HAS_NORMAL_MAP
#endif

layout (location = 0) out vec4 out_color;

uniform mat4 u_view;

#ifdef HAS_ALBEDO
uniform vec3 u_albedo;
#endif

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

vec3 GetAlbedo(){
    vec3 result = vec3(1.0);

#ifdef HAS_ALBEDO
    result *= u_albedo;
#endif

#ifdef HAS_ALBEDO_TEXTURE
    result *= texture(s_albedo, in_texcoord).rgb;
#endif

    return result;
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
    result *= texture(s_metallicRoughness, in_texcoord).rg;
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
#ifdef HAS_NORMAL_MAP
    vec3 normal = normalize(in_normal);
    vec3 normalTex = texture(s_normal, in_texcoord).rgb * 2.0 - vec3(1.0);

    vec3 p = vec3(-u_view[3]) - in_position;
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

vec3 GetLightPos()
{
    // return vec3(0, 5, 0);
    return vec3(0, 2, -2);
}

vec3 GetViewPos()
{
    return vec3(-u_view[3]);
}

vec3 GetFragPos()
{
    return in_position;
}

void main()
{
    vec3 color = GetAlbedo() * GetAmbientOcclusion();

    // out_color = vec4(GetAmbientOcclusion(), 0, 0, 1);
    // out_color = vec4(vec3(GetMetallicRoughness().x), 1);

    vec3 normal = GetNormal();
    vec3 lightDir = normalize(GetLightPos() - GetFragPos());

    float diffuseFactor = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diffuseFactor * GetAlbedo() * GetAmbientOcclusion();

    color = diffuse;
    color += GetEmissive();

    out_color = vec4(color, 1.0);

    // out_color = vec4(vec3(diffuseFactor), 1.0);

    // out_color = vec4(GetNormal() * 0.5 + 0.5, 1);
}
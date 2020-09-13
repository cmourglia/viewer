layout (location = 0) in vec3 in_position;
layout (location = 1) in vec3 in_normal;
layout (location = 2) in vec2 in_texcoord;

layout (location = 0) out vec4 out_color;

#ifdef HAS_ALBEDO
uniform vec3 u_albedo;
#endif

#ifdef HAS_ALBEDO_TEXTURE
uniform sampler2D s_albedo;
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

void main()
{
    // out_color = vec4(in_texcoord, 0.0, 1);
    out_color = vec4(GetAlbedo(), 1.0);
}
layout (location = 0) in vec3 in_position;

layout (location = 0) out vec4 out_color;

uniform sampler2D equirectangularMap;

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    return uv * invAtan + 0.5;
}

void main()
{
    vec2 uv = SampleSphericalMap(normalize(in_position));
    vec3 color = texture(equirectangularMap, uv).rgb;

    out_color = vec4(color, 1.0);
}
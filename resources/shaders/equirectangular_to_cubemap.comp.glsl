layout (local_size_x = 8, local_size_y = 8, local_size_z = 6) in;
layout (binding = 0) uniform sampler2D equirectangularMap;
layout (binding = 1, rgba32f) writeonly uniform imageCube envmap;

#include "base_math.glsl"
#include "cubemap_helpers.glsl"

const vec2 invAtan = vec2(0.1591, 0.3183);

vec2 SampleSphericalMap(vec3 v)
{
    vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
    return uv * invAtan + 0.5;
}

void main()
{
    ivec3 cubeCoord = ivec3(gl_GlobalInvocationID);
    vec3 worldPos = CubeCoordToWorld(cubeCoord, vec2(1024, 1024));
    vec3 normal = normalize(worldPos);

    vec2 uv = SampleSphericalMap(normal);

    imageStore(envmap, cubeCoord, texture(equirectangularMap, uv));
    // imageStore(envmap, cubeCoord, vec4(normal * 0.5 + 0.5, 1.0));
}
layout (local_size_x = 8, local_size_y = 8, local_size_z = 6) in;
layout (binding = 0) uniform samplerCube envMap;
layout (binding = 1, rgba32f) writeonly uniform imageCube irradianceMap;

const vec2 cubemapSize = vec2(64, 64);

#include "base_math.glsl"
#include "cubemap_helpers.glsl"

const float sampleTheta = TWO_PI / 360.0;
const float samplePhi = HALF_PI / 90.0;

void main()
{
    ivec3 cubeCoord = ivec3(gl_GlobalInvocationID);
    vec3 worldPos = CubeCoordToWorld(cubeCoord, cubemapSize);

    // Tangent space from origin point
    vec3 normal = normalize(worldPos);
    vec3 up = vec3(0.0, 1.0, 0.0);
    vec3 right = normalize(cross(up, normal));
    up = cross(normal, right);

    int sampleCount = 0;
    vec3 irradiance = vec3(0.0);
    for (float phi = 0.0; phi < TWO_PI; phi += sampleTheta)
    {
        float sinPhi = sin(phi);
        float cosPhi = cos(phi);

        for (float theta = 0.0; theta < HALF_PI; theta += samplePhi)
        {
            float sinTheta = sin(theta);
            float cosTheta = cos(theta);

            vec3 tempVec = cosPhi * right + sinPhi * up;
            vec3 sampleVector = cosTheta * normal + sinTheta * tempVec;

            irradiance += textureLod(envMap, sampleVector, 0).rgb * cosTheta * sinTheta;
            sampleCount += 1;
        }
    }

    irradiance *= PI * (1.0 / float(sampleCount));

    imageStore(irradianceMap, cubeCoord, vec4(irradiance, 1.0));
}

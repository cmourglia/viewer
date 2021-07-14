layout (local_size_x = 8, local_size_y = 8, local_size_z = 6) in;
layout (binding = 0) uniform samplerCube envMap;
layout (binding = 1, rgba32f) writeonly uniform imageCube radianceMap;

uniform float roughness;
uniform vec2 mipSize;

#include "base_math.glsl"
#include "cubemap_helpers.glsl"
#include "pbr_utils.glsl"

const vec2 cubemapSize = vec2(1024, 1024);
const float originalSamples = cubemapSize.x * cubemapSize.y;
const uint sampleCount = 1024u;

void main()
{
	ivec3 cubeCoord = ivec3(gl_GlobalInvocationID);
	vec3 worldPos = CubeCoordToWorld(cubeCoord, mipSize);
	vec3 N = normalize(worldPos);

	// Assume view direction always equal to outgoing direction
	vec3 R = N;
	vec3 V = R;

	vec3 color = vec3(1.0);
	float totalWeight = 0.0;

	float invSampleCount = 1.0f / sampleCount;

	for (uint i = 0; i < sampleCount; ++i)
	{
		vec2 Xi = Hammersley(i, invSampleCount);
		vec3 H = HemisphereImportanceSampleDGGX(Xi, roughness * roughness, N);
		vec3 L = normalize(2.0f * dot(V, H) * H - V);

		float NoL = saturate(dot(N, L));

		if (NoL > 0)
		{
			float NoH = saturate(dot(N, H));
			float VoH = saturate(dot(V, H));

			float pdf = D_GGX(NoH, roughness) * NoH / (4 * VoH) + 1e-4;

			float omegaS = 1.0 / (sampleCount * pdf + 1e-4);
			float omegaP = 4.0 * PI / (6.0 * originalSamples);
			float mipLevel = roughness == 0.0 ? 0.0 : 0.5 * log2(omegaS / omegaP);

			color += textureLod(envMap, L, mipLevel).rgb * NoL;
			totalWeight += NoL;
		}
	}

	color /= totalWeight;

	imageStore(radianceMap, cubeCoord, vec4(color, 1.0));
}
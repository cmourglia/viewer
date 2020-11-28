layout (location = 0) in vec3 in_position;

layout (location = 0) out vec4 out_color;

uniform samplerCube envmap;

uniform float roughness;
uniform float lodLevel;

const float PI = 3.14159265359;

vec2 Hammersley(uint i, float invN)
{
	float tof  = 0.5f / 0x80000000U;
	uint  bits = i;

	bits = (bits << 16u) | (bits >> 16u);
	bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
	bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
	bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
	bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
	return vec2(i * invN, bits * tof);
}

vec3 HemisphereImportanceSampleDGGX(const vec2 u, const float a, const vec3 N)
{
	float phi = 2.0 * PI * u.x;
	float cosTheta2 = (1.0f - u.y) / (1.0f + (a + 1.0f) * ((a - 1.0f) * u.y));
	float cosTheta  = sqrt(cosTheta2);
	float sinTheta  = sqrt(1.0f - cosTheta2);

	// From spherical coords to cartesian - halfway vector
	vec3 H = vec3(sinTheta * cos(phi), sinTheta * sin(phi), cosTheta);

	// From tangent space H vector to world space sample vector
	vec3 U = abs(N.z) < 0.999 ? vec3(0.0, 0.0, 1.0) : vec3(1.0, 0.0, 0.0);
	vec3 T = normalize(cross(U, N));
	vec3 B = cross(N, T);
	vec3 sampleVec = T * H.x + B * H.y + N * H.z;
	return normalize(sampleVec);
}

float D_GGX(float a, float NoH)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f);
}

#define SAMPLE_COUNT 1024u
void main()
{
	vec3 N = normalize(in_position);
	vec3 R = N;
	vec3 V = R;

	vec3 color = vec3(1.0);
	float totalWeight = 0.0;

	float invSampleCount = 1.0f / SAMPLE_COUNT;
	float perceptualRoughness = roughness * roughness;

	for (uint i = 0; i < SAMPLE_COUNT; ++i)
	{
		vec2 u = Hammersley(i, invSampleCount);
		vec3 H = HemisphereImportanceSampleDGGX(u, perceptualRoughness, N);
		vec3 L = 2.0f * dot(V, H) * H - V;

		float NoL = clamp(dot(N, L), 0.0, 1.0);

		if (NoL > 0)
		{
			float NoH = clamp(dot(N, H), 0.0, 1.0);
			float VoH = clamp(dot(V, H), 0.0, 1.0);

			float pdf = D_GGX(NoH, roughness) * NoH / (4 * VoH);

            float resolution = 256.0;
            float omegaS = 1.0 / (SAMPLE_COUNT * pdf);
            float omegaP = 4.0 * PI / (6.0 * resolution * resolution);
            float mipLevel = roughness == 0.0 ? 0.0 : clamp(0.5 * log2(omegaS / omegaP), 0, 8);

			color += textureLod(envmap, L, mipLevel).rgb * NoL;
			totalWeight += NoL;
		}
	}

	color /= totalWeight;

	out_color = vec4(color, 1.0);
}
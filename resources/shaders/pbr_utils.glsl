#ifndef PBR_UTILS_GLSL
#define PBR_UTILS_GLSL

#include "base_math.glsl"

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

// Fresnel
vec3 F_Schlick(vec3 f0, float VoH)
{
    return f0 + (1 - f0) * pow(1 - VoH, 5.0);
}

// Distribution
float D_GGX(float a, float NoH)
{
	float a2 = a * a;
	float f = (NoH * a2 - NoH) * NoH + 1.0;
	return a2 / (PI * f * f);
}

// Visibility term

// Appoximation of joint Smith term for GGX
// [Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"]
float Vis_SmithJointApprox(float a, float NoV, float NoL)
{
    float V_SmithV = NoL * (NoV * (1 - a) + a);
    float V_SmithL = NoV * (NoL * (1 - a) + a);
    return 0.5 / (V_SmithL + V_SmithV);
}

// Diffuse stuff
float Fd_Lambert()
{
    return INV_PI;
}

#endif // PBR_UTILS_GLSL
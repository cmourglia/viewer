
#include "defines.h"
#include "utils.h"

#include <stdint.h>
#include <stdlib.h>
#include <math.h>
#include <algorithm>
#include <vector>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/glm.hpp>

f32 Vis(f32 a, f32 NoV, f32 NoL)
{
	// Heitz 2014, "Understanding the Masking-Shadowing Function in Microfacet-Based BRDFs"
	// Height-correlated GGX
	const f32 a2    = a * a;
	const f32 GGX_L = NoV * sqrtf((NoL - NoL * a2) * NoL + a2);
	const f32 GGX_V = NoL * sqrtf((NoV - NoV * a2) * NoV + a2);
	return 0.5f / (GGX_V + GGX_L);
}

inline glm::vec3 HemisphereImportanceSampleDGGX(const glm::vec2& u, const f32 a)
{
	const f32 phi       = Tau * u.x;
	const f32 cosTheta2 = (1.0f - u.y) / (1.0f + (a + 1.0f) * ((a - 1.0f) * u.y));
	const f32 cosTheta  = sqrtf(cosTheta2);
	const f32 sinTheta  = sqrtf(1.0f - cosTheta2);
	return {sinTheta * cosf(phi), sinTheta * sinf(phi), cosTheta};
}

glm::vec2 DFV(f32 NoV, f32 roughness, u32 sampleCount)
{
	glm::vec2 r(0.0f);

	const glm::vec3 V(sqrtf(1.0f - NoV * NoV), 0.0f, NoV);
	const f32       invSampleCount = 1.0f / sampleCount;

	for (u32 i = 0; i < sampleCount; ++i)
	{
		const glm::vec2 u   = Hammersley(i, invSampleCount);
		const glm::vec3 H   = HemisphereImportanceSampleDGGX(u, roughness);
		const glm::vec3 L   = 2.0f * glm::dot(V, H) * H - V;
		const f32       VoH = Saturate(glm::dot(V, H));
		const f32       NoL = Saturate(L.z);
		const f32       NoH = Saturate(H.z);
		if (NoL > 0)
		{
			/*
			 * Fc = (1 - V•H)^5
			 * F(h) = f0*(1 - Fc) + f90*Fc
			 *
			 * f0 and f90 are known at runtime, but thankfully can be factored out, allowing us
			 * to split the integral in two terms and store both terms separately in a LUT.
			 *
			 * At runtime, we can reconstruct Er() exactly as below:
			 *
			 *            4                      <v•h>
			 *   DFV.x = --- ∑ (1 - Fc) V(v, l) ------- <n•l>
			 *            N  h                   <n•h>
			 *
			 *
			 *            4                      <v•h>
			 *   DFV.y = --- ∑ (    Fc) V(v, l) ------- <n•l>
			 *            N  h                   <n•h>
			 *
			 *
			 *   Er() = f0 * DFV.x + f90 * DFV.y
			 */
			const f32 v  = Vis(roughness, NoV, NoL) * NoL * (VoH / NoH);
			const f32 Fc = Pow5(1.0 - VoH);
			r.x += v * (1.0f - Fc);
			r.y += v * Fc;
		}
	}

	return 4.0f * r * invSampleCount;
}

std::vector<glm::vec3> PrecomputeDFG(u32 w, u32 h, u32 sampleCount) // 128, 128, 512
{
	std::vector<glm::vec3> lutDataRG32F;
	lutDataRG32F.resize(w * h);

	for (u32 y = 0; y < h; ++y)
	{
		const f32 roughness       = Saturate((h - y + 0.5f) / h);
		const f32 linearRoughness = roughness * roughness;
		for (u32 x = 0; x < w; ++x)
		{
			const f32 NoV = Saturate((x + 0.5f) / w);
			// const f32 m2  = m * m;

			// const f32 vx = sqrtf(1.0f - NoV * NoV);
			// const f32 vy = 0.0f;
			// const f32 vz = NoV;

			// f32 scale = 0.0f;
			// f32 bias  = 0.0f;

			const glm::vec2 d = DFV(NoV, linearRoughness, sampleCount);

			lutDataRG32F[y * w + x] = glm::vec3(d.x, d.y, 0.0);

			// for (u32 i = 0; i < sampleCount; ++i)
			// {
			// 	const glm::vec2 h = Hammersley(i, 1.0f / sampleCount);

			// 	const f32 phi      = 2.0f * MATH_PI * h.x;
			// 	const f32 cosPhi   = cosf(phi);
			// 	const f32 sinPhi   = sinf(phi);
			// 	const f32 cosTheta = sqrtf((1.0f - h.y) / (1.0f + (m2 - 1.0f) * h.y));
			// 	const f32 sinTheta = sqrtf(1.0f - cosTheta * cosTheta);

			// 	const f32 hx = sinTheta * cosf(phi);
			// 	const f32 hy = sinTheta * sinf(phi);
			// 	const f32 hz = cosTheta;

			// 	const f32 vdh = vx * hx + vy * hy + vz * hz;
			// 	const f32 lx  = 2.0f * vdh * hx - vx;
			// 	const f32 ly  = 2.0f * vdh * hy - vy;
			// 	const f32 lz  = 2.0f * vdh * hz - vz;

			// 	const f32 NoL   = std::max(lz, 0.0f);
			// 	const f32 ndoth = std::max(hz, 0.0f);
			// 	const f32 vdoth = std::max(vdh, 0.0f);

			// 	if (NoL > 0.0f)
			// 	{
			// 		const f32 vis         = Vis(roughness, NoV, NoL);
			// 		const f32 ndotlVisPDF = NoL * vis * (4.0f * vdoth / ndoth);
			// 		const f32 fresnel     = powf(1.0f - vdoth, 5.0f);

			// 		scale += ndotlVisPDF * (1.0f - fresnel);
			// 		bias += ndotlVisPDF * fresnel;
			// 	}
			// }
			// scale /= sampleCount;
			// bias /= sampleCount;
		}
	}

	return lutDataRG32F;
}
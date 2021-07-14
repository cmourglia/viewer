#include "environment.h"

#include "renderer/program.h"
#include "renderer/render_primitives.h"
#include "renderer/frame_stats.h"

#include "core/defines.h"
#include "core/utils.h"

#include <glad/glad.h>

#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void LoadEnvironment(const char* filename, Environment* env)
{
	FrameStats* stats = FrameStats::Get();
	Timer       timer;
	Timer       procTimer;

	stbi_set_flip_vertically_on_load(true);

	i32  w, h, c;
	f32* data = stbi_loadf(filename, &w, &h, &c, 0);

	stbi_set_flip_vertically_on_load(false);

	if (data == nullptr)
	{
		return;
	}

	const u32 cubemapSize = 1024;

	GLuint equirectangularTexture;
	glCreateTextures(GL_TEXTURE_2D, 1, &equirectangularTexture);

	// glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureStorage2D(equirectangularTexture, log2f(Min(w, h)), GL_RGB32F, w, h);
	glTextureSubImage2D(equirectangularTexture, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, data);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_image_free(data);

	stats->ibl.loadTexture = timer.Tick();

	// Cleanup old data
	if (!glIsTexture(env->envMap))
	{
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &env->envMap);
		glTextureStorage2D(env->envMap, log2f(cubemapSize), GL_RGBA32F, cubemapSize, cubemapSize);

		glTextureParameteri(env->envMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->envMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->envMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->envMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(env->envMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	Program* equirectangularToCubemapProgram = Program::GetProgramByName("equirectangularToCubemap");
	equirectangularToCubemapProgram->Bind();
	glBindTextureUnit(0, equirectangularTexture);
	glBindImageTexture(1, env->envMap, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

	glDispatchCompute(cubemapSize / 8, cubemapSize / 8, 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	glGenerateTextureMipmap(env->envMap);

	stats->ibl.cubemap = timer.Tick();

	if (!glIsTexture(env->radianceMap))
	{
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &env->radianceMap);
		glTextureStorage2D(env->radianceMap, 6, GL_RGBA32F, cubemapSize, cubemapSize);

		glTextureParameteri(env->radianceMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->radianceMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->radianceMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->radianceMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(env->radianceMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	Program* prefilterEnvmapProgram = Program::GetProgramByName("prefilterEnvmap");
	prefilterEnvmapProgram->Bind();
	glBindTextureUnit(0, env->envMap);

	u32 mipLevels = 6;
	u32 mipSize   = cubemapSize;

	for (u32 mip = 0; mip < mipLevels; ++mip, mipSize /= 2)
	{
		const f32 roughness = (f32)mip / (f32)(mipLevels - 1);

		glBindImageTexture(1, env->radianceMap, mip, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		prefilterEnvmapProgram->SetUniform("roughness", roughness);
		prefilterEnvmapProgram->SetUniform("mipSize", glm::vec2(mipSize, mipSize));

		glDispatchCompute(mipSize / 8, mipSize / 8, 1);
	}

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	stats->ibl.prefilter = timer.Tick();

	if (!glIsTexture(env->irradianceMap))
	{
		glCreateTextures(GL_TEXTURE_CUBE_MAP, 1, &env->irradianceMap);

		glTextureStorage2D(env->irradianceMap, 1, GL_RGBA32F, 64, 64);

		glTextureParameteri(env->irradianceMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->irradianceMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->irradianceMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		glTextureParameteri(env->irradianceMap, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTextureParameteri(env->irradianceMap, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	}

	Program* irradianceProgram = Program::GetProgramByName("irradiance");
	irradianceProgram->Bind();
	glBindTextureUnit(0, env->envMap); // glBindImageTexture(0, env->envMap, 0, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, env->irradianceMap, 0, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA32F);

	glDispatchCompute(8, 8, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	stats->ibl.irradiance = timer.Tick();
	stats->ibl.total      = procTimer.Tick();
}

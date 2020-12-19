#include "environment.h"

#include "program.h"
#include "render_primitives.h"

#include "core/defines.h"
#include "core/utils.h"

#include <glad/glad.h>

#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

void LoadEnvironment(const char* filename, Environment* env)
{
	stbi_set_flip_vertically_on_load(true);

	i32  w, h, c;
	f32* data = stbi_loadf(filename, &w, &h, &c, 0);

	stbi_set_flip_vertically_on_load(false);

	if (data == nullptr)
	{
		return;
	}

	const u32 cubemapSize = 512;

	GLuint equirectangularTexture;
	glCreateTextures(GL_TEXTURE_2D, 1, &equirectangularTexture);

	const i32 levels = log2f(Min(w, h));

	// glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureSubImage2D(equirectangularTexture, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, data);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_image_free(data);

	if (glIsTexture(env->envMap))
	{
		glDeleteTextures(1, &env->envMap);
	}

	glGenTextures(1, &env->envMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, env->envMap);

	for (i32 face = 0; face < 6; ++face)
	{
		// face: // +x, -x, +y, -y, +z, -z
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB32F, cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	// clang-format off
	const glm::mat4 captureProjection = glm::perspective(Pi * 0.5f, 1.0f, 0.1f, 10.0f);
	const glm::mat4 captureViews[]    = {
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f,  0.0f,  0.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  1.0f,  0.0f), glm::vec3(0.0f,  0.0f,  1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f, -1.0f,  0.0f), glm::vec3(0.0f,  0.0f, -1.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f,  1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
        glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3( 0.0f,  0.0f, -1.0f), glm::vec3(0.0f, -1.0f,  0.0f)),
    };
	// clang-format on

	GLuint captureFBO, captureRBO;
	glGenFramebuffers(1, &captureFBO);
	glGenRenderbuffers(1, &captureRBO);

	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, cubemapSize, cubemapSize);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

	Program* equirectangularToCubemapProgram = Program::GetProgramByName("equirectangularToCubemap");
	equirectangularToCubemapProgram->Bind();
	equirectangularToCubemapProgram->SetUniform("equirectangularMap", 0);
	glBindTextureUnit(0, equirectangularTexture);
	equirectangularToCubemapProgram->SetUniform("proj", captureProjection);
	glViewport(0, 0, cubemapSize, cubemapSize);

	glDepthFunc(GL_ALWAYS);
	for (i32 face = 0; face < 6; ++face)
	{
		equirectangularToCubemapProgram->SetUniform("view", captureViews[face]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, env->envMap, 0);

		if (glCheckNamedFramebufferStatus(captureFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "framebuffer incomplete\n");
		}

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClearDepth(1.0f);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		// glClearNamedFramebufferfv(captureFBO, GL_COLOR, 0, clearColor);
		// glClearNamedFramebufferfv(captureFBO, GL_DEPTH, 0, &clearDepth);

		RenderCube();
	}
	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glFlush();
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

	// Generate mipmaps
	if (glIsTexture(env->radianceMap))
	{
		glDeleteTextures(1, &env->radianceMap);
	}

	glGenTextures(1, &env->radianceMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, env->radianceMap);

	for (i32 face = 0; face < 6; ++face)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB32F, cubemapSize, cubemapSize, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glGenerateMipmap(GL_TEXTURE_CUBE_MAP); // Allocate memory

	Program* prefilterEnvmapProgram = Program::GetProgramByName("prefilterEnvmap");
	prefilterEnvmapProgram->Bind();
	prefilterEnvmapProgram->SetUniform("envmap", 0);
	glBindTextureUnit(0, env->envMap);
	prefilterEnvmapProgram->SetUniform("proj", captureProjection);

	u32 mipLevels = (u32)log2f((float)cubemapSize);
	u32 mipSize   = cubemapSize;

	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

	for (u32 mip = 0; mip < mipLevels; ++mip)
	{
		glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, mipSize, mipSize);

		glViewport(0, 0, mipSize, mipSize);

		const f32 roughness = (f32)mip / (f32)(mipLevels - 1);
		prefilterEnvmapProgram->SetUniform("roughness", roughness);

		for (i32 face = 0; face < 6; ++face)
		{
			prefilterEnvmapProgram->SetUniform("view", captureViews[face]);
			glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, env->radianceMap, mip);

			if (glCheckNamedFramebufferStatus(captureFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			{
				fprintf(stderr, "framebuffer incomplete\n");
			}

			glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
			glClearDepth(1.0f);
			glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
			// glClearNamedFramebufferfv(captureFBO, GL_COLOR, 0, clearColor);
			// glClearNamedFramebufferfv(captureFBO, GL_DEPTH, 0, &clearDepth);

			RenderCube();
		}

		mipSize /= 2;

		glBindRenderbuffer(GL_RENDERBUFFER, 0);
	}

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	if (glIsTexture(env->irradianceMap))
	{
		glDeleteTextures(1, &env->irradianceMap);
	}

	glGenTextures(1, &env->irradianceMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, env->irradianceMap);

	for (i32 face = 0; face < 6; ++face)
	{
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB32F, 64, 64, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
	glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);

	Program* irradianceProgram = Program::GetProgramByName("irradiance");
	irradianceProgram->Bind();
	irradianceProgram->SetUniform("envMap", 0);
	glBindTextureUnit(0, env->envMap);
	irradianceProgram->SetUniform("proj", captureProjection);
	glViewport(0, 0, 64, 64);

	glDepthFunc(GL_ALWAYS);
	for (i32 face = 0; face < 6; ++face)
	{
		irradianceProgram->SetUniform("view", captureViews[face]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, env->irradianceMap, 0);

		glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
		glClearDepth(1.0f);
		glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
		// glClearNamedFramebufferfv(captureFBO, GL_COLOR, 0, clearColor);
		// glClearNamedFramebufferfv(captureFBO, GL_DEPTH, 0, &clearDepth);

		if (glCheckNamedFramebufferStatus(captureFBO, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "framebuffer incomplete\n");
		}

		RenderCube();
	}
	glDepthFunc(GL_LESS);

	glBindRenderbuffer(GL_RENDERBUFFER, 0);
	glBindFramebuffer(GL_FRAMEBUFFER, 0);

	glDeleteRenderbuffers(1, &captureRBO);
	glDeleteFramebuffers(1, &captureFBO);
}

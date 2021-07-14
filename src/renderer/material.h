#pragma once

#include "program.h"
#include "environment.h"

#include <glad/glad.h>

#include <string>

struct Material
{
	Material(const char* matName, const char* baseVS, const char* baseFS);

	u32      GetMask() const;
	void     Bind(Program* program, const Environment* env);
	Program* GetProgram() const;

private:
	std::vector<const char*> GetDefines() const;
	std::string              GetUniqueName() const;

private:
	std::string m_name;
	std::string m_baseVS;
	std::string m_baseFS;

public:
	glm::vec3 albedo         = glm::vec3(0.5f, 0.5f, 0.5f);
	f32       roughness      = 0.0f;
	f32       metallic       = 0.0f;
	glm::vec3 emissive       = glm::vec3(0.0f, 0.0f, 0.0f);
	f32       emissiveFactor = 1.0f;

	GLuint albedoTexture            = 0;
	GLuint roughnessTexture         = 0;
	GLuint metallicTexture          = 0;
	GLuint metallicRoughnessTexture = 0;
	GLuint emissiveTexture          = 0;
	GLuint normalMap                = 0;
	GLuint ambientOcclusionMap      = 0;

	bool hasAlbedo                   = false;
	bool hasRoughness                = false;
	bool hasMetallic                 = false;
	bool hasMetallicRoughnessTexture = false;
	bool hasEmissive                 = false;
	bool hasAlbedoTexture            = false;
	bool hasRoughnessTexture         = false;
	bool hasMetallicTexture          = false;
	bool hasEmissiveTexture          = false;
	bool hasNormalMap                = false;
	bool hasAmbientOcclusionMap      = false;

private:
	i32 m_padding : 5;
};

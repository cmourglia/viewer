#pragma once

#include "program.h"
#include "environment.h"

#include <glad/glad.h>

#include <string>

struct Material
{
	Material(const std::string& matName, const std::string& baseVS, const std::string& baseFS);

	u32      GetMask() const;
	void     Bind(Program* program, const Environment* env);
	Program* GetProgram() const;

private:
	std::vector<std::string> GetDefines() const;
	std::string              GetUniqueName() const;

private:
	std::string m_name;
	std::string m_baseVS;
	std::string m_baseFS;

public:
	glm::vec3 albedo;
	f32       roughness;
	f32       metallic;
	glm::vec3 emissive;

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
	bool m_padding0;
	bool m_padding1;
	bool m_padding2;
	bool m_padding3;
	bool m_padding4;
};

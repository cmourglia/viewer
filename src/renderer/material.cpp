#include "material.h"

Material::Material(const std::string& matName, const std::string& baseVS, const std::string& baseFS)
    : m_name(matName)
    , m_baseVS(baseVS)
    , m_baseFS(baseFS)
{
}

u32 Material::GetMask() const
{
	// clang-format off
		const u32 result = 0
			| hasAlbedo                   << 0
			| hasAlbedoTexture            << 1
			| hasRoughness                << 2
			| hasRoughnessTexture         << 3
			| hasMetallic                 << 4
			| hasMetallicTexture          << 5
			| hasMetallicRoughnessTexture << 6
			| hasEmissive                 << 7
			| hasEmissiveTexture          << 8
			| hasNormalMap                << 9
			| hasAmbientOcclusionMap      << 10;

	// clang-format on
	return result;
}

void Material::Bind(Program* program, const Environment* env)
{
	if (hasAlbedo)
		program->SetUniform("u_albedo", albedo);
	if (hasRoughness)
		program->SetUniform("u_roughness", roughness);
	if (hasMetallic)
		program->SetUniform("u_metallic", metallic);
	if (hasEmissive)
		program->SetUniform("u_emissive", emissive);

	GLint index = 0;

	if (hasAlbedoTexture)
	{
		program->SetUniform("s_albedo", index);
		glBindTextureUnit(index, albedoTexture);
		++index;
	}

	if (hasRoughnessTexture)
	{
		program->SetUniform("s_roughness", index);
		glBindTextureUnit(index, roughnessTexture);
		++index;
	}

	if (hasMetallicTexture)
	{
		program->SetUniform("s_metallic", index);
		glBindTextureUnit(index, metallicTexture);
		++index;
	}

	if (hasMetallicRoughnessTexture)
	{
		program->SetUniform("s_metallicRoughness", index);
		glBindTextureUnit(index, metallicRoughnessTexture);
		++index;
	}

	if (hasEmissiveTexture)
	{
		program->SetUniform("s_emissive", index);
		glBindTextureUnit(index, emissiveTexture);
		++index;
	}

	if (hasNormalMap)
	{
		program->SetUniform("s_normal", index);
		glBindTextureUnit(index, normalMap);
		++index;
	}

	if (hasAmbientOcclusionMap)
	{
		program->SetUniform("s_ambientOcclusion", index);
		glBindTextureUnit(index, ambientOcclusionMap);
		++index;
	}

	program->SetUniform("s_irradianceMap", index);
	glBindTextureUnit(index, env->irradianceMap);
	++index;

	program->SetUniform("s_radianceMap", index);
	glBindTextureUnit(index, env->radianceMap);
	// glBindTextureUnit(index, g_envCubeMap);
	++index;

	program->SetUniform("s_iblDFG", index);
	glBindTextureUnit(index, env->iblDFG);
	++index;
}

Program* Material::GetProgram()
{
	return Program::MakeRender(GetUniqueName().c_str(), m_baseVS.c_str(), m_baseFS.c_str(), GetDefines());
}

std::vector<std::string> Material::GetDefines()
{
	std::vector<std::string> defines;

	if (hasAlbedoTexture)
	{
		defines.push_back("HAS_ALBEDO_TEXTURE");
	}
	if (hasRoughnessTexture)
	{
		defines.push_back("HAS_ROUGHNESS_TEXTURE");
	}
	if (hasMetallicTexture)
	{
		defines.push_back("HAS_METALLIC_TEXTURE");
	}
	if (hasMetallicRoughnessTexture)
	{
		defines.push_back("HAS_METALLIC_ROUGHNESS_TEXTURE");
	}
	if (hasEmissive)
	{
		defines.push_back("HAS_EMISSIVE");
	}
	if (hasEmissiveTexture)
	{
		defines.push_back("HAS_EMISSIVE_TEXTURE");
	}
	if (hasNormalMap)
	{
		defines.push_back("HAS_NORMAL_MAP");
	}
	if (hasAmbientOcclusionMap)
	{
		defines.push_back("HAS_AMBIENT_OCCLUSION_MAP");
	}

	return defines;
}

std::string Material::GetUniqueName()
{
	return std::string(m_name) + "_" + std::to_string(GetMask());
}
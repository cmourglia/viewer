#pragma once

#include "core/defines.h"

#include <glad/glad.h>

#include <glm/glm.hpp>

#include <string>
#include <filesystem>
#include <unordered_map>

struct shader
{
	GLenum                          type;
	std::string                filename;
	std::filesystem::file_time_type time;
};

class Program
{
public:
	static Program* MakeRender(const std::string&              name,
	                           const std::string&              vsfile,
	                           const std::string&              fsfile  = nullptr,
	                           const std::vector<std::string>& defines = std::vector<std::string>());
	static Program* MakeCompute(const std::string& name, const std::string& csfile);
	static Program* GetProgramByName(const std::string& name);
	static void     UpdateAllPrograms();

	explicit Program(const std::string& name = "");

	void Update();
	void Bind();
	void SetUniform(const std::string& name, int32_t value) const;
	void SetUniform(const std::string& name, u32 value) const;
	void SetUniform(const std::string& name, f32 value) const;
	void SetUniform(const std::string& name, const glm::vec2& value) const;
	void SetUniform(const std::string& name, const glm::vec3& value) const;
	void SetUniform(const std::string& name, const glm::vec4& value) const;
	void SetUniform(const std::string& name, const glm::mat2& value) const;
	void SetUniform(const std::string& name, const glm::mat3& value) const;
	void SetUniform(const std::string& name, const glm::mat4& value) const;

private:
	void  Build();
	void  GetUniformInfos();
	GLint GetLocation(const std::string& name) const;

private:
	using Shader  = std::pair<std::string, GLenum>;
	using Shaders = std::vector<Shader>;

	using ShaderTime  = std::pair<std::string, std::filesystem::file_time_type>;
	using ShaderTimes = std::vector<ShaderTime>;

	std::string m_name = nullptr;

	std::unordered_map<std::string, GLint> m_uniforms;

	GLuint                        m_id = 0;
	std::vector<shader>           m_shaders;
	std::vector<std::string> m_defines;
};

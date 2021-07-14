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
	std::string                     filename;
	std::filesystem::file_time_type time;
};

class Program
{
	using StringArray = std::vector<const char*>;

public:
	static Program* MakeRender(const char*        name,
	                           const char*        vsfile,
	                           const char*        fsfile  = nullptr,
	                           const StringArray& defines = StringArray());
	static Program* MakeCompute(const char* name, const char* csfile, const StringArray& defines = StringArray());
	static Program* GetProgramByName(const char* name);
	static void     UpdateAllPrograms();

	explicit Program(const char* name = "");

	void Update();
	void Bind();
	void SetUniform(const char* name, int32_t value) const;
	void SetUniform(const char* name, u32 value) const;
	void SetUniform(const char* name, f32 value) const;
	void SetUniform(const char* name, const glm::vec2& value) const;
	void SetUniform(const char* name, const glm::vec3& value) const;
	void SetUniform(const char* name, const glm::vec4& value) const;
	void SetUniform(const char* name, const glm::mat2& value) const;
	void SetUniform(const char* name, const glm::mat3& value) const;
	void SetUniform(const char* name, const glm::mat4& value) const;

private:
	void  Build();
	void  GetUniformInfos();
	GLint GetLocation(const char* name) const;

private:
	using Shader  = std::pair<std::string, GLenum>;
	using Shaders = std::vector<Shader>;

	using ShaderTime  = std::pair<std::string, std::filesystem::file_time_type>;
	using ShaderTimes = std::vector<ShaderTime>;

	std::string m_name = nullptr;

	std::unordered_map<std::string, GLint> m_uniforms;

	GLuint              m_id = 0;
	std::vector<shader> m_shaders;
	StringArray         m_defines;
};

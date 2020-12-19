#include "program.h"

#include <iostream>
#include <string>
#include <unordered_map>

std::unordered_map<std::string, Program> g_programsByName;

inline u32 CompileShader(const std::string& filename, GLenum shaderType, const std::vector<std::string>& defines)
{
	FILE* file = fopen(filename.data(), "r");
	if (!file)
	{
		return 0;
	}

	fseek(file, SEEK_SET, SEEK_END);
	long filesize = ftell(file);
	rewind(file);

	std::string src;
	src.resize(filesize);
	filesize = (size_t)fread(src.data(), 1, filesize, file);
	src.resize(filesize);

	fclose(file);

	std::vector<std::string> completeShader;
	completeShader.push_back("#version 450\n");
	for (const auto& define : defines)
	{
		completeShader.push_back(std::string("#define ") + define.data() + "\n");
	}
	completeShader.push_back(std::move(src));

	char** finalSrc = new char*[completeShader.size()];
	for (size_t i = 0; i < completeShader.size(); ++i)
	{
		finalSrc[i] = new char[completeShader[i].size() + 1];
		strcpy(finalSrc[i], completeShader[i].data());
	}

	u32 shader = glCreateShader(shaderType);

	glShaderSource(shader, completeShader.size(), finalSrc, nullptr);
	GLenum error = glGetError();
	glCompileShader(shader);

	i32 compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

	for (size_t i = 0; i < completeShader.size(); ++i)
	{
		delete[] finalSrc[i];
	}
	delete[] finalSrc;

	if (!compiled)
	{
		char error[512];
		glGetShaderInfoLog(shader, 512, nullptr, error);
		fprintf(stderr, "Could not compile shader %s: %s\n", filename.data(), error);
		glDeleteShader(shader);
		shader = 0;
	}

	return shader;
}

Program* Program::MakeRender(const std::string&              name,
                             const std::string&              vsfile,
                             const std::string&              fsfile,
                             const std::vector<std::string>& defines)
{
	if (!g_programsByName.contains(name))
	{
		Program program(name);

		program.m_defines = defines;
		program.m_shaders.push_back({GL_VERTEX_SHADER, vsfile, std::filesystem::last_write_time(vsfile)});

		if (!fsfile.empty())
		{
			program.m_shaders.push_back({GL_FRAGMENT_SHADER, fsfile, std::filesystem::last_write_time(fsfile)});
		}

		program.Build();

		g_programsByName[name] = program;
	}

	return &g_programsByName[name];
}

Program* Program::MakeCompute(const std::string& name, const std::string& csfile)
{
	if (!g_programsByName.contains(name))
	{
		Program program(name);

		program.m_shaders.push_back({GL_COMPUTE_SHADER, csfile, std::filesystem::last_write_time(csfile)});
		program.Build();

		g_programsByName[name] = program;
	}

	return &g_programsByName[name];
}

Program* Program::GetProgramByName(const std::string& name)
{
	if (g_programsByName.contains(name))
	{
		return &g_programsByName[name];
	}

	return nullptr;
}

void Program::UpdateAllPrograms()
{
	for (auto&& program : g_programsByName)
	{
		program.second.Update();
	}
}

Program::Program(const std::string& name)
    : m_name(name)
{
}

void Program::Update()
{
	bool needsUpdate = false;

	for (auto&& shader : m_shaders)
	{
		auto shaderTime = std::filesystem::last_write_time(shader.filename);
		if (shaderTime > shader.time)
		{
			needsUpdate = true;
			shader.time = shaderTime;
		}
	}

	if (needsUpdate)
	{
		Build();
	}
}

void Program::Bind()
{
	glUseProgram(m_id);
}

void Program::SetUniform(const std::string& name, int32_t value) const
{
	glUniform1i(GetLocation(name), value);
}
void Program::SetUniform(const std::string& name, u32 value) const
{
	glUniform1ui(GetLocation(name), value);
}
void Program::SetUniform(const std::string& name, f32 value) const
{
	glUniform1f(GetLocation(name), value);
}
void Program::SetUniform(const std::string& name, const glm::vec2& value) const
{
	glUniform2fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const std::string& name, const glm::vec3& value) const
{
	glUniform3fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const std::string& name, const glm::vec4& value) const
{
	glUniform4fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const std::string& name, const glm::mat2& value) const
{
	glUniformMatrix2fv(GetLocation(name), 1, false, &value[0][0]);
}
void Program::SetUniform(const std::string& name, const glm::mat3& value) const
{
	glUniformMatrix3fv(GetLocation(name), 1, false, &value[0][0]);
}
void Program::SetUniform(const std::string& name, const glm::mat4& value) const
{
	glUniformMatrix4fv(GetLocation(name), 1, false, &value[0][0]);
}

void Program::Build()
{
	std::vector<GLuint> shaders;

	bool allValid = true;

	for (const auto& shader : m_shaders)
	{
		GLuint shaderID = CompileShader(shader.filename, shader.type, m_defines);
		if (glIsShader(shaderID))
		{
			shaders.push_back(shaderID);
		}
		else
		{
			allValid = false;
			break;
		}
	}

	if (!allValid)
	{
		for (auto shader : shaders)
		{
			glDeleteShader(shader);
		}
		return;
	}

	GLuint program = glCreateProgram();

	for (GLuint shader : shaders)
	{
		if (glIsShader(shader))
		{
			glAttachShader(program, shader);
		}
	}

	glLinkProgram(program);

	i32 linked = GL_FALSE;
	glGetProgramiv(program, GL_LINK_STATUS, &linked);

	if (!linked)
	{
		char error[512];
		glGetProgramInfoLog(program, 512, nullptr, error);
		fprintf(stderr, "Could not link program %s: %s\n", m_name.data(), error);
		glDeleteProgram(program);
		program = 0;
	}

	for (GLuint shader : shaders)
	{
		if (glIsShader(shader))
		{
			glDetachShader(program, shader);
			glDeleteShader(shader);
		}
	}

	if (glIsProgram(program))
	{
		m_id = program;
		GetUniformInfos();
	}
}

void Program::GetUniformInfos()
{
	GLint uniformCount = 0;
	glGetProgramiv(m_id, GL_ACTIVE_UNIFORMS, &uniformCount);

	if (uniformCount != 0)
	{
		GLint   maxNameLength = 0;
		GLsizei length        = 0;
		GLsizei count         = 0;
		GLenum  type          = GL_NONE;
		glGetProgramiv(m_id, GL_ACTIVE_UNIFORM_MAX_LENGTH, &maxNameLength);

		char* uniformName = new char[maxNameLength];

		for (GLint i = 0; i < uniformCount; ++i)
		{
			glGetActiveUniform(m_id, i, maxNameLength, &length, &count, &type, uniformName);

			GLint location = glGetUniformLocation(m_id, uniformName);

			m_uniforms.emplace(std::make_pair(std::string(uniformName, length), location));
		}

		delete[] uniformName;
	}
}

GLint Program::GetLocation(const std::string& name) const
{
	auto it = m_uniforms.find(name.data());
	return (it == m_uniforms.end()) ? -1 : it->second;
}

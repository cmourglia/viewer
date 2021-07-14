#include "program.h"

#include <iostream>
#include <string>
#include <unordered_map>
#include <regex>

std::unordered_map<std::string, Program> g_programsByName;

inline std::string GetShaderFullPath(const char* filename)
{
	return std::string("resources/shaders/") + filename;
}

inline std::string GetFileContent(const char* filename)
{
	FILE* file = fopen(filename, "r");
	if (!file)
	{
		return "";
	}

	defer(fclose(file));

	fseek(file, SEEK_SET, SEEK_END);
	long filesize = ftell(file);
	rewind(file);

	std::string fileContent;
	fileContent.resize(filesize);
	filesize = (size_t)fread(fileContent.data(), 1, filesize, file);
	fileContent.resize(filesize);

	return fileContent;
}

inline std::string ParseShader(const char* input, int level = 0)
{
	// Tokenize string
	std::vector<std::string> lines;

	const char* ptr       = input;
	const char* lineStart = input;

	while (*ptr)
	{
		bool isNewLine = false;

		switch (*ptr)
		{
			case '\n':
			{
				isNewLine = true;
				if (*(ptr + 1) == '\r')
					++ptr;
			}
			break;

			case '\r':
			{
				isNewLine = true;
				if (*(ptr + 1) == '\n')
					++ptr;
			}
			break;

			default:
				break;
		}

		++ptr;
		if (isNewLine)
		{
			lines.emplace_back(lineStart, ptr);
			lineStart = ptr;
		}
	}

	lines.emplace_back(lineStart, ptr);

	std::string content = "";

	// Look for #includes
	for (auto&& line : lines)
	{
		if (line.find("#include") != std::string::npos)
		{
			auto i = line.find_first_of("\"<");
			auto j = line.find_last_of("\">");

			auto filename = line.substr(i + 1, j - i - 1);

			auto src = GetFileContent(GetShaderFullPath(filename.c_str()).c_str());
			content += ParseShader(src.c_str(), level + 1);
			content += "\n"; // Just in case
		}
		else
		{
			content += line;
		}
	}

	return content;
}

inline u32 CompileShader(const char* filename, GLenum shaderType, const std::vector<const char*>& defines)
{
	std::string src = GetFileContent(filename);
	if (src.empty())
	{
		return 0;
	}

	std::string completeShader = "#version 450\n";
	for (const char* define : defines)
	{
		completeShader.append(std::string("#define ") + define + "\n");
	}

	completeShader.append(src);

	std::string finalShader = ParseShader(completeShader.c_str());

	const char* finalShaderData = finalShader.data();

	u32 shader = glCreateShader(shaderType);

	glShaderSource(shader, 1, &finalShaderData, nullptr);
	glCompileShader(shader);

	i32 compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

	if (!compiled)
	{
		char error[512];
		glGetShaderInfoLog(shader, 512, nullptr, error);
		fprintf(stderr, "Could not compile shader %s: %s\n", filename, error);
		glDeleteShader(shader);
		shader = 0;
	}

	return shader;
}

Program* Program::MakeRender(const char* name, const char* vsfile, const char* fsfile, const StringArray& defines)
{
	if (!g_programsByName.contains(name))
	{
		Program program(name);

		program.m_defines   = defines;
		std::string vshader = GetShaderFullPath(vsfile);

		program.m_shaders.push_back({GL_VERTEX_SHADER, vshader.c_str(), std::filesystem::last_write_time(vshader.c_str())});

		if (fsfile != nullptr && strcmp(fsfile, "") != 0)
		{
			std::string fshader = GetShaderFullPath(fsfile);
			program.m_shaders.push_back({GL_FRAGMENT_SHADER, fshader.c_str(), std::filesystem::last_write_time(fshader.c_str())});
		}

		program.Build();

		g_programsByName[name] = program;
	}

	return &g_programsByName[name];
}

Program* Program::MakeCompute(const char* name, const char* csfile, const StringArray& defines)
{
	if (!g_programsByName.contains(name))
	{
		Program program(name);

		program.m_defines   = defines;
		std::string cshader = GetShaderFullPath(csfile);

		program.m_shaders.push_back({GL_COMPUTE_SHADER, cshader.c_str(), std::filesystem::last_write_time(cshader.c_str())});
		program.Build();

		g_programsByName[name] = program;
	}

	return &g_programsByName[name];
}

Program* Program::GetProgramByName(const char* name)
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

Program::Program(const char* name)
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

void Program::SetUniform(const char* name, int32_t value) const
{
	glUniform1i(GetLocation(name), value);
}
void Program::SetUniform(const char* name, u32 value) const
{
	glUniform1ui(GetLocation(name), value);
}
void Program::SetUniform(const char* name, f32 value) const
{
	glUniform1f(GetLocation(name), value);
}
void Program::SetUniform(const char* name, const glm::vec2& value) const
{
	glUniform2fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const char* name, const glm::vec3& value) const
{
	glUniform3fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const char* name, const glm::vec4& value) const
{
	glUniform4fv(GetLocation(name), 1, &value[0]);
}
void Program::SetUniform(const char* name, const glm::mat2& value) const
{
	glUniformMatrix2fv(GetLocation(name), 1, false, &value[0][0]);
}
void Program::SetUniform(const char* name, const glm::mat3& value) const
{
	glUniformMatrix3fv(GetLocation(name), 1, false, &value[0][0]);
}
void Program::SetUniform(const char* name, const glm::mat4& value) const
{
	glUniformMatrix4fv(GetLocation(name), 1, false, &value[0][0]);
}

void Program::Build()
{
	std::vector<GLuint> shaders;

	bool allValid = true;

	for (const auto& shader : m_shaders)
	{
		GLuint shaderID = CompileShader(shader.filename.data(), shader.type, m_defines);
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
	m_uniforms.clear();

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

GLint Program::GetLocation(const char* name) const
{
	auto it = m_uniforms.find(name);
	return (it == m_uniforms.end()) ? -1 : it->second;
}

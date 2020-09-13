#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

#include <vector>
#include <filesystem>
#include <chrono>
#include <unordered_map>

#include <stdio.h>

constexpr float Pi        = 3.14159265359f;
constexpr float Tau       = 2.0f * Pi;
constexpr float ToRadians = Pi / 180.0f;
constexpr float ToDegrees = 180.0f / Pi;

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void MouseMoveCallback(GLFWwindow* window, double x, double y);
static void WheelCallback(GLFWwindow* window, double x, double y);

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

static int g_width, g_height;

template <typename T>
inline constexpr T Min(T a, T b)
{
	return a < b ? a : b;
}

template <typename T>
inline constexpr T Max(T a, T b)
{
	return a > b ? a : b;
}

template <typename T>
inline constexpr T Clamp(T x, T a, T b)
{
	return Min(b, Max(x, a));
}

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texcoord;
};

struct Camera
{
	float phi      = 0.0f;
	float theta    = 90.0f;
	float distance = 1.0f;

	glm::mat4 GetView()
	{
		const float x = distance * sinf(theta * ToRadians) * sinf(phi * ToRadians);
		const float y = distance * cosf(theta * ToRadians);
		const float z = distance * sinf(theta * ToRadians) * cosf(phi * ToRadians);

		const auto pos    = glm::vec3(x, y, z);
		const auto center = glm::vec3(0.0f, 0.0f, 0.0f);
		const auto up     = glm::vec3(0.0f, 1.0f, 0.0f);

		return glm::lookAt(pos, center, up);
	}
};

uint32_t CompileShader(const char* filename, GLenum shaderType, const std::vector<const char*>& defines)
{
	FILE* file = fopen(filename, "r");
	if (!file) { return 0; }

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
	for (const auto& define : defines) { completeShader.push_back(std::string("#define ") + define + "\n"); }
	completeShader.push_back(std::move(src));

	char** finalSrc = new char*[completeShader.size()];
	for (size_t i = 0; i < completeShader.size(); ++i)
	{
		finalSrc[i] = new char[completeShader[i].size() + 1];
		strcpy(finalSrc[i], completeShader[i].data());
	}

	uint32_t shader = glCreateShader(shaderType);

	glShaderSource(shader, completeShader.size(), finalSrc, nullptr);
	glCompileShader(shader);

	int compiled = GL_FALSE;
	glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);

	for (size_t i = 0; i < completeShader.size(); ++i) { delete[] finalSrc[i]; }
	delete[] finalSrc;

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

struct shader
{
	GLenum                          type;
	const char*                     filename;
	std::filesystem::file_time_type time;
};

class Program
{
public:
	static Program MakeRender(const char*                     name,
	                          const char*                     vsfile,
	                          const char*                     fsfile  = nullptr,
	                          const std::vector<const char*>& defines = std::vector<const char*>())
	{
		auto program = Program{name};

		program.m_defines = defines;
		program.m_shaders.push_back({GL_VERTEX_SHADER, vsfile, std::filesystem::last_write_time(vsfile)});

		if (fsfile != nullptr) { program.m_shaders.push_back({GL_FRAGMENT_SHADER, fsfile, std::filesystem::last_write_time(fsfile)}); }

		program.Build();

		return program;
	}

	static Program MakeCompute(const char* name, const char* csfile)
	{
		auto program = Program{name};

		program.m_shaders.push_back({GL_COMPUTE_SHADER, csfile, std::filesystem::last_write_time(csfile)});
		program.Build();

		return program;
	}

	explicit Program(const char* name = "")
	    : m_name(name)
	{
	}

	void Update()
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

		if (needsUpdate) { Build(); }
	}

	void Bind() { glUseProgram(m_id); }

	void SetUniform(const char* name, int32_t value) const { glUniform1i(GetLocation(name), value); }
	void SetUniform(const char* name, uint32_t value) const { glUniform1ui(GetLocation(name), value); }
	void SetUniform(const char* name, float value) const { glUniform1f(GetLocation(name), value); }
	void SetUniform(const char* name, const glm::vec2& value) const { glUniform2fv(GetLocation(name), 1, &value[0]); }
	void SetUniform(const char* name, const glm::vec3& value) const { glUniform3fv(GetLocation(name), 1, &value[0]); }
	void SetUniform(const char* name, const glm::vec4& value) const { glUniform4fv(GetLocation(name), 1, &value[0]); }
	void SetUniform(const char* name, const glm::mat2& value) const { glUniformMatrix2fv(GetLocation(name), 1, false, &value[0][0]); }
	void SetUniform(const char* name, const glm::mat3& value) const { glUniformMatrix3fv(GetLocation(name), 1, false, &value[0][0]); }
	void SetUniform(const char* name, const glm::mat4& value) const { glUniformMatrix4fv(GetLocation(name), 1, false, &value[0][0]); }

private:
	void Build()
	{
		std::vector<GLuint> shaders;

		for (const auto& shader : m_shaders) { shaders.push_back(CompileShader(shader.filename, shader.type, m_defines)); }

		GLuint program = glCreateProgram();

		for (GLuint shader : shaders)
		{
			if (glIsShader(shader)) { glAttachShader(program, shader); }
		}

		glLinkProgram(program);

		int linked = GL_FALSE;
		glGetProgramiv(program, GL_LINK_STATUS, &linked);

		if (!linked)
		{
			char error[512];
			glGetProgramInfoLog(program, 512, nullptr, error);
			fprintf(stderr, "Could not link program %s: %s\n", m_name, error);
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

	void GetUniformInfos()
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

	GLint GetLocation(const char* name) const { return m_uniforms.contains(name) ? m_uniforms.at(name) : -1; }

private:
	using Shader  = std::pair<const char*, GLenum>;
	using Shaders = std::vector<Shader>;

	using ShaderTime  = std::pair<const char*, std::filesystem::file_time_type>;
	using ShaderTimes = std::vector<ShaderTime>;

	const char* m_name = nullptr;

	std::unordered_map<std::string, GLint> m_uniforms;

	GLuint                   m_id = 0;
	std::vector<shader>      m_shaders;
	std::vector<const char*> m_defines;
};

static std::unordered_map<uint32_t, Program>   g_programs;
static std::unordered_map<const char*, GLuint> g_textures;

GLuint LoadTexture(const char* filename)
{
	if (!g_textures.contains(filename))
	{
		stbi_set_flip_vertically_on_load(true);

		int      w, h, c;
		uint8_t* data = stbi_load(filename, &w, &h, &c, 0);

		if (data == nullptr) { return 0; }

		GLuint texture;
		glCreateTextures(GL_TEXTURE_2D, 1, &texture);

		const int levels = log2f(Min(w, h));

		GLenum format, internalFormat;
		switch (c)
		{
			case 1:
				format         = GL_R;
				internalFormat = GL_R8;
				break;
			case 2:
				format         = GL_RG;
				internalFormat = GL_RG8;
				break;
			case 3:
				format         = GL_RGB;
				internalFormat = GL_RGB8;
				break;
			case 4:
				format         = GL_RGBA;
				internalFormat = GL_RGBA8;
				break;
		}

		glTextureStorage2D(texture, levels, internalFormat, w, h);
		glTextureSubImage2D(texture, 0, 0, 0, w, h, format, GL_UNSIGNED_BYTE, data);
		glGenerateTextureMipmap(texture);

		stbi_image_free(data);

		g_textures[filename] = texture;
	}

	return g_textures[filename];
}

struct Material
{
	Material(const char* matName, const char* baseVS, const char* baseFS)
	    : m_name(matName)
	    , m_baseVS(baseVS)
	    , m_baseFS(baseFS)
	{
	}

	uint32_t GetMask() const
	{
		// clang-format off
		const uint32_t result = 0
			| m_hasAlbedo              << 0
			| m_hasAlbedoTexture       << 1
			| m_hasRoughness           << 2
			| m_hasRoughnessTexture    << 3
			| m_hasMetallic            << 4
			| m_hasMetallicTexture     << 5
			| m_hasEmissive            << 6
			| m_hasEmissiveTexture     << 7
			| m_hasNormalMap           << 8
			| m_hasAmbientOcclusionMap << 9;

		// clang-format on
		return result;
	}

	void Bind(Program* program)
	{
		if (m_hasAlbedo) program->SetUniform("u_albedo", m_albedo);
		if (m_hasRoughness) program->SetUniform("u_roughness", m_roughness);
		if (m_hasMetallic) program->SetUniform("u_metallic", m_metallic);
		if (m_hasEmissive) program->SetUniform("u_emissive", m_emissive);

		GLuint index = 0;

		if (m_hasAlbedoTexture)
		{
			glBindTextureUnit(index, m_albedoTexture);
			program->SetUniform("s_albedo", index);
			++index;
		}

		if (m_hasRoughnessTexture)
		{
			glBindTextureUnit(index, m_roughnessTexture);
			program->SetUniform("s_roughness", index);
			++index;
		}

		if (m_hasMetallicTexture)
		{
			glBindTextureUnit(index, m_metallicTexture);
			program->SetUniform("s_metallic", index);
			++index;
		}

		if (m_hasEmissiveTexture)
		{
			glBindTextureUnit(index, m_emissiveTexture);
			program->SetUniform("s_emissive", index);
			++index;
		}

		if (m_hasNormalMap)
		{
			glBindTextureUnit(index, m_normalMap);
			program->SetUniform("s_normal", index);
			++index;
		}

		if (m_hasAmbientOcclusionMap)
		{
			glBindTextureUnit(index, m_ambientOcclusionMap);
			program->SetUniform("s_ambientOcclusion", index);
			++index;
		}
	}

	void SetAlbedo(const glm::vec3& albedo)
	{
		m_hasAlbedo = true;
		m_albedo    = albedo;
	}
	void RemoveAlbedo() { m_hasAlbedo = false; }

	void SetRoughness(float roughness)
	{
		m_hasRoughness = true;
		m_roughness    = roughness;
	}
	void RemoveRoughness() { m_hasRoughness = false; }

	void SetMetallic(float metallic)
	{
		m_hasMetallic = true;
		m_metallic    = metallic;
	}
	void RemoveMetallic() { m_hasMetallic = false; }

	void SetEmissive(const glm::vec3& emissive)
	{
		m_hasEmissive = true;
		m_emissive    = emissive;
	}
	void RemoveEmissive() { m_hasEmissive = false; }

	void SetAlbedoTexture(const char* filename)
	{
		m_hasAlbedoTexture = true;
		m_albedoTexture    = LoadTexture(filename);
	}
	void RemoveAlbedoTexture() { m_hasAlbedoTexture = false; }

	void SetRoughnessTexture(const char* filename)
	{
		m_hasRoughnessTexture = true;
		m_roughnessTexture    = LoadTexture(filename);
	}
	void RemoveRoughnessTexture() { m_roughnessTexture = false; }

	void SetMetallicTexture(const char* filename)
	{
		m_hasMetallicTexture = true;
		m_metallicTexture    = LoadTexture(filename);
	}
	void RemoveMetallicTexture() { m_hasMetallicTexture = false; }

	void SetEmissiveTexture(const char* filename)
	{
		m_hasEmissiveTexture = true;
		m_emissiveTexture    = LoadTexture(filename);
	}
	void RemoveEmissiveTexture() { m_hasMetallicTexture = false; }

	void SetNormalMap(const char* filename)
	{
		m_hasNormalMap = true;
		m_normalMap    = LoadTexture(filename);
	}
	void RemoveNormalMap() { m_hasNormalMap = false; }

	void SetAmbientOcclusionMap(const char* filename)
	{
		m_hasAmbientOcclusionMap = true;
		m_ambientOcclusionMap    = LoadTexture(filename);
	}
	void RemoveAmbientOcclusionMap() { m_hasAmbientOcclusionMap = false; }

	Program BuildProgram() { return Program::MakeRender(m_name, m_baseVS, m_baseFS, GetDefines()); }

private:
	std::vector<const char*> GetDefines()
	{
		if (m_defines.empty())
		{
			if (m_hasAlbedo) { m_defines.push_back("HAS_ALBEDO"); }
			if (m_hasAlbedoTexture) { m_defines.push_back("HAS_ALBEDO_TEXTURE"); }
			if (m_hasRoughness) { m_defines.push_back("HAS_ROUGHNESS"); }
			if (m_hasRoughnessTexture) { m_defines.push_back("HAS_ROUGHNESS_TEXTURE"); }
			if (m_hasMetallic) { m_defines.push_back("HAS_METALLIC"); }
			if (m_hasMetallicTexture) { m_defines.push_back("HAS_METALLIC_TEXTURE"); }
			if (m_hasEmissive) { m_defines.push_back("HAS_EMISSIVE"); }
			if (m_hasEmissiveTexture) { m_defines.push_back("HAS_EMISSIVE_TEXTURE"); }
			if (m_hasNormalMap) { m_defines.push_back("HAS_NORMAL_MAP"); }
			if (m_hasAmbientOcclusionMap) { m_defines.push_back("HAS_AMBIENT_OCCLUSION_MAP"); }
		}

		return m_defines;
	}

private:
	const char* m_name;
	const char* m_baseVS;
	const char* m_baseFS;

	glm::vec3 m_albedo;
	float     m_roughness;
	float     m_metallic;
	glm::vec3 m_emissive;

	GLuint m_albedoTexture;
	GLuint m_roughnessTexture;
	GLuint m_metallicTexture;
	GLuint m_emissiveTexture;
	GLuint m_normalMap;
	GLuint m_ambientOcclusionMap;

	std::vector<const char*> m_defines;

	bool m_hasAlbedo              = false;
	bool m_hasRoughness           = false;
	bool m_hasMetallic            = false;
	bool m_hasEmissive            = false;
	bool m_hasAlbedoTexture       = false;
	bool m_hasRoughnessTexture    = false;
	bool m_hasMetallicTexture     = false;
	bool m_hasEmissiveTexture     = false;
	bool m_hasNormalMap           = false;
	bool m_hasAmbientOcclusionMap = false;
	bool m_padding0;
	bool m_padding1;
	bool m_padding2;
	bool m_padding3;
	bool m_padding4;
	bool m_padding5;
};

struct RenderContext
{
	glm::mat4 model, view, proj;
};

void SetupShader(RenderContext* context, Material* material)
{
	const uint32_t mask = material->GetMask();

	if (!g_programs.contains(mask))
	{
		Program program = material->BuildProgram();
		g_programs.emplace(std::make_pair(mask, program));
	}

	Program* program = &g_programs[mask];

	program->Bind();

	program->SetUniform("u_model", context->model);
	program->SetUniform("u_view", context->view);
	program->SetUniform("u_proj", context->proj);

	material->Bind(program);
}

struct Mesh
{
	GLuint  vao, vbo, ibo;
	GLsizei indexCount;

	Mesh(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices)
	{
		glCreateVertexArrays(1, &vao);

		glCreateBuffers(1, &vbo);
		glNamedBufferStorage(vbo, (GLsizeiptr)vertices.size() * sizeof(Vertex), vertices.data(), GL_DYNAMIC_STORAGE_BIT);

		glCreateBuffers(1, &ibo);
		glNamedBufferStorage(ibo, (GLsizeiptr)indices.size() * sizeof(GLuint), indices.data(), GL_DYNAMIC_STORAGE_BIT);

		glVertexArrayVertexBuffer(vao, 0, vbo, 0, sizeof(Vertex));
		glVertexArrayElementBuffer(vao, ibo);

		glEnableVertexArrayAttrib(vao, 0);
		glEnableVertexArrayAttrib(vao, 1);
		glEnableVertexArrayAttrib(vao, 2);

		glVertexArrayAttribFormat(vao, 0, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, position));
		glVertexArrayAttribFormat(vao, 1, 3, GL_FLOAT, GL_FALSE, offsetof(Vertex, normal));
		glVertexArrayAttribFormat(vao, 2, 2, GL_FLOAT, GL_FALSE, offsetof(Vertex, texcoord));

		glVertexArrayAttribBinding(vao, 0, 0);
		glVertexArrayAttribBinding(vao, 1, 0);
		glVertexArrayAttribBinding(vao, 2, 0);

		indexCount = (GLsizei)indices.size();
	}

	void Draw()
	{
		glBindVertexArray(vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ibo);
		glDrawElements(GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr);
	}
};

struct Model
{
	Material* material;
	Mesh*     mesh;

	glm::mat4 worldTransform;

	void Draw(RenderContext* context)
	{
		context->model = worldTransform;
		SetupShader(context, material);
		mesh->Draw();
	}
};

static Camera g_camera;

int main()
{
	glfwInit();

	glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
	glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_FALSE);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
	glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
	// TODO: Proper multisampling
	// glfwWindowHint(GLFW_SAMPLES, 8);
#if _DEBUG
	glfwWindowHint(GLFW_OPENGL_DEBUG_CONTEXT, GLFW_TRUE);
#endif

	GLFWwindow* window = glfwCreateWindow(1024, 768, "Viewer", nullptr, nullptr);

	glfwSetKeyCallback(window, KeyCallback);
	glfwSetMouseButtonCallback(window, MouseButtonCallback);
	glfwSetCursorPosCallback(window, MouseMoveCallback);
	glfwSetScrollCallback(window, WheelCallback);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
	// glfwSetDropCallback(window, DropCallback);

	glfwMakeContextCurrent(window);

	glfwGetFramebufferSize(window, &g_width, &g_height);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) { return 1; }

	std::vector<Vertex> vertices = {
	    {{-0.5, -0.5, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
	    {{0.5, -0.5, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0}},
	    {{0.5, 0.5, 0.0}, {0.0, 0.0, 0.0}, {1.0, 1.0}},
	    {{-0.5, 0.5, 0.0}, {0.0, 0.0, 0.0}, {0.0, 1.0}},
	};

	std::vector<GLuint> indices = {0, 1, 2, 0, 2, 3};

	Mesh mesh(vertices, indices);

	auto material1 = Material("test", "resources/shaders/test.vert", "resources/shaders/test.frag");
	material1.SetAlbedoTexture("resources/textures/grin.png");
	material1.SetAlbedo(glm::vec3(1.0, 1.0, 0.0));

	auto material2 = Material("test", "resources/shaders/test.vert", "resources/shaders/test.frag");
	material2.SetAlbedoTexture("resources/textures/image.png");
	material2.SetAlbedo(glm::vec3(1.0, 0.0, 0.0));

	std::vector<Model> models;
	models.push_back({
	    .material       = &material1,
	    .mesh           = &mesh,
	    .worldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.75f, 0.0f, 0.0f)),
	});

	models.push_back({
	    .material       = &material2,
	    .mesh           = &mesh,
	    .worldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.75f, 0.0f, 0.0f)),
	});

	while (!glfwWindowShouldClose(window))
	{
		glfwPollEvents();

		for (auto&& program : g_programs) { program.second.Update(); }

		glViewport(0, 0, g_width, g_height);

		glClearColor(0.1f, 0.6f, 0.4f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

		RenderContext context = {
		    .view = g_camera.GetView(),
		    .proj = glm::perspective(60.0f * ToRadians, (float)g_width / g_height, 0.1f, 100.0f),
		};

		for (auto&& model : models) { model.Draw(&context); }

		glfwSwapBuffers(window);
	}

	glfwTerminate();

	return 0;
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE) { glfwSetWindowShouldClose(window, GLFW_TRUE); }
}

bool   g_cameraMoving = false;
double g_mouseX, g_mouseY;

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT)
	{
		if (action == GLFW_PRESS)
		{
			g_cameraMoving = true;
			glfwGetCursorPos(window, &g_mouseX, &g_mouseY);
		}
		else
		{
			g_cameraMoving = false;
		}
	}
}

static void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
	if (g_cameraMoving)
	{
		const double dx = 0.1f * (x - g_mouseX);
		const double dy = 0.1f * (y - g_mouseY);

		g_camera.phi += dx;
		g_camera.theta = Clamp(g_camera.theta + (float)dy, 10.0f, 170.0f);

		g_mouseX = x;
		g_mouseY = y;
	}
}

static void WheelCallback(GLFWwindow* window, double x, double y)
{
	constexpr float minDistance = 0.2f;
	constexpr float maxDistance = 100.0f;

	const float multiplier = 2.5f * (g_camera.distance - minDistance) / (maxDistance - minDistance);

	const float distance = g_camera.distance - (float)y * multiplier;

	g_camera.distance = Clamp(distance, 0.2f, 100.0f);
}

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	g_width  = width;
	g_height = height;
}
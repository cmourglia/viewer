#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

#include <tiny_gltf.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "imfilebrowser.h"

#include <vector>
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>

#include <stdio.h>

constexpr float Pi        = 3.14159265359f;
constexpr float Tau       = 2.0f * Pi;
constexpr float ToRadians = Pi / 180.0f;
constexpr float ToDegrees = 180.0f / Pi;

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
static void MouseMoveCallback(GLFWwindow* window, double x, double y);
static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
static void WheelCallback(GLFWwindow* window, double x, double y);

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height);

static void DropCallback(GLFWwindow* window, int count, const char** paths);

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
		completeShader.push_back(std::string("#define ") + define + "\n");
	}
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

	for (size_t i = 0; i < completeShader.size(); ++i)
	{
		delete[] finalSrc[i];
	}
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

		if (fsfile != nullptr)
		{
			program.m_shaders.push_back({GL_FRAGMENT_SHADER, fsfile, std::filesystem::last_write_time(fsfile)});
		}

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

		if (needsUpdate)
		{
			Build();
		}
	}

	void Bind()
	{
		glUseProgram(m_id);
	}

	void SetUniform(const char* name, int32_t value) const
	{
		glUniform1i(GetLocation(name), value);
	}
	void SetUniform(const char* name, uint32_t value) const
	{
		glUniform1ui(GetLocation(name), value);
	}
	void SetUniform(const char* name, float value) const
	{
		glUniform1f(GetLocation(name), value);
	}
	void SetUniform(const char* name, const glm::vec2& value) const
	{
		glUniform2fv(GetLocation(name), 1, &value[0]);
	}
	void SetUniform(const char* name, const glm::vec3& value) const
	{
		glUniform3fv(GetLocation(name), 1, &value[0]);
	}
	void SetUniform(const char* name, const glm::vec4& value) const
	{
		glUniform4fv(GetLocation(name), 1, &value[0]);
	}
	void SetUniform(const char* name, const glm::mat2& value) const
	{
		glUniformMatrix2fv(GetLocation(name), 1, false, &value[0][0]);
	}
	void SetUniform(const char* name, const glm::mat3& value) const
	{
		glUniformMatrix3fv(GetLocation(name), 1, false, &value[0][0]);
	}
	void SetUniform(const char* name, const glm::mat4& value) const
	{
		glUniformMatrix4fv(GetLocation(name), 1, false, &value[0][0]);
	}

private:
	void Build()
	{
		std::vector<GLuint> shaders;

		for (const auto& shader : m_shaders)
		{
			shaders.push_back(CompileShader(shader.filename, shader.type, m_defines));
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

	GLint GetLocation(const char* name) const
	{
		return m_uniforms.contains(name) ? m_uniforms.at(name) : -1;
	}

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

		stbi_set_flip_vertically_on_load(false);

		if (data == nullptr)
		{
			return 0;
		}

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

	void Bind(Program* program)
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
	}

	Program BuildProgram()
	{
		return Program::MakeRender(m_name, m_baseVS, m_baseFS, GetDefines());
	}

private:
	std::vector<const char*> GetDefines()
	{
		std::vector<const char*> defines;

		if (hasAlbedo)
		{
			defines.push_back("HAS_ALBEDO");
		}
		if (hasAlbedoTexture)
		{
			defines.push_back("HAS_ALBEDO_TEXTURE");
		}
		if (hasRoughness)
		{
			defines.push_back("HAS_ROUGHNESS");
		}
		if (hasRoughnessTexture)
		{
			defines.push_back("HAS_ROUGHNESS_TEXTURE");
		}
		if (hasMetallic)
		{
			defines.push_back("HAS_METALLIC");
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

private:
	const char* m_name;
	const char* m_baseVS;
	const char* m_baseFS;

public:
	glm::vec3 albedo;
	float     roughness;
	float     metallic;
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

struct RenderContext
{
	glm::mat4 model, view, proj;

	glm::vec3 lightDirection;
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

enum DataType
{
	DataType_Byte          = GL_BYTE,
	DataType_UnsignedByte  = GL_UNSIGNED_BYTE,
	DataType_Short         = GL_SHORT,
	DataType_UnsignedShort = GL_UNSIGNED_SHORT,
	DataType_Int           = GL_INT,
	DataType_UnsignedInt   = GL_UNSIGNED_INT,
	DataType_HalfFloat     = GL_HALF_FLOAT,
	DataType_Float         = GL_FLOAT,
};

enum ElementType
{
	ElementType_Scalar = 1,
	ElementType_Vec2   = 2,
	ElementType_Vec3   = 3,
	ElementType_Vec4   = 4,
};

enum BindingPoint
{
	BindingPoint_Position  = 0,
	BindingPoint_Normal    = 1,
	BindingPoint_Tangent   = 2,
	BindingPoint_Texcoord0 = 3,
	BindingPoint_Texcoord1 = 4,
	BindingPoint_Texcoord2 = 5,
	BindingPoint_Texcoord3 = 6,
	BindingPoint_Texcoord4 = 7,
	BindingPoint_Color     = 8,
	BindingPoint_Joints    = 9,
	BindingPoint_Weights   = 10,
	BindingPoint_Custom0   = 11,
	BindingPoint_Custom1   = 12,
	BindingPoint_Custom2   = 13,
	BindingPoint_Custom3   = 14,
};

struct LayoutItem
{
	BindingPoint   bindingPoint;
	DataType       dataType;
	ElementType    elementType;
	GLsizeiptr     offset;
	GLsizeiptr     dataSize;
	const GLubyte* data;

	GLsizeiptr GetSize() const
	{
		GLsizeiptr dataSize = 0;

		switch (dataType)
		{
			case DataType_Byte:
			case DataType_UnsignedByte:
				assert(sizeof(GLbyte) == sizeof(GLubyte));
				dataSize = sizeof(GLbyte);
				break;

			case DataType_Short:
			case DataType_UnsignedShort:
			case DataType_HalfFloat:
				assert(sizeof(GLshort) == sizeof(GLushort));
				assert(sizeof(GLshort) == sizeof(GLhalf));
				dataSize = sizeof(GLshort);
				break;

			case DataType_Int:
			case DataType_UnsignedInt:
			case DataType_Float:
				assert(sizeof(GLint) == sizeof(GLuint));
				assert(sizeof(GLint) == sizeof(GLfloat));
				dataSize = sizeof(GLint);
				break;
		}

		return dataSize * (GLsizeiptr)elementType;
	}
};

using Layout = std::vector<LayoutItem>;

struct VertexDataInfos
{
	Layout     layout;
	GLuint     byteStride;
	GLsizeiptr bufferSize;
	bool       interleaved;
	bool       singleBuffer;
};

struct IndexDataInfos
{
	GLsizeiptr     bufferSize;
	GLuint         indexCount;
	GLenum         indexType;
	const GLubyte* data;
};

struct Mesh
{
	GLuint  vao, buffer;
	GLsizei indexCount;
	GLenum  indexType;

	Mesh()
	{
	}

	Mesh(const std::vector<Vertex>& vertices, const std::vector<GLushort>& indices)
	    : indexType(GL_UNSIGNED_SHORT)
	    , indexCount(indices.size())
	{
		SetData(vertices, indices);
	}

	Mesh(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices)
	    : indexType(GL_UNSIGNED_INT)
	    , indexCount(indices.size())
	{
		SetData(vertices, indices);
	}

	Mesh(const VertexDataInfos& vertexDataInfos, const IndexDataInfos& indexDataInfos)
	    : indexCount(indexDataInfos.indexCount)
	    , indexType(indexDataInfos.indexType)
	{
		GLint alignment = GL_NONE;
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

		glCreateVertexArrays(1, &vao);

		const GLsizeiptr alignedIndexSize = AlignedSize(indexDataInfos.bufferSize, alignment);

		GLsizeiptr alignedVertexSize = 0;

		if (vertexDataInfos.singleBuffer)
		{
			alignedVertexSize = AlignedSize(vertexDataInfos.bufferSize, alignment);
		}
		else
		{
			for (const auto& entry : vertexDataInfos.layout)
			{
				alignedVertexSize += AlignedSize(entry.dataSize, alignment);
			}
		}

		glCreateBuffers(1, &buffer);
		glNamedBufferStorage(buffer, alignedIndexSize + alignedVertexSize, nullptr, GL_DYNAMIC_STORAGE_BIT);

		glNamedBufferSubData(buffer, 0, indexDataInfos.bufferSize, indexDataInfos.data);
		glVertexArrayElementBuffer(vao, buffer);

		if (vertexDataInfos.singleBuffer)
		{
			int        vboIndex   = 0;
			GLsizeiptr vboBasePtr = alignedIndexSize;

			const GLubyte* data = vertexDataInfos.layout[0].data;
			glNamedBufferSubData(buffer, vboBasePtr, vertexDataInfos.bufferSize, data);

			for (const auto& entry : vertexDataInfos.layout)
			{
				assert(entry.data == data);

				glEnableVertexArrayAttrib(vao, entry.bindingPoint);
				glVertexArrayAttribBinding(vao, entry.bindingPoint, vboIndex);

				if (vertexDataInfos.interleaved)
				{
					glVertexArrayVertexBuffer(vao, vboIndex, buffer, vboBasePtr, vertexDataInfos.byteStride);
					glVertexArrayAttribFormat(vao, entry.bindingPoint, entry.elementType, entry.dataType, GL_FALSE, entry.offset);
				}
				else
				{
					vboBasePtr += entry.offset;

					glVertexArrayVertexBuffer(vao, vboIndex, buffer, vboBasePtr, entry.GetSize());
					glVertexArrayAttribFormat(vao, entry.bindingPoint, entry.elementType, entry.dataType, GL_FALSE, 0);

					++vboIndex;
				}
			}
		}
		else
		{
			int        vboIndex   = 0;
			GLsizeiptr vboBasePtr = alignedIndexSize;

			std::unordered_map<const GLubyte*, GLsizeiptr> insertedData;

			for (const auto& entry : vertexDataInfos.layout)
			{
				if (!insertedData.contains(entry.data))
				{
					glNamedBufferSubData(buffer, vboBasePtr, entry.dataSize, entry.data);

					glVertexArrayVertexBuffer(vao, vboIndex, buffer, vboBasePtr, entry.GetSize());

					insertedData[entry.data] = vboBasePtr;

					vboBasePtr += AlignedSize(entry.dataSize, alignment);
				}
				else
				{
					glVertexArrayVertexBuffer(vao, vboIndex, buffer, insertedData[entry.data] + entry.offset, entry.GetSize());
				}

				glVertexArrayAttribFormat(vao, entry.bindingPoint, entry.elementType, entry.dataType, GL_FALSE, 0);
				glEnableVertexArrayAttrib(vao, entry.bindingPoint);
				glVertexArrayAttribBinding(vao, entry.bindingPoint, vboIndex);

				++vboIndex;
			}
		}
	}

	static GLsizeiptr AlignedSize(GLsizeiptr size, GLsizeiptr align)
	{
		if (size % align == 0)
			return size;
		return size + (align - size % align);
	}

	void SetLayout(const Layout& layout, const std::vector<GLsizeiptr>& offsets)
	{
		assert(offsets.size() == layout.size());
		for (size_t i = 0; i < layout.size(); ++i)
		{
			const auto& entry = layout[i];
			glEnableVertexArrayAttrib(vao, entry.bindingPoint);
			glVertexArrayAttribFormat(vao, entry.bindingPoint, entry.elementType, entry.dataType, GL_FALSE, offsets[i]);
			glVertexArrayAttribBinding(vao, entry.bindingPoint, 0);
		}
	}

	template <typename IndexType>
	void SetData(const std::vector<Vertex>& vertices, const std::vector<IndexType>& indices)
	{
		GLint alignment = GL_NONE;
		glGetIntegerv(GL_UNIFORM_BUFFER_OFFSET_ALIGNMENT, &alignment);

		glCreateVertexArrays(1, &vao);

		const GLsizeiptr indexSize        = indices.size() * sizeof(IndexType);
		const GLsizeiptr alignedIndexSize = AlignedSize(indexSize, alignment);

		const GLsizeiptr vertexSize        = vertices.size() * sizeof(Vertex);
		const GLsizeiptr alignedVertexSize = AlignedSize(vertexSize, alignment);

		glCreateBuffers(1, &buffer);
		glNamedBufferStorage(buffer, alignedIndexSize + alignedVertexSize, nullptr, GL_DYNAMIC_STORAGE_BIT);

		glNamedBufferSubData(buffer, 0, indexSize, indices.data());
		glNamedBufferSubData(buffer, alignedIndexSize, vertexSize, vertices.data());

		glVertexArrayVertexBuffer(vao, 0, buffer, alignedIndexSize, sizeof(Vertex));
		glVertexArrayElementBuffer(vao, buffer);

		static const Layout layout = {{BindingPoint_Position, DataType_Float, ElementType_Vec3},
		                              {BindingPoint_Normal, DataType_Float, ElementType_Vec3},
		                              {BindingPoint_Texcoord0, DataType_Float, ElementType_Vec2}};

		SetLayout(layout, {offsetof(Vertex, position), offsetof(Vertex, normal), offsetof(Vertex, texcoord)});
	}

	void Draw()
	{
		glBindVertexArray(vao);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
		glDrawElements(GL_TRIANGLES, indexCount, indexType, nullptr);
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

void SetupUI(GLFWwindow* window);
void RenderUI(std::vector<Model>* models);

float g_lastScroll = 0.0f;

GLuint Render(std::vector<Model>* models, const glm::vec2& size);

GLuint fbos[2];
GLuint rts[3];

#define msaaFramebuffer fbos[0]
#define resolveFramebuffer fbos[1]

GLuint msaaRenderTexture;
GLuint msaaDepthRenderBuffer;
GLuint resolveTexture;

double g_viewportX = 0.0, g_viewportY = 0.0;
double g_viewportW = 0.0, g_viewportH = 0.0;

static std::vector<Model> g_models;

void LoadNode(const tinygltf::Model& model, int nodeIdx, const glm::mat4& parentTransform)
{
	tinygltf::Node node = model.nodes[nodeIdx];

	auto AsFloat = [](const std::vector<double>& v) {
		std::vector<float> r(v.size());
		std::transform(v.begin(), v.end(), r.begin(), [](double d) { return (float)d; });
		return r;
	};

	glm::mat4 localTransform(1.0f);
	if (!node.matrix.empty())
	{
		localTransform = glm::make_mat4(AsFloat(node.matrix).data());
	}
	else
	{
		glm::mat4 t(1.0f), r(1.0f), s(1.0f);

		if (!node.translation.empty())
		{
			t = glm::translate(t, glm::make_vec3(AsFloat(node.translation).data()));
		}

		if (!node.rotation.empty())
		{
			auto q = AsFloat(node.rotation);
			r      = glm::mat4_cast(glm::quat(q[3], q[0], q[1], q[2]));
		}

		if (!node.scale.empty())
		{
			s = glm::scale(s, glm::make_vec3(AsFloat(node.scale).data()));
		}

		localTransform = s * r * t;
	}

	glm::mat4 worldTransform = parentTransform * localTransform;

	if (node.mesh != -1)
	{
		tinygltf::Mesh mesh = model.meshes[node.mesh];
		for (const auto& primitive : mesh.primitives)
		{
			Model renderModel;

			// Load mesh
			std::vector<Vertex> vertices;
			std::vector<GLuint> indices;

			if (primitive.mode != GL_TRIANGLES)
			{
				fprintf(stderr, "%d primitive is not handled yet\n", primitive.mode);
				continue;
			}

			GLsizeiptr indexBufferSize;
			GLuint     indexCount;
			GLenum     indexType;

			const unsigned char* indexData;

			tinygltf::Accessor indicesAccessor = model.accessors[primitive.indices];
			indexType                          = indicesAccessor.componentType;
			indexCount                         = indicesAccessor.count;

			auto indexBufferView = model.bufferViews[indicesAccessor.bufferView];

			indexData       = model.buffers[indexBufferView.buffer].data.data() + indexBufferView.byteOffset;
			indexBufferSize = indexBufferView.byteLength;

			Layout                  layout;
			std::vector<GLsizeiptr> offsets;
			GLsizei                 vertexBufferSize       = 0;
			GLuint                  vertexBufferByteStride = 0;
			bool                    singleBuffer           = true;
			bool                    interleaved            = true;

			const unsigned char* vertexData;

			int                  vertexBufferViewIndex = -1;
			tinygltf::BufferView vertexBufferView;

			for (const auto& attribute : primitive.attributes)
			{
				auto GetBindingPoint = [](const std::string& attr) {
					// clang-format off
						 if (attr == "POSITION")   return BindingPoint_Position;
					else if (attr == "NORMAL")     return BindingPoint_Normal;
					else if (attr == "TANGENT")    return BindingPoint_Tangent;
					else if (attr == "TEXCOORD_0") return BindingPoint_Texcoord0;
					else if (attr == "TEXCOORD_1") return BindingPoint_Texcoord1;
					else if (attr == "WEIGHTS_0")  return BindingPoint_Weights;
					else if (attr == "JOINTS_0")   return BindingPoint_Joints;
					// clang-format on

					fprintf(stderr, "Binding point not handled: %s\n", attr.c_str());
					return BindingPoint_Custom0;
				};

				auto GetElementType = [](int type) {
					// clang-format off
					switch (type)
					{
						case TINYGLTF_TYPE_SCALAR: return ElementType_Scalar;
						case TINYGLTF_TYPE_VEC2:   return ElementType_Vec2;
						case TINYGLTF_TYPE_VEC3:   return ElementType_Vec3;
						case TINYGLTF_TYPE_VEC4:   return ElementType_Vec4;
					}
					// clang-format on
					fprintf(stderr, "Element type not handled: %d\n", type);
					return ElementType_Scalar;
				};

				tinygltf::Accessor accessor = model.accessors[attribute.second];

				if (accessor.bufferView != vertexBufferViewIndex)
				{
					if (vertexBufferViewIndex >= 0)
					{
						singleBuffer = false;
						interleaved  = false;
					}

					vertexBufferViewIndex  = accessor.bufferView;
					vertexBufferView       = model.bufferViews[vertexBufferViewIndex];
					vertexData             = model.buffers[vertexBufferView.buffer].data.data() + vertexBufferView.byteOffset;
					vertexBufferByteStride = vertexBufferView.byteStride;

					vertexBufferSize += vertexBufferView.byteLength;
				}

				if (accessor.byteOffset >= vertexBufferView.byteStride)
				{
					interleaved = false;
				}

				LayoutItem entry;
				entry.bindingPoint = GetBindingPoint(attribute.first);
				entry.elementType  = GetElementType(accessor.type);
				entry.dataType     = (DataType)accessor.componentType;
				entry.offset       = accessor.byteOffset;
				entry.dataSize     = vertexBufferView.byteLength;
				entry.data         = vertexData;

				//    vertexData);
				layout.push_back(entry);
			}

			VertexDataInfos vertexInfos = {
			    .layout       = layout,
			    .byteStride   = vertexBufferByteStride,
			    .bufferSize   = vertexBufferSize,
			    .interleaved  = interleaved,
			    .singleBuffer = singleBuffer,
			};

			IndexDataInfos indexInfos = {
			    .bufferSize = indexBufferSize,
			    .indexCount = indexCount,
			    .indexType  = indexType,
			    .data       = indexData,
			};

			Mesh* renderMesh = new Mesh(vertexInfos, indexInfos);

			// Load material

			Material* renderMaterial;

			if (primitive.material != -1)
			{
				tinygltf::Material material = model.materials[primitive.material];

				renderMaterial = new Material(material.name.c_str(), "resources/shaders/test.vert", "resources/shaders/test.frag");
				renderMaterial->hasAlbedo    = true;
				renderMaterial->albedo       = glm::make_vec3(AsFloat(material.pbrMetallicRoughness.baseColorFactor).data());
				renderMaterial->hasMetallic  = true;
				renderMaterial->hasRoughness = true;
				renderMaterial->metallic     = material.pbrMetallicRoughness.metallicFactor;
				renderMaterial->roughness    = material.pbrMetallicRoughness.roughnessFactor;
				renderMaterial->hasEmissive  = true;
				renderMaterial->emissive     = glm::make_vec3(AsFloat(material.emissiveFactor).data());

				const auto& albedoTextureInfo            = material.pbrMetallicRoughness.baseColorTexture;
				const auto& metallicRoughnessTextureInfo = material.pbrMetallicRoughness.metallicRoughnessTexture;
				const auto& normalTextureInfo            = material.normalTexture;
				const auto& occlusionTextureInfo         = material.occlusionTexture;
				const auto& emissiveTextureInfo          = material.emissiveTexture;

				auto CreateTexture = [](const tinygltf::Model& model, int textureIndex) {
					auto t       = model.textures[textureIndex];
					auto image   = model.images[t.source];
					auto sampler = t.sampler != -1 ? model.samplers[t.sampler] : tinygltf::Sampler();

					GLuint texture;
					glCreateTextures(GL_TEXTURE_2D, 1, &texture);

					const bool hasMips = (sampler.minFilter != TINYGLTF_TEXTURE_FILTER_NEAREST);

					const int levels = hasMips ? log2f(Min(image.width, image.height)) : 1;

					GLenum format, internalFormat;
					assert(image.pixel_type == TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE);
					switch (image.component)
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

					glTextureStorage2D(texture, levels, internalFormat, image.width, image.height);
					glTextureSubImage2D(texture, 0, 0, 0, image.width, image.height, format, image.pixel_type, image.image.data());

					glTextureParameteri(texture, GL_TEXTURE_WRAP_S, sampler.wrapS);
					glTextureParameteri(texture, GL_TEXTURE_WRAP_R, sampler.wrapR);
					glTextureParameteri(texture, GL_TEXTURE_MIN_FILTER, sampler.minFilter != -1 ? sampler.minFilter : GL_LINEAR);
					glTextureParameteri(texture, GL_TEXTURE_MAG_FILTER, sampler.magFilter != -1 ? sampler.magFilter : GL_LINEAR);

					if (hasMips)
					{
						glGenerateTextureMipmap(texture);
					}

					return texture;
				};

				if (albedoTextureInfo.index != -1)
				{
					assert(albedoTextureInfo.texCoord == 0);
					renderMaterial->hasAlbedoTexture = true;

					renderMaterial->albedoTexture = CreateTexture(model, albedoTextureInfo.index);
				}

				if (metallicRoughnessTextureInfo.index != -1)
				{
					assert(metallicRoughnessTextureInfo.texCoord == 0);
					renderMaterial->hasMetallicRoughnessTexture = true;

					renderMaterial->metallicRoughnessTexture = CreateTexture(model, metallicRoughnessTextureInfo.index);
				}

				if (normalTextureInfo.index != -1)
				{
					assert(normalTextureInfo.texCoord == 0);
					renderMaterial->hasNormalMap = true;
					renderMaterial->normalMap    = CreateTexture(model, normalTextureInfo.index);

					assert(normalTextureInfo.scale == 1.0f);
				}

				if (occlusionTextureInfo.index != -1)
				{
					assert(occlusionTextureInfo.texCoord == 0);
					renderMaterial->hasAmbientOcclusionMap = true;
					renderMaterial->ambientOcclusionMap    = CreateTexture(model, normalTextureInfo.index);
				}

				if (emissiveTextureInfo.index != -1)
				{
					assert(emissiveTextureInfo.texCoord == 0);
					renderMaterial->hasEmissiveTexture = true;
					renderMaterial->emissiveTexture    = CreateTexture(model, emissiveTextureInfo.index);
				}
			}
			else
			{
				renderMaterial            = new Material("dummy", "resources/shaders/test.vert", "resources/shaders/test.frag");
				renderMaterial->hasAlbedo = true;
				renderMaterial->albedo    = glm::vec3(0.2f, 0.5f, 0.4f);
			}

			renderModel.mesh           = renderMesh;
			renderModel.material       = renderMaterial;
			renderModel.worldTransform = worldTransform;

			g_models.push_back(renderModel);
		}
	}

	for (int child : node.children)
	{
		LoadNode(model, child, worldTransform);
	}
}

void LoadScene(const char* gltf)
{
	std::string gltf_str(gltf);
	auto        pos = gltf_str.find_last_of(".");
	auto        ext = gltf_str.substr(pos);

	tinygltf::Model    model;
	tinygltf::TinyGLTF loader;
	std::string        err;
	std::string        warn;

	bool result;

	if (ext == ".gltf")
	{
		result = loader.LoadASCIIFromFile(&model, &err, &warn, gltf_str);
	}
	else if (ext == ".glb")
	{
		result = loader.LoadBinaryFromFile(&model, &err, &warn, gltf_str);
	}
	else
	{
		fprintf(stderr, "%s is not a gltf file\n", gltf);
		return;
	}

	if (!warn.empty())
	{
		fprintf(stderr, "TinyGLTF warning: %s\n", warn.c_str());
	}

	if (!err.empty())
	{
		fprintf(stderr, "TinyGLTF error: %s\n", err.c_str());
	}

	if (!result)
	{
		fprintf(stderr, "Failed to parse GLTF file %s\n", gltf);
		return;
	}

	std::vector<int> nodes;

	if (model.defaultScene != -1)
	{
		nodes = model.scenes[model.defaultScene].nodes;
	}
	else
	{
		if (!model.scenes.empty())
		{
			nodes = model.scenes[0].nodes;
		}
		else
		{
			if (!model.nodes.empty())
			{
				std::unordered_set<int> parentedNodes;
				for (const auto& node : model.nodes)
				{
					for (int child : node.children)
					{
						parentedNodes.insert(child);
					}
				}

				for (int i = 0; i < (int)model.nodes.size(); ++i)
				{
					if (!parentedNodes.contains(i))
					{
						nodes.push_back(i);
					}
				}
			}
		}
	}

	if (nodes.empty())
	{
		fprintf(stderr, "No nodes found, ignoring scene.\n");
		return;
	}

	g_models.clear();

	for (int node : nodes)
	{
		LoadNode(model, node, glm::mat4(1.0f));
	}
}

glm::vec3 g_lightDirection = glm::vec3(0, -1, 0);

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

	GLFWwindow* window = glfwCreateWindow(1920, 1080, "Viewer", nullptr, nullptr);

	glfwSetKeyCallback(window, KeyCallback);
	glfwSetMouseButtonCallback(window, MouseButtonCallback);
	glfwSetCursorPosCallback(window, MouseMoveCallback);
	glfwSetScrollCallback(window, WheelCallback);
	glfwSetFramebufferSizeCallback(window, FramebufferSizeCallback);
	glfwSetDropCallback(window, DropCallback);

	glfwMakeContextCurrent(window);

	glfwGetFramebufferSize(window, &g_width, &g_height);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		return 1;
	}

	SetupUI(window);

	std::vector<Vertex> vertices = {
	    {{-0.5, -0.5, 0.0}, {0.0, 0.0, 0.0}, {0.0, 0.0}},
	    {{0.5, -0.5, 0.0}, {0.0, 0.0, 0.0}, {1.0, 0.0}},
	    {{0.5, 0.5, 0.0}, {0.0, 0.0, 0.0}, {1.0, 1.0}},
	    {{-0.5, 0.5, 0.0}, {0.0, 0.0, 0.0}, {0.0, 1.0}},
	};

	std::vector<GLuint> indices = {0, 1, 2, 0, 2, 3};

	Mesh mesh(vertices, indices);

	auto material1             = Material("test", "resources/shaders/test.vert", "resources/shaders/test.frag");
	material1.hasAlbedoTexture = true;
	material1.albedoTexture    = LoadTexture("resources/textures/grin.png");
	material1.hasAlbedo        = true;
	material1.albedo           = glm::vec3(1.0, 1.0, 0.0);

	auto material2             = Material("test", "resources/shaders/test.vert", "resources/shaders/test.frag");
	material2.hasAlbedoTexture = true;
	material2.albedoTexture    = LoadTexture("resources/textures/image.png");
	material2.hasAlbedo        = true;
	material2.albedo           = glm::vec3(1.0, 0.0, 0.0);

	g_models.push_back({
	    .material       = &material1,
	    .mesh           = &mesh,
	    .worldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(-0.75f, 0.0f, 0.0f)),
	});

	g_models.push_back({
	    .material       = &material2,
	    .mesh           = &mesh,
	    .worldTransform = glm::translate(glm::mat4(1.0f), glm::vec3(0.75f, 0.0f, 0.0f)),
	});

	ImGui::FileBrowser textureDialog;
	textureDialog.SetTitle("Open texture...");
	textureDialog.SetTypeFilters({".png", ".jpg", ".jpeg", ".tiff"});

	while (!glfwWindowShouldClose(window))
	{
		ImGuiIO& io = ImGui::GetIO();

		ImGui_ImplOpenGL3_NewFrame();
		ImGui_ImplGlfw_NewFrame();
		ImGui::NewFrame();

		static bool               showDemo        = true;
		static ImGuiDockNodeFlags dockspace_flags = ImGuiDockNodeFlags_None;

		// We are using the ImGuiWindowFlags_NoDocking flag to make the parent window not dockable into,
		// because it would be confusing to have two docking targets within each others.
		ImGuiWindowFlags window_flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
		ImGuiViewport*   viewport     = ImGui::GetMainViewport();
		ImGui::SetNextWindowPos(viewport->GetWorkPos());
		ImGui::SetNextWindowSize(viewport->GetWorkSize());
		ImGui::SetNextWindowViewport(viewport->ID);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
		ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
		window_flags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
		window_flags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;

		// When using ImGuiDockNodeFlags_PassthruCentralNode, DockSpace() will render our background
		// and handle the pass-thru hole, so we ask Begin() to not render a background.
		if (dockspace_flags & ImGuiDockNodeFlags_PassthruCentralNode)
			window_flags |= ImGuiWindowFlags_NoBackground;

		// Important: note that we proceed even if Begin() returns false (aka window is collapsed).
		// This is because we want to keep our DockSpace() active. If a DockSpace() is inactive,
		// all active windows docked into it will lose their parent and become undocked.
		// We cannot preserve the docking relationship between an active window and an inactive docking, otherwise
		// any change of dockspace/settings would lead to windows being stuck in limbo and never being visible.
		ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
		ImGui::Begin("DockSpace Demo", nullptr, window_flags);
		ImGui::PopStyleVar();

		ImGui::PopStyleVar(2);

		// DockSpace
		// ImGuiIO& io = ImGui::GetIO();
		ImGuiID dockspace_id = ImGui::GetID("###Dockspace");

		if (ImGui::DockBuilderGetNode(dockspace_id) == nullptr)
		{
			ImGui::DockBuilderRemoveNode(dockspace_id);
			ImGui::DockBuilderAddNode(dockspace_id, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspace_id, ImVec2(g_width, g_height));

			ImGuiID dockMainID  = dockspace_id;
			ImGuiID dockIDLeft  = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Left, 0.20f, nullptr, &dockMainID);
			ImGuiID dockIDRight = ImGui::DockBuilderSplitNode(dockMainID, ImGuiDir_Right, 0.20f, nullptr, &dockMainID);

			ImGui::DockBuilderDockWindow("Viewport", dockMainID);
			ImGui::DockBuilderDockWindow("Entities", dockIDLeft);
			ImGui::DockBuilderDockWindow("Light", dockIDLeft);
			ImGui::DockBuilderDockWindow("Properties", dockIDRight);
		}

		ImGui::DockSpace(dockspace_id, ImVec2(0.0f, 0.0f), dockspace_flags);

		ImGui::Begin("Viewport");
		{
			ImVec2 vMin = ImGui::GetWindowContentRegionMin();
			ImVec2 vMax = ImGui::GetWindowContentRegionMax();

			vMin.x += ImGui::GetWindowPos().x;
			vMin.y += ImGui::GetWindowPos().y;
			vMax.x += ImGui::GetWindowPos().x;
			vMax.y += ImGui::GetWindowPos().y;

			int wx, wy;
			glfwGetWindowPos(window, &wx, &wy);

			vMin.x -= wx;
			vMin.y -= wy;
			vMax.x -= wx;
			vMax.y -= wy;

			g_viewportX = vMin.x;
			g_viewportY = vMin.y;
			g_viewportW = vMax.x - vMin.x;
			g_viewportH = vMax.y - vMin.y;

			const glm::vec2 size(g_viewportW, g_viewportH);

			ImTextureID id = (void*)(intptr_t)Render(&g_models, size);
			// ImTextureID
			ImGui::Image(id, ImVec2(size.x, size.y), ImVec2(0, 1), ImVec2(1, 0));
		}
		ImGui::End();

		static int selectedEntity = -1;

		ImGui::Begin("Entities");
		{
			for (int i = 0; i < g_models.size(); ++i)
			{
				char buf[32];
				sprintf(buf, "Entity #%d", i);
				if (ImGui::Selectable(buf, selectedEntity == i))
				{
					selectedEntity = i;
				}
			}
		}
		ImGui::End();

		ImGui::Begin("Light");
		{
			ImGui::InputFloat3("Light direction", &g_lightDirection.x);
		}
		ImGui::End();

		static GLuint* selectedTexture;

		ImGui::Begin("Properties");
		if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen))
		{
			if (selectedEntity >= 0 && selectedEntity < g_models.size())
			{
				ImGui::LabelText("Hello", "World");
				Model*    model    = &g_models[selectedEntity];
				Material* material = model->material;

				ImGui::Checkbox("Albedo", &material->hasAlbedo);
				if (material->hasAlbedo)
				{
					ImGui::ColorEdit3("Albedo", &material->albedo.x);
				}

				ImGui::Checkbox("Albedo texture", &material->hasAlbedoTexture);
				if (material->hasAlbedoTexture)
				{
					if (ImGui::ImageButton((void*)(intptr_t)material->albedoTexture, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0)))
					{
						selectedTexture = &material->albedoTexture;
						textureDialog.Open();
					}
				}

				ImGui::Checkbox("Emissive", &material->hasEmissive);
				if (material->hasEmissive)
				{
					ImGui::ColorEdit3("Emissive", &material->emissive.x);
				}

				ImGui::Checkbox("Emissive texture", &material->hasEmissiveTexture);
				if (material->hasEmissiveTexture)
				{
					if (ImGui::ImageButton((void*)(intptr_t)material->emissiveTexture, ImVec2(64, 64), ImVec2(0, 1), ImVec2(1, 0)))
					{
						selectedTexture = &material->emissiveTexture;
						textureDialog.Open();
					}
				}
			}
		}
		ImGui::End();

		static bool opened = true;
		ImGui::ShowDemoWindow(&opened);

		ImGui::End();

		textureDialog.Display();
		if (textureDialog.HasSelected())
		{
			std::string textureFile = textureDialog.GetSelected().string();
			*selectedTexture        = LoadTexture(textureFile.c_str());
			textureDialog.ClearSelected();
		}

		ImGui::Render();
		ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			GLFWwindow* backupCurrentContext = glfwGetCurrentContext();
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
			glfwMakeContextCurrent(backupCurrentContext);
		}

		glfwSwapBuffers(window);
		glfwPollEvents();
	}

	glfwTerminate();

	return 0;
}

double lastX, lastY;
bool   movingCamera = false;

inline bool inViewport(double x, double y)
{
	return (x >= g_viewportX && x <= (g_viewportX + g_viewportW)) && (y >= g_viewportY && y <= (g_viewportY + g_viewportH));
}

static void MouseButtonCallback(GLFWwindow* window, int button, int action, int mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		double x, y;
		glfwGetCursorPos(window, &x, &y);

		if (inViewport(x, y))
		{
			movingCamera = true;
			lastX        = x;
			lastY        = y;
		}
	}
	else if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_RELEASE)
	{
		movingCamera = false;
	}
}

static void MouseMoveCallback(GLFWwindow* window, double x, double y)
{
	if (movingCamera)
	{
		const double dx = 0.1f * (x - lastX);
		const double dy = 0.1f * (y - lastY);

		g_camera.phi += dx;
		g_camera.theta = Clamp(g_camera.theta + (float)dy, 10.0f, 170.0f);

		lastX = x;
		lastY = y;
	}
}

static void KeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
	{
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}
}

static void WheelCallback(GLFWwindow* window, double x, double y)
{
	if (!movingCamera)
	{
		double mouseX, mouseY;
		glfwGetCursorPos(window, &mouseX, &mouseY);

		if (inViewport(mouseX, mouseY))
		{
			constexpr float minDistance = 0.01f;
			constexpr float maxDistance = 100.0f;

			const float multiplier = 2.5f * (g_camera.distance - minDistance) / (maxDistance - minDistance);

			const float distance = g_camera.distance - (float)y * multiplier;

			g_camera.distance = Clamp(distance, minDistance, maxDistance);
		}
	}
}

static void FramebufferSizeCallback(GLFWwindow* window, int width, int height)
{
	g_width  = width;
	g_height = height;
}

void SetupUI(GLFWwindow* window)
{
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
	io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

	ImGui::StyleColorsDark();

	ImGuiStyle& style = ImGui::GetStyle();
	if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
	{
		style.WindowRounding              = 0.0f;
		style.Colors[ImGuiCol_WindowBg].w = 1.0f;
	}

	ImGui_ImplGlfw_InitForOpenGL(window, true);
	ImGui_ImplOpenGL3_Init("#version 450");
}

GLuint Render(std::vector<Model>* models, const glm::vec2& size)
{
	static auto lastSize = glm::vec2(0, 0);

	// FIXME: Be smart about this
	if (!glIsFramebuffer(msaaFramebuffer))
	{
		glCreateFramebuffers(2, fbos);
	}

	if (lastSize != size)
	{
		if (glIsTexture(msaaRenderTexture))
		{
			glDeleteTextures(1, &msaaRenderTexture);
			glDeleteTextures(1, &resolveTexture);
			glDeleteRenderbuffers(1, &msaaDepthRenderBuffer);
		}

		// Create MSAA texture and attach it to FBO
		glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &msaaRenderTexture);
		glTextureStorage2DMultisample(msaaRenderTexture, 4, GL_RGB8, size.x, size.y, GL_TRUE);
		glNamedFramebufferTexture(msaaFramebuffer, GL_COLOR_ATTACHMENT0, msaaRenderTexture, 0);

		// Create MSAA DS rendertarget and attach it to FBO
		glCreateRenderbuffers(1, &msaaDepthRenderBuffer);
		glNamedRenderbufferStorageMultisample(msaaDepthRenderBuffer, 4, GL_DEPTH24_STENCIL8, size.x, size.y);
		glNamedFramebufferRenderbuffer(msaaFramebuffer, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRenderBuffer);

		if (glCheckNamedFramebufferStatus(msaaFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "MSAA framebuffer incomplete\n");
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &resolveTexture);
		glTextureStorage2D(resolveTexture, 1, GL_RGB8, size.x, size.y);
		glNamedFramebufferTexture(resolveFramebuffer, GL_COLOR_ATTACHMENT0, resolveTexture, 0);

		if (glCheckNamedFramebufferStatus(resolveFramebuffer, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "Resolve framebuffer incomplete\n");
		}

		lastSize = size;
	}

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, msaaFramebuffer);

	for (auto&& program : g_programs)
	{
		program.second.Update();
	}

	glViewport(0, 0, size.x, size.y);

	glClearColor(0.1f, 0.6f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LESS);

	RenderContext context = {
	    .view = g_camera.GetView(),
	    .proj = glm::perspective(60.0f * ToRadians, (float)size.x / size.y, 0.001f, 100.0f),
	};

	for (auto&& model : *models)
	{
		model.Draw(&context);
	}

	glDisable(GL_DEPTH_TEST);

	glBlitNamedFramebuffer(msaaFramebuffer,
	                       resolveFramebuffer,
	                       0,
	                       0,
	                       size.x,
	                       size.y,
	                       0,
	                       0,
	                       size.x,
	                       size.y,
	                       GL_COLOR_BUFFER_BIT,
	                       GL_NEAREST);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	return resolveTexture;
}

static void DropCallback(GLFWwindow* window, int count, const char** paths)
{
	if (count > 0)
	{
		LoadScene(paths[0]);
	}
}
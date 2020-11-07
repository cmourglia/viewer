#include <glad/glad.h>
#include <glfw/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <stb_image.h>

#include <imgui.h>
#include <imgui_internal.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include "imfilebrowser.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>

#include <vector>
#include <filesystem>
#include <chrono>
#include <unordered_map>
#include <unordered_set>
#include <filesystem>

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

void RenderCube();

void DebugOutput(GLenum source, GLenum type, uint32_t id, GLenum severity, GLsizei length, const char* message, const void* userParam);

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

std::string GetFileExtension(const std::string& filename)
{
	return filename.substr(filename.find_last_of(".") + 1);
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

	glm::vec3 position;
	glm::vec3 center;
	glm::vec3 up;

	glm::mat4 GetView()
	{
		const float x = distance * sinf(theta * ToRadians) * sinf(phi * ToRadians);
		const float y = distance * cosf(theta * ToRadians);
		const float z = distance * sinf(theta * ToRadians) * cosf(phi * ToRadians);

		position = glm::vec3(x, y, z);
		center   = glm::vec3(0.0f, 0.0f, 0.0f);
		up       = glm::vec3(0.0f, 1.0f, 0.0f);

		return glm::lookAt(position, center, up);
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
	GLenum error = glGetError();
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
		auto it = m_uniforms.find(name);
		return (it == m_uniforms.end()) ? -1 : it->second;
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
static std::unordered_map<std::string, GLuint> g_textures;

GLuint LoadTexture(const std::string& filename)
{
	auto it = g_textures.find(filename);
	if (it != g_textures.end())
	{
		return it->second;
	}

	stbi_set_flip_vertically_on_load(true);

	int      w, h, c;
	uint8_t* data = stbi_load(filename.c_str(), &w, &h, &c, 0);

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

	g_textures.insert(std::make_pair(filename, texture));

	return texture;
}

/*
File Structure:
  Section     Length
  ///////////////////
  FILECODE    4
  HEADER      124
  HEADER_DX10* 20	(https://msdn.microsoft.com/en-us/library/bb943983(v=vs.85).aspx)
  PIXELS      fseek(f, 0, SEEK_END); (ftell(f) - 128) - (fourCC == "DX10" ? 17 or 20 : 0)
* the link tells you that this section isn't written unless its a DX10 file
Supports DXT1, DXT3, DXT5.
The problem with supporting DX10 is you need to know what it is used for and how opengl would use it.
File Byte Order:
typedef uint32_t DWORD;           // 32bits little endian
  type   index    attribute           // description
///////////////////////////////////////////////////////////////////////////////////////////////
  DWORD  0        file_code;          //. always `DDS `, or 0x20534444
  DWORD  4        size;               //. size of the header, always 124 (includes PIXELFORMAT)
  DWORD  8        flags;              //. bitflags that tells you if data is present in the file
                                      //      CAPS         0x1
                                      //      HEIGHT       0x2
                                      //      WIDTH        0x4
                                      //      PITCH        0x8
                                      //      PIXELFORMAT  0x1000
                                      //      MIPMAPCOUNT  0x20000
                                      //      LINEARSIZE   0x80000
                                      //      DEPTH        0x800000
  DWORD  12       height;             //. height of the base image (biggest mipmap)
  DWORD  16       width;              //. width of the base image (biggest mipmap)
  DWORD  20       pitchOrLinearSize;  //. bytes per scan line in an uncompressed texture, or bytes in the top level texture for a compressed
texture
                                      //     D3DX11.lib and other similar libraries unreliably or inconsistently provide the pitch, convert
with
                                      //     DX* && BC*: max( 1, ((width+3)/4) ) * block-size
                                      //     *8*8_*8*8 && UYVY && YUY2: ((width+1) >> 1) * 4
                                      //     (width * bits-per-pixel + 7)/8 (divide by 8 for byte alignment, whatever that means)
  DWORD  24       depth;              //. Depth of a volume texture (in pixels), garbage if no volume data
  DWORD  28       mipMapCount;        //. number of mipmaps, garbage if no pixel data
  DWORD  32       reserved1[11];      //. unused
  DWORD  76       Size;               //. size of the following 32 bytes (PIXELFORMAT)
  DWORD  80       Flags;              //. bitflags that tells you if data is present in the file for following 28 bytes
                                      //      ALPHAPIXELS  0x1
                                      //      ALPHA        0x2
                                      //      FOURCC       0x4
                                      //      RGB          0x40
                                      //      YUV          0x200
                                      //      LUMINANCE    0x20000
  DWORD  84       FourCC;             //. File format: DXT1, DXT2, DXT3, DXT4, DXT5, DX10.
  DWORD  88       RGBBitCount;        //. Bits per pixel
  DWORD  92       RBitMask;           //. Bit mask for R channel
  DWORD  96       GBitMask;           //. Bit mask for G channel
  DWORD  100      BBitMask;           //. Bit mask for B channel
  DWORD  104      ABitMask;           //. Bit mask for A channel
  DWORD  108      caps;               //. 0x1000 for a texture w/o mipmaps
                                      //      0x401008 for a texture w/ mipmaps
                                      //      0x1008 for a cube map
  DWORD  112      caps2;              //. bitflags that tells you if data is present in the file
                                      //      CUBEMAP           0x200     Required for a cube map.
                                      //      CUBEMAP_POSITIVEX 0x400     Required when these surfaces are stored in a cube map.
                                      //      CUBEMAP_NEGATIVEX 0x800     ^
                                      //      CUBEMAP_POSITIVEY 0x1000    ^
                                      //      CUBEMAP_NEGATIVEY 0x2000    ^
                                      //      CUBEMAP_POSITIVEZ 0x4000    ^
                                      //      CUBEMAP_NEGATIVEZ 0x8000    ^
                                      //      VOLUME            0x200000  Required for a volume texture.
  DWORD  114      caps3;              //. unused
  DWORD  116      caps4;              //. unused
  DWORD  120      reserved2;          //. unused
*/
GLuint LoadDDS(const char* path)
{
	// lay out variables to be used
	uint8_t* header;

	uint32_t width;
	uint32_t height;
	uint32_t mipMapCount;

	uint32_t blockSize;
	uint32_t format;

	uint32_t w;
	uint32_t h;

	uint8_t* buffer = 0;

	GLuint tid = 0;

	// open the DDS file for binary reading and get file size
	FILE* f;
	if ((f = fopen(path, "rb")) == 0)
	{
		fprintf(stderr, "Could not open file %s\n", path);
		return 0;
	}

	fseek(f, 0, SEEK_END);
	long file_size = ftell(f);
	fseek(f, 0, SEEK_SET);

	// allocate new uint8_t space with 4 (file code) + 124 (header size) bytes
	// read in 128 bytes from the file
	header = (uint8_t*)malloc(128);
	fread(header, 1, 128, f);

	// compare the `DDS ` signature
	if (memcmp(header, "DDS ", 4) != 0)
	{
		free(header);
		fclose(f);
		fprintf(stderr, "%s is not a dds file\n", path);
		return 0;
	}

	// extract height, width, and amount of mipmaps - yes it is stored height then width
	height      = (header[12]) | (header[13] << 8) | (header[14] << 16) | (header[15] << 24);
	width       = (header[16]) | (header[17] << 8) | (header[18] << 16) | (header[19] << 24);
	mipMapCount = (header[28]) | (header[29] << 8) | (header[30] << 16) | (header[31] << 24);

	// figure out what format to use for what fourCC file type it is
	// block size is about physical chunk storage of compressed data in file (important)
	if (header[84] == 'D')
	{
		switch (header[87])
		{
			case '1': // DXT1
				format    = GL_COMPRESSED_RGBA_S3TC_DXT1_EXT;
				blockSize = 8;
				break;
			case '3': // DXT3
				format    = GL_COMPRESSED_RGBA_S3TC_DXT3_EXT;
				blockSize = 16;
				break;
			case '5': // DXT5
				format    = GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
				blockSize = 16;
				break;
			case '0': // DX10
			          // unsupported, else will error
			          // as it adds sizeof(struct DDS_HEADER_DXT10) between pixels
			          // so, buffer = malloc((file_size - 128) - sizeof(struct DDS_HEADER_DXT10));
			default:
				free(header);
				fclose(f);
				fprintf(stderr, "%s is not an handled DDS format\n", path);
				return 0;
		}
	}
	else // BC4U/BC4S/ATI2/BC55/R8G8_B8G8/G8R8_G8B8/UYVY-packed/YUY2-packed unsupported
	{
		free(header);
		fclose(f);
		fprintf(stderr, "%s is not a supported DDS file\n", path);
		return 0;
	}

	// allocate new uint8_t space with file_size - (file_code + header_size) magnitude
	// read rest of file
	buffer = (uint8_t*)malloc(file_size - 128);
	if (buffer == 0)
	{
		free(header);
		fclose(f);
		fprintf(stderr, "Cannot malloc\n");
		return 0;
	}
	fread(buffer, 1, file_size, f);

	// prepare new incomplete texture
	glGenTextures(1, &tid);

	// bind the texture
	// make it complete by specifying all needed parameters and ensuring all mipmaps are filled
	glBindTexture(GL_TEXTURE_2D, tid);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_BASE_LEVEL, 0);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1); // opengl likes array length of mipmaps
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR); // don't forget to enable mipmaping
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

	// prepare some variables
	uint32_t offset = 0;
	uint32_t size   = 0;
	w               = width;
	h               = height;

	// loop through sending block at a time with the magic formula
	// upload to opengl properly, note the offset transverses the pointer
	// assumes each mipmap is 1/2 the size of the previous mipmap
	for (uint32_t i = 0; i < mipMapCount; i++)
	{
		if (w == 0 || h == 0)
		{ // discard any odd mipmaps 0x1 0x2 resolutions
			mipMapCount--;
			continue;
		}
		size = ((w + 3) / 4) * ((h + 3) / 4) * blockSize;
		glCompressedTexImage2D(GL_TEXTURE_2D, i, format, w, h, 0, size, buffer + offset);
		offset += size;
		w /= 2;
		h /= 2;
	}
	// discard any odd mipmaps, ensure a complete texture
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAX_LEVEL, mipMapCount - 1);
	// unbind
	glBindTexture(GL_TEXTURE_2D, 0);

	free(buffer);
	free(header);
	fclose(f);
	return tid;
}

static GLuint g_irradianceMap;
static GLuint g_iblDFG;

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
		glBindTextureUnit(index, g_irradianceMap);
		++index;

		program->SetUniform("s_iblDFG", index);
		glBindTextureUnit(index, g_iblDFG);
		++index;
	}

	Program BuildProgram()
	{
		return Program::MakeRender(m_name, m_baseVS, m_baseFS, GetDefines());
	}

private:
	std::vector<const char*> GetDefines()
	{
		std::vector<const char*> defines;

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
	glm::vec3 eyePosition;
	glm::mat4 model, view, proj;

	glm::vec3 lightDirection;
};

void SetupShader(RenderContext* context, Material* material)
{
	const uint32_t mask = material->GetMask();

	if (g_programs.find(mask) == g_programs.end())
	{
		Program program = material->BuildProgram();
		g_programs.emplace(std::make_pair(mask, program));
	}

	Program* program = &g_programs[mask];

	program->Bind();

	program->SetUniform("u_eye", context->eyePosition);
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
				if (insertedData.find(entry.data) == insertedData.end())
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

Mesh* ProcessMesh(aiMesh* inputMesh, const aiScene* scene)
{
	std::vector<Vertex>   vertices;
	std::vector<uint32_t> indices;

	vertices.reserve(inputMesh->mNumVertices);
	indices.reserve(inputMesh->mNumFaces * 3);

	const aiVector3D* inVertices  = inputMesh->mVertices;
	const aiVector3D* inNormals   = inputMesh->mNormals;
	const aiVector3D* inTexcoords = inputMesh->mTextureCoords[0];

	for (uint32_t index = 0; index < inputMesh->mNumVertices; ++index)
	{
		const aiVector3D v = *inVertices++;
		const aiVector3D n = *inNormals++;

		Vertex vertex;
		vertex.position = {v.x, v.y, v.z};
		vertex.normal   = {n.x, n.y, n.z};

		if (inTexcoords)
		{
			const aiVector3D t = *inTexcoords++;
			vertex.texcoord    = {t.x, t.y};
		}

		vertices.push_back(vertex);
	}

	const aiFace* inFaces = inputMesh->mFaces;
	for (uint32_t i = 0; i < inputMesh->mNumFaces; ++i)
	{
		const aiFace face = *inFaces++;
		for (uint32_t j = 0; j < face.mNumIndices; ++j)
		{
			indices.push_back(face.mIndices[j]);
		}
	}

	Mesh* mesh = new Mesh(vertices, indices);

	return mesh;
}

std::string TexturePath(const char* texture, const std::filesystem::path& path)
{
	std::filesystem::path texturePath(texture);
	if (texturePath.is_absolute())
	{
		return texturePath.string();
	}

	std::filesystem::path fullPath = path / texturePath;

	return fullPath.string();
}

Material* ProcessMaterial(aiMaterial* inputMaterial, const aiScene* scene, const std::filesystem::path& path)
{
	Material* material = new Material(inputMaterial->GetName().C_Str(), "resources/shaders/pbr.vert", "resources/shaders/pbr.frag");

	aiColor3D albedo;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR, albedo))
	{
		material->hasAlbedo = true;
		material->albedo    = glm::vec3(albedo.r, albedo.g, albedo.b);
	}

	float metallic;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic))
	{
		material->hasMetallic = true;
		material->metallic    = metallic;
	}

	float roughness;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR, roughness))
	{
		material->hasRoughness = true;
		material->roughness    = roughness;
	}

	aiString albedoTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE, &albedoTexture))
	{
		material->hasAlbedoTexture = true;
		material->albedoTexture    = LoadTexture(TexturePath(albedoTexture.C_Str(), path));
	}

	aiString metallicRoughnessTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE, &metallicRoughnessTexture))
	{
		material->hasMetallicRoughnessTexture = true;
		material->metallicRoughnessTexture    = LoadTexture(TexturePath(metallicRoughnessTexture.C_Str(), path));
	}

	aiColor3D emissiveColor;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_COLOR_EMISSIVE, emissiveColor))
	{
		material->hasEmissive = true;
		material->emissive    = glm::vec3(emissiveColor.r, emissiveColor.g, emissiveColor.b);
	}

	aiString emissiveTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(aiTextureType_EMISSIVE, 0, &emissiveTexture))
	{
		material->hasEmissiveTexture = true;
		material->emissiveTexture    = LoadTexture(TexturePath(emissiveTexture.C_Str(), path));
	}

	aiString occlusionTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(aiTextureType_LIGHTMAP, 0, &occlusionTexture))
	{
		material->hasAmbientOcclusionMap = true;
		material->ambientOcclusionMap    = LoadTexture(TexturePath(occlusionTexture.C_Str(), path));
	}

	return material;
}

void ProcessNode(aiNode* node, const aiScene* scene, const glm::mat4& parentTransform, const std::filesystem::path& path)
{
	const auto& mat = node->mTransformation;

	// clang-format off
	const glm::mat4 nodeTransform(mat.a1, mat.b1, mat.c1, mat.d1,
	                              mat.a2, mat.b2, mat.c2, mat.d2,
	                              mat.a3, mat.b3, mat.c3, mat.d3,
	                              mat.a4, mat.b4, mat.c4, mat.d4);
	// clang-format on

	const glm::mat4 transform = parentTransform * nodeTransform;

	for (uint32_t index = 0; index < node->mNumMeshes; ++index)
	{
		Model model;

		aiMesh*     inputMesh     = scene->mMeshes[node->mMeshes[index]];
		aiMaterial* inputMaterial = scene->mMaterials[inputMesh->mMaterialIndex];

		model.mesh           = ProcessMesh(scene->mMeshes[node->mMeshes[index]], scene);
		model.material       = ProcessMaterial(inputMaterial, scene, path);
		model.worldTransform = transform;
		g_models.push_back(model);
	}

	for (uint32_t index = 0; index < node->mNumChildren; ++index)
	{
		ProcessNode(node->mChildren[index], scene, transform, path);
	}
}

void LoadScene(const char* filename)
{
	Assimp::Importer importer;

	const uint32_t importerFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace |
	                               aiProcess_OptimizeMeshes;
	const aiScene* scene = importer.ReadFile(filename, importerFlags);

	if ((nullptr == scene) || (0 != scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) || (nullptr == scene->mRootNode))
	{
		fprintf(stderr, "Could not load file %s\n", filename);
		return;
	}

	std::filesystem::path p;
	p = filename;
	p.remove_filename();
	ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), p);
}

static Program g_equirectangularToCubemapProgram;
static Program g_irradianceProgram;
static Program g_backgroundProgram;
static Mesh    g_cubeMesh;
static GLuint  g_envCubeMap;

void LoadEnvironment(const char* filename)
{
	stbi_set_flip_vertically_on_load(true);

	int    w, h, c;
	float* data = stbi_loadf(filename, &w, &h, &c, 0);

	stbi_set_flip_vertically_on_load(false);

	if (data == nullptr)
	{
		return;
	}

	GLuint equirectangularTexture;
	glCreateTextures(GL_TEXTURE_2D, 1, &equirectangularTexture);

	const int levels = log2f(Min(w, h));

	// glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureSubImage2D(equirectangularTexture, 0, 0, 0, w, h, GL_RGB, GL_FLOAT, data);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(equirectangularTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	stbi_image_free(data);

	if (glIsTexture(g_envCubeMap))
	{
		glDeleteTextures(1, &g_envCubeMap);
	}

	glGenTextures(1, &g_envCubeMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, g_envCubeMap);

	// glTextureStorage2D(g_envCubeMap, 1, GL_RGB32F, 512, 512);
	// glTextureParameteri(g_envCubeMap, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	// glTextureParameteri(g_envCubeMap, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	// glTextureParameteri(g_envCubeMap, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

	for (int face = 0; face < 6; ++face)
	{
		// face:
		// 0 -> positive x
		// 1 -> negative x
		// 2 -> positive y
		// 3 -> negative y
		// 4 -> positive z
		// 5 -> negative z
		// glTextureSubImage3D(g_envCubeMap, 0, 0, 0, face, 512, 512, 1, GL_RGB, GL_HALF_FLOAT, nullptr);
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB32F, 512, 512, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
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
	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
	glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, captureRBO);

	g_equirectangularToCubemapProgram.Bind();
	g_equirectangularToCubemapProgram.SetUniform("equirectangularMap", 0);
	glBindTextureUnit(0, equirectangularTexture);
	g_equirectangularToCubemapProgram.SetUniform("proj", captureProjection);
	glViewport(0, 0, 512, 512);

	glDepthFunc(GL_ALWAYS);
	for (int face = 0; face < 6; ++face)
	{
		g_equirectangularToCubemapProgram.SetUniform("view", captureViews[face]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, g_envCubeMap, 0);

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

	if (glIsTexture(g_irradianceMap))
	{
		glDeleteTextures(1, &g_irradianceMap);
	}

	glGenTextures(1, &g_irradianceMap);
	glBindTexture(GL_TEXTURE_CUBE_MAP, g_irradianceMap);

	for (int face = 0; face < 6; ++face)
	{
		// face:
		// 0 -> positive x
		// 1 -> negative x
		// 2 -> positive y
		// 3 -> negative y
		// 4 -> positive z
		// 5 -> negative z
		glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB32F, 64, 64, 0, GL_RGB, GL_FLOAT, nullptr);
	}

	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 64, 64);

	g_irradianceProgram.Bind();
	g_irradianceProgram.SetUniform("envMap", 0);
	glBindTextureUnit(0, g_envCubeMap);
	g_irradianceProgram.SetUniform("proj", captureProjection);
	glViewport(0, 0, 64, 64);

	glDepthFunc(GL_ALWAYS);
	for (int face = 0; face < 6; ++face)
	{
		g_irradianceProgram.SetUniform("view", captureViews[face]);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, g_irradianceMap, 0);

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

glm::vec3 g_lightDirection    = glm::vec3(0, -1, 0);
bool      g_showIrradianceMap = false;

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
	glfwSwapInterval(0);

	glfwGetFramebufferSize(window, &g_width, &g_height);

	if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress))
	{
		return 1;
	}

#if _DEBUG
	glEnable(GL_DEBUG_OUTPUT);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
	glDebugMessageCallback(DebugOutput, nullptr);
	glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, nullptr, GL_TRUE);
#endif

	SetupUI(window);

	// glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	g_equirectangularToCubemapProgram = Program::MakeRender("equirectangularToCubemap",
	                                                        "resources/shaders/cubemap.vert",
	                                                        "resources/shaders/equirectangularToCubemap.frag");
	g_irradianceProgram = Program::MakeRender("irradiance", "resources/shaders/cubemap.vert", "resources/shaders/irradiance.frag");
	g_backgroundProgram = Program::MakeRender("background", "resources/shaders/background.vert", "resources/shaders/background.frag");

	g_iblDFG = LoadDDS("resources/textures/dfg.dds");

	LoadEnvironment("resources/env/Frozen_Waterfall_Ref.hdr");

	// LoadScene("external/glTF-Sample-Models/2.0/DamagedHelmet/glTF/DamagedHelmet.gltf");
	// LoadScene(R"(W:\glTF-Sample-Models\2.0\MetalRoughSpheres\glTF\MetalRoughSpheres.gltf)");

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
			ImGui::Checkbox("Show irradiance map", &g_showIrradianceMap);
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

	g_backgroundProgram.Update();
	g_equirectangularToCubemapProgram.Update();
	g_irradianceProgram.Update();

	glViewport(0, 0, size.x, size.y);

	glClearDepth(1.0f);
	glClearColor(0.1f, 0.6f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	RenderContext context = {
	    .eyePosition = g_camera.position,
	    .view        = g_camera.GetView(),
	    .proj        = glm::perspective(60.0f * ToRadians, (float)size.x / size.y, 0.001f, 100.0f),
	};

	for (auto&& model : *models)
	{
		model.Draw(&context);
	}

	g_backgroundProgram.Bind();
	g_backgroundProgram.SetUniform("envmap", 0);
	g_backgroundProgram.SetUniform("view", context.view);
	g_backgroundProgram.SetUniform("proj", context.proj);
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_CUBE_MAP, g_showIrradianceMap ? g_irradianceMap : g_envCubeMap);
	// glBindTexture(GL_TEXTURE_CUBE_MAP, g_envCubeMap);
	RenderCube();

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
	for (int i = 0; i < count; ++i)
	{
		std::string ext = GetFileExtension(paths[i]);
		if (ext == "hdr")
		{
			LoadEnvironment(paths[i]);
		}
		else
		{
			LoadScene(paths[i]);
		}
	}
}

void RenderCube()
{
	static GLuint g_cubeVAO = 0;
	static GLuint g_cubeVBO = 0;

	// initialize (if necessary)
	if (g_cubeVAO == 0)
	{
		// clang-format off
        float vertices[] = {
            // back face
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 0.0f, // bottom-right
             1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 1.0f, 1.0f, // top-right
            -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 0.0f, // bottom-left
            -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, -1.0f, 0.0f, 1.0f, // top-left
            // front face
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
             1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 0.0f, // bottom-right
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
             1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 1.0f, 1.0f, // top-right
            -1.0f,  1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 1.0f, // top-left
            -1.0f, -1.0f,  1.0f,  0.0f,  0.0f,  1.0f, 0.0f, 0.0f, // bottom-left
            // left face
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
            -1.0f,  1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-left
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
            -1.0f, -1.0f, -1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-left
            -1.0f, -1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-right
            -1.0f,  1.0f,  1.0f, -1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-right
            // right face
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
             1.0f,  1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 1.0f, // top-right
             1.0f, -1.0f, -1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 1.0f, // bottom-right
             1.0f,  1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 1.0f, 0.0f, // top-left
             1.0f, -1.0f,  1.0f,  1.0f,  0.0f,  0.0f, 0.0f, 0.0f, // bottom-left
            // bottom face
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
             1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 1.0f, // top-left
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
             1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 1.0f, 0.0f, // bottom-left
            -1.0f, -1.0f,  1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 0.0f, // bottom-right
            -1.0f, -1.0f, -1.0f,  0.0f, -1.0f,  0.0f, 0.0f, 1.0f, // top-right
            // top face
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
             1.0f,  1.0f , 1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
             1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 1.0f, // top-right
             1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 1.0f, 0.0f, // bottom-right
            -1.0f,  1.0f, -1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 1.0f, // top-left
            -1.0f,  1.0f,  1.0f,  0.0f,  1.0f,  0.0f, 0.0f, 0.0f  // bottom-left
        };
		// clang-format on

		glGenVertexArrays(1, &g_cubeVAO);
		glGenBuffers(1, &g_cubeVBO);
		// fill buffer
		glBindBuffer(GL_ARRAY_BUFFER, g_cubeVBO);
		glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
		// link vertex attributes
		glBindVertexArray(g_cubeVAO);
		glEnableVertexAttribArray(0);
		glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
		glEnableVertexAttribArray(1);
		glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
		glEnableVertexAttribArray(2);
		glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));
		glBindBuffer(GL_ARRAY_BUFFER, 0);
		glBindVertexArray(0);
	}
	// render Cube
	glBindVertexArray(g_cubeVAO);
	glDrawArrays(GL_TRIANGLES, 0, 36);
	glBindVertexArray(0);
}

void APIENTRY
DebugOutput(GLenum source, GLenum type, uint32_t id, GLenum severity, GLsizei length, const char* message, const void* userParam)
{
	// ignore non-significant error/warning codes
	if (id == 131169 || id == 131185 || id == 131218 || id == 131204)
		return;

	fprintf(stderr, "---------------\n");
	fprintf(stderr, "Debug message (%d): %s\n", id, message);

	switch (source)
	{
		case GL_DEBUG_SOURCE_API:
			fprintf(stderr, "Source: API");
			break;
		case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
			fprintf(stderr, "Source: Window System");
			break;
		case GL_DEBUG_SOURCE_SHADER_COMPILER:
			fprintf(stderr, "Source: Shader Compiler");
			break;
		case GL_DEBUG_SOURCE_THIRD_PARTY:
			fprintf(stderr, "Source: Third Party");
			break;
		case GL_DEBUG_SOURCE_APPLICATION:
			fprintf(stderr, "Source: Application");
			break;
		case GL_DEBUG_SOURCE_OTHER:
			fprintf(stderr, "Source: Other");
			break;
	}
	fprintf(stderr, "\n");

	switch (type)
	{
		case GL_DEBUG_TYPE_ERROR:
			fprintf(stderr, "Type: Error");
			break;
		case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
			fprintf(stderr, "Type: Deprecated Behaviour");
			break;
		case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
			fprintf(stderr, "Type: Undefined Behaviour");
			break;
		case GL_DEBUG_TYPE_PORTABILITY:
			fprintf(stderr, "Type: Portability");
			break;
		case GL_DEBUG_TYPE_PERFORMANCE:
			fprintf(stderr, "Type: Performance");
			break;
		case GL_DEBUG_TYPE_MARKER:
			fprintf(stderr, "Type: Marker");
			break;
		case GL_DEBUG_TYPE_PUSH_GROUP:
			fprintf(stderr, "Type: Push Group");
			break;
		case GL_DEBUG_TYPE_POP_GROUP:
			fprintf(stderr, "Type: Pop Group");
			break;
		case GL_DEBUG_TYPE_OTHER:
			fprintf(stderr, "Type: Other");
			break;
	}
	fprintf(stderr, "\n");

	switch (severity)
	{
		case GL_DEBUG_SEVERITY_HIGH:
			fprintf(stderr, "Severity: high");
			break;
		case GL_DEBUG_SEVERITY_MEDIUM:
			fprintf(stderr, "Severity: medium");
			break;
		case GL_DEBUG_SEVERITY_LOW:
			fprintf(stderr, "Severity: low");
			break;
		case GL_DEBUG_SEVERITY_NOTIFICATION:
			fprintf(stderr, "Severity: notification");
			break;
	}
	fprintf(stderr, "\n\n");
}

#pragma once

#include "renderer/environment.h"
#include "renderer/material.h"
#include "renderer/program.h"

#include "core/defines.h"

#include <glad/glad.h>
#include <glm/glm.hpp>

#include <vector>

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

	GLsizeiptr GetSize() const;
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

struct Vertex
{
	glm::vec3 position;
	glm::vec3 normal;
	glm::vec2 texcoord;
};

struct Mesh
{
	GLuint  vao, buffer;
	GLsizei indexCount;
	GLenum  indexType;

	Mesh();
	Mesh(const std::vector<Vertex>& vertices, const std::vector<GLushort>& indices);
	Mesh(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices);
	Mesh(const VertexDataInfos& vertexDataInfos, const IndexDataInfos& indexDataInfos);

	static GLsizeiptr AlignedSize(GLsizeiptr size, GLsizeiptr align);

	void SetLayout(const Layout& layout, const std::vector<GLsizeiptr>& offsets);

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

	void Draw() const;
	void DrawInstanced(u32 instanceCount) const;
};

struct RenderContext
{
	glm::vec3 eyePosition;
	glm::mat4 model, view, proj;

	glm::vec3 lightDirection;

	Environment* env;
};

struct Model
{
	Material* material;
	Mesh*     mesh;

	glm::mat4 worldTransform;

	void Draw(RenderContext* context) const;
};

struct CameraInfos
{
	glm::mat4 view;
	glm::mat4 proj;
	glm::vec3 position;
};

class Renderer
{
public:
	void Initialize(const glm::vec2& initSize);
	u32  Render(const CameraInfos& camera, const std::vector<Model>& models);

	void Resize(const glm::vec2& newSize);

	Environment* GetEnvironment()
	{
		return &m_environment;
	}

private:
	glm::vec2 m_framebufferSize;
	glm::vec2 m_bloomBufferSize;

	u32 m_fbos[2];
	u32 m_rts[3];

#define m_msaaFB m_fbos[0]
#define m_resolveFB m_fbos[1]

	u32 m_msaaRenderTexture;
	u32 m_resolveTexture;
	u32 m_msaaDepthRenderBuffer;

	// Post-process textures
	u32 m_bloomTexture;
	u32 m_averageLuminanceTexture;

	// Final render texture
	u32 m_outputTexture;

	Program* m_backgroundProgram;

	// Post-process compute shaders
	Program* m_highPassProgram;
	Program* m_outputProgram;

	Environment m_environment;
};

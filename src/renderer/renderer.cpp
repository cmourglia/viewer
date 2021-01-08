#include "renderer.h"

#include "renderer/render_primitives.h"

#include "core/utils.h"

extern std::vector<glm::vec3> PrecomputeDFG(u32 w, u32 h, u32 sampleCount); // 128, 128, 512

void Renderer::Initialize(const glm::vec2& initialSize)
{
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	Program::MakeRender("equirectangularToCubemap", "resources/shaders/cubemap.vert", "resources/shaders/equirectangularToCubemap.frag");
	Program::MakeRender("prefilterEnvmap", "resources/shaders/cubemap.vert", "resources/shaders/prefilter.frag");
	Program::MakeRender("irradiance", "resources/shaders/cubemap.vert", "resources/shaders/irradiance.frag");
	m_backgroundProgram = Program::MakeRender("background", "resources/shaders/background.vert", "resources/shaders/background.frag");

	m_highPassProgram = Program::MakeCompute("highPass", "resources/shaders/highPassFilter.comp");
	m_outputProgram   = Program::MakeCompute("compose", "resources/shaders/compose.comp");

	glCreateTextures(GL_TEXTURE_2D, 1, &m_environment.iblDFG);

	const i32 levels = 1;

	// glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureStorage2D(m_environment.iblDFG, 1, GL_RGB32F, 128, 128);
	// glTextureSubImage2D(m_environment.iblDFG, 0, 0, 0, 128, 128, GL_RGB, GL_FLOAT, PrecomputeDFG(128, 128, 1).data());
	glTextureSubImage2D(m_environment.iblDFG, 0, 0, 0, 128, 128, GL_RGB, GL_FLOAT, PrecomputeDFG(128, 128, 1024).data());
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

	glCreateFramebuffers(2, m_fbos);

	Resize(initialSize);
}

void Renderer::Resize(const glm::vec2& newSize)
{
	if (m_framebufferSize != newSize)
	{
		if (glIsTexture(m_msaaRenderTexture))
		{
			glDeleteTextures(1, &m_msaaRenderTexture);
			glDeleteTextures(1, &m_resolveTexture);
			glDeleteTextures(1, &m_outputTexture);
			glDeleteTextures(1, &m_bloomTexture);
			glDeleteTextures(1, &m_averageLuminanceTexture);
			glDeleteRenderbuffers(1, &m_msaaDepthRenderBuffer);
		}

		// Create MSAA texture and attach it to FBO
		glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &m_msaaRenderTexture);
		glTextureStorage2DMultisample(m_msaaRenderTexture, 4, GL_RGBA32F, newSize.x, newSize.y, GL_TRUE);
		glNamedFramebufferTexture(m_msaaFB, GL_COLOR_ATTACHMENT0, m_msaaRenderTexture, 0);

		// Create MSAA DS rendertarget and attach it to FBO
		glCreateRenderbuffers(1, &m_msaaDepthRenderBuffer);
		glNamedRenderbufferStorageMultisample(m_msaaDepthRenderBuffer, 4, GL_DEPTH24_STENCIL8, newSize.x, newSize.y);
		glNamedFramebufferRenderbuffer(m_msaaFB, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRenderBuffer);

		if (glCheckNamedFramebufferStatus(m_msaaFB, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "MSAA framebuffer incomplete\n");
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &m_resolveTexture);
		glTextureStorage2D(m_resolveTexture, 1, GL_RGBA32F, newSize.x, newSize.y);
		glNamedFramebufferTexture(m_resolveFB, GL_COLOR_ATTACHMENT0, m_resolveTexture, 0);
		glTextureParameteri(m_resolveTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(m_resolveTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if (glCheckNamedFramebufferStatus(m_resolveFB, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "Resolve framebuffer incomplete\n");
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &m_outputTexture);
		glTextureStorage2D(m_outputTexture, 1, GL_RGBA8, newSize.x, newSize.y);

		m_bloomBufferSize = newSize / 8.0f;
		glCreateTextures(GL_TEXTURE_2D, 1, &m_bloomTexture);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_averageLuminanceTexture);

		m_framebufferSize = newSize;
	}
}

u32 Renderer::Render(const CameraInfos& camera, const std::vector<Model>& models)
{
	Program::UpdateAllPrograms();
	static auto lastSize = glm::vec2(0, 0);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_msaaFB);

	glViewport(0, 0, m_framebufferSize.x, m_framebufferSize.y);

	glClearDepth(1.0f);
	glClearColor(0.1f, 0.6f, 0.4f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

	// .proj        =
	RenderContext context = {
	    .eyePosition = camera.position,
	    .view        = camera.view,
	    .proj        = camera.proj,
	    .env         = GetEnvironment(),
	};

	for (auto&& model : models)
	{
		model.Draw(&context);
	}

	m_backgroundProgram->Bind();
	m_backgroundProgram->SetUniform("envmap", 0);
	m_backgroundProgram->SetUniform("view", context.view);
	m_backgroundProgram->SetUniform("proj", context.proj);
	glBindTextureUnit(0, m_environment.envMap);

	RenderCube();

	glDisable(GL_DEPTH_TEST);

	glBlitNamedFramebuffer(m_msaaFB,
	                       m_resolveFB,
	                       0,
	                       0,
	                       m_framebufferSize.x,
	                       m_framebufferSize.y,
	                       0,
	                       0,
	                       m_framebufferSize.x,
	                       m_framebufferSize.y,
	                       GL_COLOR_BUFFER_BIT,
	                       GL_NEAREST);

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);

	return m_resolveTexture;

	glGenerateTextureMipmap(m_resolveTexture);

	// Highpass
	m_highPassProgram->Bind();
	m_highPassProgram->SetUniform("viewportSize", m_bloomBufferSize);
	glBindImageTexture(0, m_resolveTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, m_bloomTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glGenerateTextureMipmap(m_resolveTexture);
	glDispatchCompute(m_framebufferSize.x / 128, m_framebufferSize.y / 128, 1);

	// Final render
	m_outputProgram->Bind();
	m_outputProgram->SetUniform("viewportSize", m_framebufferSize);
	glBindImageTexture(0, m_resolveTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, m_outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindTextureUnit(0, m_resolveTexture);
	m_outputProgram->SetUniform("input_sampler", 0);
	glDispatchCompute(m_framebufferSize.x / 16, m_framebufferSize.y / 16, 1);
	// glDispatchCompute(16, 8, 1);

	// glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	// m_outputTexture = m_resolveTexture;
	return m_outputTexture;
}

GLsizeiptr LayoutItem::GetSize() const
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

Mesh::Mesh()
{
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<GLushort>& indices)
    : indexType(GL_UNSIGNED_SHORT)
    , indexCount(indices.size())
{
	SetData(vertices, indices);
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices)
    : indexType(GL_UNSIGNED_INT)
    , indexCount(indices.size())
{
	SetData(vertices, indices);
}

Mesh::Mesh(const VertexDataInfos& vertexDataInfos, const IndexDataInfos& indexDataInfos)
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
		i32        vboIndex   = 0;
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
		i32        vboIndex   = 0;
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

GLsizeiptr Mesh::AlignedSize(GLsizeiptr size, GLsizeiptr align)
{
	return size;
	if (size % align == 0)
		return size;
	return size + (align - size % align);
}

void Mesh::SetLayout(const Layout& layout, const std::vector<GLsizeiptr>& offsets)
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

void Mesh::Draw() const
{
	glBindVertexArray(vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
	glDrawElements(GL_TRIANGLES, indexCount, indexType, nullptr);
}

void Mesh::DrawInstanced(u32 instanceCount) const
{
	glBindVertexArray(vao);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, buffer);
	glDrawElementsInstanced(GL_TRIANGLES, indexCount, indexType, nullptr, instanceCount);
}

void Model::Draw(RenderContext* context) const
{
	context->model = worldTransform;

	Program* program = material->GetProgram();

	program->Bind();

	program->SetUniform("u_eye", context->eyePosition);
	program->SetUniform("u_model", context->model);
	program->SetUniform("u_view", context->view);
	program->SetUniform("u_proj", context->proj);

	material->Bind(program, context->env);

	mesh->Draw();
}
#include "renderer.h"

#include "renderer/render_primitives.h"
#include "renderer/frame_stats.h"

#include "core/utils.h"

extern std::vector<glm::vec3> PrecomputeDFG(u32 w, u32 h, u32 sampleCount); // 128, 128, 512

void Renderer::Initialize(const glm::vec2& initialSize)
{
	glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);

	Program::MakeCompute("equirectangularToCubemap", "equirectangular_to_cubemap.comp.glsl");
	Program::MakeCompute("prefilterEnvmap", "prefilter.comp.glsl");
	Program::MakeCompute("irradiance", "irradiance.comp.glsl");

	m_backgroundProgram = Program::MakeRender("background", "background.vert.glsl", "background.frag.glsl");

	m_highpassProgram = Program::MakeCompute("highpass", "highpass_filter.comp.glsl");
	m_blurXProgram    = Program::MakeCompute("blurX", "blur.comp.glsl", {"HORIZONTAL_BLUR"});
	m_blurYProgram    = Program::MakeCompute("blurY", "blur.comp.glsl", {"VERTICAL_BLUR"});
	m_upsampleProgram = Program::MakeCompute("upsample", "upsample.comp.glsl");
	m_outputProgram   = Program::MakeCompute("compose", "compose.comp.glsl");

	glCreateTextures(GL_TEXTURE_2D, 1, &m_environment.iblDFG);

	// glTextureStorage2D(equirectangularTexture, levels, GL_RGB32F, w, h);
	glTextureStorage2D(m_environment.iblDFG, 1, GL_RGB32F, 128, 128);

	Timer timer;
	glTextureSubImage2D(m_environment.iblDFG, 0, 0, 0, 128, 128, GL_RGB, GL_FLOAT, PrecomputeDFG(128, 128, 1024).data());
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTextureParameteri(m_environment.iblDFG, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	FrameStats::Get()->ibl.precomputeDFG = timer.Tick();

	glCreateFramebuffers(2, m_fbos);

	Resize(initialSize);
}

void Renderer::Resize(const glm::vec2& newSize)
{
	if (m_framebufferSize != newSize)
	{
		if (glIsTexture(msaaRenderTexture))
		{
			glDeleteTextures(1, &msaaRenderTexture);
			glDeleteTextures(1, &resolveTexture);
			glDeleteTextures(1, &outputTexture);
			glDeleteTextures(1, bloomTextures);
			glDeleteTextures(1, &averageLuminanceTexture);
			glDeleteRenderbuffers(1, &msaaDepthRenderBuffer);
		}

		// Create MSAA texture and attach it to FBO
		glCreateTextures(GL_TEXTURE_2D_MULTISAMPLE, 1, &msaaRenderTexture);
		glTextureStorage2DMultisample(msaaRenderTexture, 4, GL_RGBA32F, newSize.x, newSize.y, GL_TRUE);
		glNamedFramebufferTexture(m_msaaFB, GL_COLOR_ATTACHMENT0, msaaRenderTexture, 0);

		// Create MSAA DS rendertarget and attach it to FBO
		glCreateRenderbuffers(1, &msaaDepthRenderBuffer);
		glNamedRenderbufferStorageMultisample(msaaDepthRenderBuffer, 4, GL_DEPTH24_STENCIL8, newSize.x, newSize.y);
		glNamedFramebufferRenderbuffer(m_msaaFB, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, msaaDepthRenderBuffer);

		if (glCheckNamedFramebufferStatus(m_msaaFB, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "MSAA framebuffer incomplete\n");
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &resolveTexture);
		glTextureStorage2D(resolveTexture, 1, GL_RGBA32F, newSize.x, newSize.y);
		glNamedFramebufferTexture(m_resolveFB, GL_COLOR_ATTACHMENT0, resolveTexture, 0);
		glTextureParameteri(resolveTexture, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(resolveTexture, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

		if (glCheckNamedFramebufferStatus(m_resolveFB, GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
		{
			fprintf(stderr, "Resolve framebuffer incomplete\n");
		}

		glCreateTextures(GL_TEXTURE_2D, 1, &outputTexture);
		glTextureStorage2D(outputTexture, 1, GL_RGBA8, newSize.x, newSize.y);

		i32 mipCount = log2(Min(newSize.x, newSize.y)) - 1;
		glCreateTextures(GL_TEXTURE_2D, 2, bloomTextures);
		glTextureStorage2D(bloomTextures[0], mipCount, GL_RGBA32F, newSize.x * 0.5, newSize.y * 0.5);
		glTextureStorage2D(bloomTextures[1], mipCount, GL_RGBA32F, newSize.x * 0.5, newSize.y * 0.5);

		glTextureParameteri(bloomTextures[0], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(bloomTextures[0], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(bloomTextures[0], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTextureParameteri(bloomTextures[0], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

		glTextureParameteri(bloomTextures[1], GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
		glTextureParameteri(bloomTextures[1], GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTextureParameteri(bloomTextures[1], GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
		glTextureParameteri(bloomTextures[1], GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

		glCreateTextures(GL_TEXTURE_2D, 1, &averageLuminanceTexture);

		m_framebufferSize = newSize;
	}
}

void Renderer::Render(const CameraInfos& camera, const std::vector<Model>& models)
{
	FrameStats* stats = FrameStats::Get();
	Timer       timer;
	Timer       frameTimer;

	Program::UpdateAllPrograms();
	stats->frame.updatePrograms = timer.Tick();

	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_msaaFB);

	glViewport(0, 0, m_framebufferSize.x, m_framebufferSize.y);

	glClearDepth(1.0f);
	glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthFunc(GL_LEQUAL);

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

	stats->frame.renderModels = timer.Tick();

	if (backgroundType != BackgroundType_None)
	{
		m_backgroundProgram->Bind();
		m_backgroundProgram->SetUniform("envmap", 0);
		m_backgroundProgram->SetUniform("miplevel", backgroundType == BackgroundType_Radiance ? backgroundMipLevel : 0);
		m_backgroundProgram->SetUniform("view", context.view);
		m_backgroundProgram->SetUniform("proj", context.proj);

		switch (backgroundType)
		{
			case BackgroundType_Cubemap:
				glBindTextureUnit(0, m_environment.envMap);
				break;

			case BackgroundType_Radiance:
				glBindTextureUnit(0, m_environment.radianceMap);
				break;

			case BackgroundType_Irradiance:
				glBindTextureUnit(0, m_environment.irradianceMap);
				break;
		}

		RenderCube();
	}

	stats->frame.background = timer.Tick();

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

	stats->frame.resolveMSAA = timer.Tick();

	glm::vec2 size = m_framebufferSize / 2.0f;

	// Zero buffer
	// std::vector<u8> zeros(m_framebufferSize.x * m_framebufferSize.y * sizeof(f32), 0);
	// for (i32 i = 0; i < bloomWidth; ++i, size /= 2.0f)
	// {
	// 	glClearTexSubImage(bloomTextures[0], i, 0, 0, 0, size.x, size.y, 1, GL_RGBA, GL_FLOAT, zeros.data());
	// 	glClearTexSubImage(bloomTextures[1], i, 0, 0, 0, size.x, size.y, 1, GL_RGBA, GL_FLOAT, zeros.data());
	// }

	// size = m_framebufferSize / 2.0f;

	// Highpass + downsample
	// m_highpassProgram->Bind();
	// m_highpassProgram->SetUniform("viewportSize", m_bloomBufferSize);
	// m_highpassProgram->SetUniform("threshold", bloomThreshold);

	// glBindImageTexture(0, resolveTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	// glBindImageTexture(1, bloomTextures[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	// glDispatchCompute(ceil(m_bloomBufferSize.x / 32), ceil(m_bloomBufferSize.y / 32), 1);
	stats->frame.highpassAndLuminance = timer.Tick();

	// size = m_framebufferSize / 2.0f;

	Timer bloomTimer;

	// Init loop
	m_blurXProgram->Bind();
	glBindImageTexture(0, resolveTexture, 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, bloomTextures[0], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	m_blurYProgram->Bind();
	glBindImageTexture(0, bloomTextures[0], 0, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, bloomTextures[1], 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	size /= 2.0;

	// And start it
	for (int i = 1; i < bloomWidth; ++i, size /= 2.0)
	{
		m_blurXProgram->Bind();
		glBindImageTexture(0, bloomTextures[1], i - 1, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
		glBindImageTexture(1, bloomTextures[0], i, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

		m_blurYProgram->Bind();
		glBindImageTexture(0, bloomTextures[0], i, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
		glBindImageTexture(1, bloomTextures[1], i, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	stats->frame.bloomDownsample = timer.Tick();

	// And upsample.
	// Init pass
	size *= 2.0;
	m_upsampleProgram->Bind();

	glBindImageTexture(0, bloomTextures[1], bloomWidth - 1, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(1, bloomTextures[1], bloomWidth - 2, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
	glBindImageTexture(2, bloomTextures[0], bloomWidth - 2, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
	glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	size *= 2.0;
	for (int i = bloomWidth - 3; i >= 0; --i, size *= 2)
	{
		glBindImageTexture(0, bloomTextures[0], i + 1, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
		glBindImageTexture(1, bloomTextures[1], i, GL_FALSE, 0, GL_READ_ONLY, GL_RGBA32F);
		glBindImageTexture(2, bloomTextures[0], i, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA32F);
		glDispatchCompute(ceil(size.x / 32), ceil(size.y / 32), 1);
		glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
	}

	stats->frame.bloomUpsample = timer.Tick();
	stats->frame.bloomTotal    = bloomTimer.Tick();

	// Final render
	m_outputProgram->Bind();
	m_outputProgram->SetUniform("viewportSize", m_framebufferSize);
	m_outputProgram->SetUniform("bloomAmount", bloomAmount);

	glBindImageTexture(0, outputTexture, 0, GL_FALSE, 0, GL_WRITE_ONLY, GL_RGBA8);
	glBindTextureUnit(1, resolveTexture);
	glBindTextureUnit(2, bloomTextures[0]);
	glBindTextureUnit(3, bloomTextures[1]);

	glDispatchCompute(m_framebufferSize.x / 32, m_framebufferSize.y / 32, 1);

	glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

	stats->frame.finalCompositing = timer.Tick();
	stats->renderTotal            = frameTimer.Tick();
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
    : indexCount(indices.size())
    , vertexCount(vertices.size())
    , indexType(GL_UNSIGNED_SHORT)
{
	SetData(vertices, indices);
}

Mesh::Mesh(const std::vector<Vertex>& vertices, const std::vector<GLuint>& indices)
    : indexCount(indices.size())
    , vertexCount(vertices.size())
    , indexType(GL_UNSIGNED_INT)
{
	SetData(vertices, indices);
}

Mesh::Mesh(const VertexDataInfos& vertexDataInfos, const IndexDataInfos& indexDataInfos)
    : indexCount(indexDataInfos.indexCount)
    , vertexCount(vertexDataInfos.bufferSize / vertexDataInfos.byteStride)
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

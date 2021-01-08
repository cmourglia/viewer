#include "core/defines.h"
#include "core/utils.h"
#include "renderer/program.h"
#include "renderer/material.h"
#include "renderer/environment.h"
#include "renderer/render_primitives.h"
#include "renderer/renderer.h"

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

static void MouseButtonCallback(GLFWwindow* window, i32 button, i32 action, i32 mods);
static void MouseMoveCallback(GLFWwindow* window, f64 x, f64 y);
static void KeyCallback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods);
static void WheelCallback(GLFWwindow* window, f64 x, f64 y);

static void FramebufferSizeCallback(GLFWwindow* window, i32 width, i32 height);

static void DropCallback(GLFWwindow* window, i32 count, const char** paths);

void DebugOutput(GLenum source, GLenum type, u32 id, GLenum severity, GLsizei length, const char* message, const void* userParam);

static i32 g_width, g_height;

enum RenderMode
{
	RenderMode_Default = 0,
	RenderMode_IBL_DFG,
	RenderMode_Count,
};

u32 g_renderMode = RenderMode_Default;

std::string GetFileExtension(const std::string& filename)
{
	return filename.substr(filename.find_last_of(".") + 1);
}

struct Camera
{
	f32 phi      = 0.0f;
	f32 theta    = 90.0f;
	f32 distance = 1.0f;

	glm::vec3 position;
	glm::vec3 center = glm::vec3(0.0f, 0.0f, 0.0f);
	glm::vec3 up     = glm::vec3(0.0f, 1.0f, 0.0f);

	glm::mat4 GetView()
	{
		const f32 x = distance * sinf(theta * ToRadians) * sinf(phi * ToRadians);
		const f32 y = distance * cosf(theta * ToRadians);
		const f32 z = distance * sinf(theta * ToRadians) * cosf(phi * ToRadians);

		position = glm::vec3(x, y, z) + center;

		return glm::lookAt(position, center, up);
	}
};

static std::unordered_map<u32, Program>        g_programs;
static std::unordered_map<std::string, GLuint> g_textures;

GLuint LoadTexture(const std::string& filename)
{
	auto it = g_textures.find(filename);
	if (it != g_textures.end())
	{
		return it->second;
	}

	stbi_set_flip_vertically_on_load(true);

	i32      w, h, c;
	uint8_t* data = stbi_load(filename.c_str(), &w, &h, &c, 0);

	stbi_set_flip_vertically_on_load(false);

	if (data == nullptr)
	{
		return 0;
	}

	GLuint texture;
	glCreateTextures(GL_TEXTURE_2D, 1, &texture);

	const i32 levels = log2f(Min(w, h));

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

static Camera g_camera;

void SetupUI(GLFWwindow* window);
void RenderUI(const std::vector<Model>& models);

f32 g_lastScroll = 0.0f;

f64 g_viewportX = 0.0, g_viewportY = 0.0;
f64 g_viewportW = 0.0, g_viewportH = 0.0;

static std::vector<Model> g_models;

Mesh* ProcessMesh(aiMesh* inputMesh, const aiScene* scene)
{
	std::vector<Vertex> vertices;
	std::vector<u32>    indices;

	vertices.reserve(inputMesh->mNumVertices);
	indices.reserve(inputMesh->mNumFaces * 3);

	const aiVector3D* inVertices  = inputMesh->mVertices;
	const aiVector3D* inNormals   = inputMesh->mNormals;
	const aiVector3D* inTexcoords = inputMesh->mTextureCoords[0];

	for (u32 index = 0; index < inputMesh->mNumVertices; ++index)
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
	for (u32 i = 0; i < inputMesh->mNumFaces; ++i)
	{
		const aiFace face = *inFaces++;
		for (u32 j = 0; j < face.mNumIndices; ++j)
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

	f32 metallic;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR, metallic))
	{
		material->hasMetallic = true;
		material->metallic    = metallic;
	}

	f32 roughness;
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

	for (u32 index = 0; index < node->mNumMeshes; ++index)
	{
		Model model;

		aiMesh*     inputMesh     = scene->mMeshes[node->mMeshes[index]];
		aiMaterial* inputMaterial = scene->mMaterials[inputMesh->mMaterialIndex];

		model.mesh           = ProcessMesh(scene->mMeshes[node->mMeshes[index]], scene);
		model.material       = ProcessMaterial(inputMaterial, scene, path);
		model.worldTransform = transform;
		g_models.push_back(model);
	}

	for (u32 index = 0; index < node->mNumChildren; ++index)
	{
		ProcessNode(node->mChildren[index], scene, transform, path);
	}
}

void LoadScene(const char* filename)
{
	Assimp::Importer importer;

	const u32 importerFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_OptimizeMeshes;
	const aiScene* scene    = importer.ReadFile(filename, importerFlags);

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

static Program* g_backgroundProgram;
static Mesh     g_cubeMesh;

glm::vec3 g_lightDirection    = glm::vec3(0, -1, 0);
bool      g_showIrradianceMap = false;

Environment* g_env;

i32 main()
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
	glfwSwapInterval(1);

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

	Renderer renderer;
	renderer.Initialize(glm::vec2(g_width, g_height));
	g_env = renderer.GetEnvironment();

	LoadEnvironment("resources/env/Frozen_Waterfall_Ref.hdr", g_env);

	// LoadScene("external/glTF-Sample-Models/2.0/DamagedHelmet/glTF/DamagedHelmet.gltf");
	LoadScene(R"(W:\glTF-Sample-Models\2.0\MetalRoughSpheres\glTF\MetalRoughSpheres.gltf)");

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

			i32 wx, wy;
			glfwGetWindowPos(window, &wx, &wy);

			vMin.x -= wx;
			vMin.y -= wy;
			vMax.x -= wx;
			vMax.y -= wy;

			g_viewportX = vMin.x;
			g_viewportY = vMin.y;
			g_viewportW = vMax.x - vMin.x;
			g_viewportH = vMax.y - vMin.y;

			static glm::vec2 lastSize(0, 0);
			const glm::vec2  size(g_viewportW, g_viewportH);

			if (size != lastSize)
			{
				renderer.Resize(size);
				lastSize = size;
			}

			CameraInfos cameraInfos = {
			    .view     = g_camera.GetView(),
			    .proj     = glm::perspective(60.0f * ToRadians, (f32)size.x / size.y, 0.001f, 100.0f),
			    .position = g_camera.position,
			};

			ImTextureID id;
			switch (g_renderMode)
			{
				default:
					id = (void*)(intptr_t)renderer.Render(cameraInfos, g_models);
					ImGui::Image(id, ImVec2(size.x, size.y), ImVec2(0, 1), ImVec2(1, 0));
			}
			// ImTextureID
		}
		ImGui::End();

		static i32 selectedEntity = -1;

		ImGui::Begin("Entities");
		{
			for (i32 i = 0; i < g_models.size(); ++i)
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

f64  lastX, lastY;
bool movingCamera = false;

inline bool inViewport(f64 x, f64 y)
{
	return (x >= g_viewportX && x <= (g_viewportX + g_viewportW)) && (y >= g_viewportY && y <= (g_viewportY + g_viewportH));
}

static void MouseButtonCallback(GLFWwindow* window, i32 button, i32 action, i32 mods)
{
	if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
	{
		f64 x, y;
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

static void MouseMoveCallback(GLFWwindow* window, f64 x, f64 y)
{
	if (movingCamera)
	{
		const f64 dx = 0.1f * (x - lastX);
		const f64 dy = 0.1f * (y - lastY);

		g_camera.phi += dx;
		g_camera.theta = Clamp(g_camera.theta + (f32)dy, 10.0f, 170.0f);

		lastX = x;
		lastY = y;
	}
}

static void KeyCallback(GLFWwindow* window, i32 key, i32 scancode, i32 action, i32 mods)
{
	if (key == GLFW_KEY_ESCAPE && action == GLFW_RELEASE)
	{
		glfwSetWindowShouldClose(window, GLFW_TRUE);
	}

	if ((key == GLFW_KEY_N) && ((mods & GLFW_MOD_CONTROL) == GLFW_MOD_CONTROL) && (action == GLFW_RELEASE))
	{
		g_renderMode = (g_renderMode + 1) % RenderMode_Count;
	}

	if (key == GLFW_KEY_F2 && action == GLFW_RELEASE)
	{
		static int swapInterval = 1;
		swapInterval ^= 1;

		glfwSwapInterval(swapInterval);
	}
}

static void WheelCallback(GLFWwindow* window, f64 x, f64 y)
{
	if (!movingCamera)
	{
		f64 mouseX, mouseY;
		glfwGetCursorPos(window, &mouseX, &mouseY);

		if (inViewport(mouseX, mouseY))
		{
			constexpr f32 minDistance = 0.01f;
			constexpr f32 maxDistance = 100.0f;

			const f32 multiplier = 2.5f * (g_camera.distance - minDistance) / (maxDistance - minDistance);

			const f32 distance = g_camera.distance - (f32)y * multiplier;

			g_camera.distance = Clamp(distance, minDistance, maxDistance);
		}
	}
}

static void FramebufferSizeCallback(GLFWwindow* window, i32 width, i32 height)
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

static void DropCallback(GLFWwindow* window, i32 count, const char** paths)
{
	for (i32 i = 0; i < count; ++i)
	{
		std::string ext = GetFileExtension(paths[i]);
		if (ext == "hdr")
		{
			LoadEnvironment(paths[i], g_env);
		}
		else
		{
			g_models.clear();
			LoadScene(paths[i]);
		}
	}
}

void APIENTRY DebugOutput(GLenum source, GLenum type, u32 id, GLenum severity, GLsizei length, const char* message, const void* userParam)
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

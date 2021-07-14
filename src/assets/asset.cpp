#include "assets/asset.h"

#include "core/utils.h"

#include "renderer/material.h"
#include "renderer/texture.h"
#include "renderer/renderer.h"
#include "renderer/frame_stats.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include <assimp/pbrmaterial.h>

#include <string>
#include <filesystem>

inline std::string TexturePath(const char* texture, const std::filesystem::path& path)
{
	std::filesystem::path texturePath(texture);
	if (texturePath.is_absolute())
	{
		return texturePath.string();
	}

	std::filesystem::path fullPath = path / texturePath;

	return fullPath.string();
}

inline Material* ProcessMaterial(aiMaterial* inputMaterial, const aiScene* scene, const std::filesystem::path& path)
{
	Material* material = new Material(inputMaterial->GetName().C_Str(), "pbr.vert.glsl", "pbr.frag.glsl");

	aiColor3D albedo;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_BASE_COLOR, albedo))
	{
		material->hasAlbedo = true;
		material->albedo    = glm::vec3(albedo.r, albedo.g, albedo.b);
	}

	f32 metallic;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_METALLIC_FACTOR, metallic))
	{
		material->hasMetallic = true;
		material->metallic    = metallic;
	}

	f32 roughness;
	if (AI_SUCCESS == inputMaterial->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness))
	{
		material->hasRoughness = true;
		material->roughness    = roughness;
	}

	aiString albedoTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(AI_MATKEY_BASE_COLOR_TEXTURE, &albedoTexture))
	{
		material->hasAlbedoTexture = true;
		material->albedoTexture    = LoadTexture(TexturePath(albedoTexture.C_Str(), path));
	}

	aiString metallicTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(AI_MATKEY_METALLIC_TEXTURE, &metallicTexture))
	{
		material->hasMetallicTexture = true;
		material->metallicTexture    = LoadTexture(TexturePath(metallicTexture.C_Str(), path));
	}

	aiString roughnessTexture;
	if (AI_SUCCESS == inputMaterial->GetTexture(AI_MATKEY_ROUGHNESS_TEXTURE, &roughnessTexture))
	{
		material->hasRoughnessTexture = true;
		material->roughnessTexture    = LoadTexture(TexturePath(roughnessTexture.C_Str(), path));
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

inline void ProcessNode(aiNode*                      node,
                        const aiScene*               scene,
                        const glm::mat4&             parentTransform,
                        const std::filesystem::path& path,
                        std::vector<Model>*          loadedModels)
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
		loadedModels->push_back(std::move(model));
	}

	for (u32 index = 0; index < node->mNumChildren; ++index)
	{
		ProcessNode(node->mChildren[index], scene, transform, path, loadedModels);
	}
}

std::vector<Model> LoadScene(const char* filename)
{
	Timer timer;

	Assimp::Importer importer;

	const u32 importerFlags = aiProcess_Triangulate | aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_OptimizeMeshes;
	const aiScene* scene    = importer.ReadFile(filename, importerFlags);

	if ((nullptr == scene) || (0 != (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE)) || (nullptr == scene->mRootNode))
	{
		fprintf(stderr, "Could not load file %s\n", filename);
		return {};
	}

	std::vector<Model>    models;
	std::filesystem::path p;
	p = filename;
	p.remove_filename();
	ProcessNode(scene->mRootNode, scene, glm::mat4(1.0f), p, &models);

	FrameStats::Get()->loadScene = timer.Tick();

	return models;
}

#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"

#include "core/Log.h"
#include "core/scene/SceneWorld.h"

#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <utility>

#include <glm/glm.hpp>

namespace hybrid::assets
{

    void LogSceneSummary(const aiScene *scene);

    namespace
    {
        bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                char a = lhs[i];
                char b = rhs[i];
                if (a == b)
                {
                    continue;
                }
                if (a >= 'A' && a <= 'Z')
                {
                    a = static_cast<char>(a - 'A' + 'a');
                }
                if (b >= 'A' && b <= 'Z')
                {
                    b = static_cast<char>(b - 'A' + 'a');
                }
                if (a != b)
                {
                    return false;
                }
            }
            return true;
        }

        std::string ResolveTexturePath(const std::string &scene_path, const std::string &texture_path)
        {
            std::filesystem::path tex(texture_path);
            if (tex.is_absolute())
            {
                return tex.string();
            }
            std::filesystem::path base(scene_path);
            base = base.parent_path();
            return (base / tex).string();
        }

        hybrid::core::scene::TextureWrap ToWrap(aiTextureMapMode mode)
        {
            switch (mode)
            {
            case aiTextureMapMode_Clamp:
                return hybrid::core::scene::TextureWrap::ClampToEdge;
            case aiTextureMapMode_Mirror:
                return hybrid::core::scene::TextureWrap::MirroredRepeat;
            case aiTextureMapMode_Wrap:
            default:
                return hybrid::core::scene::TextureWrap::Repeat;
            }
        }

        bool FillMaterialTexture(const aiMaterial &material,
                                 aiTextureType type,
                                 const std::string &scene_path,
                                 hybrid::core::scene::TextureColorSpace color_space,
                                 AssetManager &assets,
                                 hybrid::core::scene::MaterialTexture &out_texture)
        {
            if (material.GetTextureCount(type) == 0)
            {
                return false;
            }

            aiString path;
            aiTextureMapping mapping = aiTextureMapping_UV;
            unsigned int uv_index = 0;
            float blend = 1.0f;
            aiTextureOp op = aiTextureOp_Multiply;
            std::array<aiTextureMapMode, 2> map_modes = {aiTextureMapMode_Wrap, aiTextureMapMode_Wrap};
            if (material.GetTexture(type, 0, &path, &mapping, &uv_index, &blend, &op, map_modes.data()) != AI_SUCCESS)
            {
                return false;
            }

            const std::string raw_path = path.C_Str();
            if (raw_path.empty())
            {
                return false;
            }

            if (!raw_path.empty() && raw_path[0] == '*')
            {
                return false;
            }

            const std::string resolved_path = ResolveTexturePath(scene_path, raw_path);
            out_texture.name = raw_path;
            out_texture.image = assets.LoadHandle<assets::ImageAsset>(resolved_path);
            out_texture.texcoord = static_cast<int>(uv_index);
            out_texture.color_space = color_space;
            out_texture.sampler.wrap_s = ToWrap(map_modes[0]);
            out_texture.sampler.wrap_t = ToWrap(map_modes[1]);

#ifdef AI_MATKEY_UVTRANSFORM
            if (aiUVTransform uv_transform; material.Get(AI_MATKEY_UVTRANSFORM(type, 0), uv_transform) == AI_SUCCESS)
            {
                if (uv_transform.mScaling.x != 1.0f || uv_transform.mScaling.y != 1.0f ||
                    uv_transform.mTranslation.x != 0.0f || uv_transform.mTranslation.y != 0.0f ||
                    uv_transform.mRotation != 0.0f)
                {
                    out_texture.has_transform = true;
                    out_texture.scale = {uv_transform.mScaling.x, uv_transform.mScaling.y};
                    out_texture.offset = {uv_transform.mTranslation.x, uv_transform.mTranslation.y};
                    out_texture.rotation = uv_transform.mRotation;
                }
            }
#endif

            return out_texture.image.IsValid();
        }

        hybrid::core::scene::Transform ToTransform(const aiMatrix4x4 &matrix)
        {
            aiVector3D scaling;
            aiQuaternion rotation;
            aiVector3D translation;
            matrix.Decompose(scaling, rotation, translation);

            hybrid::core::scene::Transform result{};
            result.translation = {translation.x, translation.y, translation.z};
            result.rotation = {rotation.w, rotation.x, rotation.y, rotation.z};
            result.scale = {scaling.x, scaling.y, scaling.z};
            return result;
        }

        void AppendNode(const aiNode &node,
                        entt::entity parent_entity,
                        const std::vector<assets::AssetHandle<hybrid::core::scene::MeshAsset>> &mesh_handles,
                        hybrid::core::scene::SceneWorld &scene)
        {
            const std::string node_name = node.mName.C_Str();
            entt::entity entity = scene.CreateEntity(node_name);
            auto &registry = scene.Registry();

            if (auto *transform = registry.try_get<hybrid::core::scene::TransformComponent>(entity))
            {
                transform->local = ToTransform(node.mTransformation);
                transform->dirty = true;
            }

            if (parent_entity != entt::null)
            {
                scene.SetParent(entity, parent_entity);
            }

            if (node.mNumMeshes == 1)
            {
                const unsigned int mesh_index = node.mMeshes[0];
                if (mesh_index < mesh_handles.size())
                {
                    registry.emplace<hybrid::core::scene::MeshRendererComponent>(
                        entity, hybrid::core::scene::MeshRendererComponent{mesh_handles[mesh_index]});
                }
                else
                {
                    LOG_WARN("[AssimpSceneLoader] Node '" + node_name +
                             "' mesh index out of range: " + std::to_string(mesh_index) +
                             " (max " + std::to_string(mesh_handles.size()) + ")");
                }
            }
            else if (node.mNumMeshes > 1)
            {
                for (unsigned int i = 0; i < node.mNumMeshes; ++i)
                {
                    const unsigned int mesh_index = node.mMeshes[i];
                    if (mesh_index >= mesh_handles.size())
                    {
                        LOG_WARN("[AssimpSceneLoader] Node '" + node_name +
                                 "' mesh index out of range: " + std::to_string(mesh_index) +
                                 " (max " + std::to_string(mesh_handles.size()) + ")");
                        continue;
                    }

                    const std::string mesh_name = node_name.empty()
                                                      ? ("mesh_" + std::to_string(i))
                                                      : (node_name + "_mesh_" + std::to_string(i));
                    entt::entity mesh_entity = scene.CreateEntity(mesh_name);
                    if (auto *mesh_transform = registry.try_get<hybrid::core::scene::TransformComponent>(mesh_entity))
                    {
                        mesh_transform->local = hybrid::core::scene::Transform{};
                        mesh_transform->dirty = true;
                    }
                    scene.SetParent(mesh_entity, entity);
                    registry.emplace<hybrid::core::scene::MeshRendererComponent>(
                        mesh_entity, hybrid::core::scene::MeshRendererComponent{mesh_handles[mesh_index]});
                }
            }

            for (unsigned int i = 0; i < node.mNumChildren; ++i)
            {
                AppendNode(*node.mChildren[i], entity, mesh_handles, scene);
            }
        }

        hybrid::core::scene::Aabb ComputeBounds(const std::vector<hybrid::core::scene::Vertex> &vertices)
        {
            hybrid::core::scene::Aabb bounds{};
            if (vertices.empty())
            {
                return bounds;
            }

            bounds.min = vertices.front().position;
            bounds.max = vertices.front().position;
            bounds.valid = true;

            for (const auto &vertex : vertices)
            {
                bounds.min = glm::min(bounds.min, vertex.position);
                bounds.max = glm::max(bounds.max, vertex.position);
            }

            return bounds;
        }
    } // namespace

    AssimpSceneLoader::AssimpSceneLoader(AssetManager *asset_manager)
        : m_assets(asset_manager)
    {
    }

    std::type_index AssimpSceneLoader::Type() const
    {
        return std::type_index(typeid(hybrid::core::scene::SceneWorld));
    }

    bool AssimpSceneLoader::SupportsExtension(std::string_view extension) const
    {
        return EqualsIgnoreCase(extension, "gltf") || EqualsIgnoreCase(extension, "glb");
    }

    std::shared_ptr<void> AssimpSceneLoader::Load(const AssetLoadRequest &request, IAssetDataSource *data_source)
    {
        if (!data_source || !m_assets)
        {
            return {};
        }

        LOG_INFO("[AssimpSceneLoader] Loading glTF scene file");

        std::string hint = request.extension;
        if (!hint.empty() && hint[0] == '.')
        {
            hint.erase(0, 1);
        }
        if (hint.empty())
        {
            std::filesystem::path path_hint(request.path);
            hint = path_hint.extension().string();
            if (!hint.empty() && hint[0] == '.')
            {
                hint.erase(0, 1);
            }
        }

        Assimp::Importer importer;
        std::string resolved_path = request.path;
        if (const auto *disk_source = dynamic_cast<const assets::DiskAssetDataSource *>(data_source))
        {
            resolved_path = disk_source->ResolvePath(request.path);
        }

        const std::filesystem::path base_path(resolved_path);
        const std::string base_dir = base_path.parent_path().make_preferred().string();
        const unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
                                   aiProcess_ImproveCacheLocality | aiProcess_GenNormals |
                                   aiProcess_CalcTangentSpace;
        const aiScene *scene = importer.ReadFile(resolved_path.c_str(), flags);
        if (!scene)
        {
            LOG_ERROR(std::string("[AssimpSceneLoader] Failed to parse scene file: ") + importer.GetErrorString());
            return {};
        }

        LogSceneSummary(scene);

        LOG_INFO("[AssimpSceneLoader] \t Loading materials...");

        auto result = std::make_shared<hybrid::core::scene::SceneWorld>();

        std::vector<assets::AssetHandle<hybrid::core::scene::MaterialAsset>> material_handles;
        material_handles.reserve(scene->mNumMaterials);

        for (unsigned int i = 0; i < scene->mNumMaterials; ++i)
        {
            const aiMaterial *material = scene->mMaterials[i];
            if (!material)
            {
                material_handles.emplace_back();
                continue;
            }

            hybrid::core::scene::MaterialAsset material_asset{};

            if (aiString material_name; material->Get(AI_MATKEY_NAME, material_name) == AI_SUCCESS)
            {
                material_asset.name = material_name.C_Str();
            }

            if (aiColor4D base_color; aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &base_color) == AI_SUCCESS ||
                                      aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &base_color) == AI_SUCCESS)
            {
                material_asset.base_color_factor = {base_color.r, base_color.g, base_color.b, base_color.a};
            }

            if (float metallic = material_asset.metallic_factor; aiGetMaterialFloat(material, AI_MATKEY_METALLIC_FACTOR, &metallic) == AI_SUCCESS)
            {
                material_asset.metallic_factor = metallic;
            }

            if (float roughness = material_asset.roughness_factor; aiGetMaterialFloat(material, AI_MATKEY_ROUGHNESS_FACTOR, &roughness) == AI_SUCCESS)
            {
                material_asset.roughness_factor = roughness;
            }

            if (aiColor4D emissive; aiGetMaterialColor(material, AI_MATKEY_COLOR_EMISSIVE, &emissive) == AI_SUCCESS)
            {
                material_asset.emissive_factor = {emissive.r, emissive.g, emissive.b};
            }

            if (int two_sided = 0; aiGetMaterialInteger(material, AI_MATKEY_TWOSIDED, &two_sided) == AI_SUCCESS)
            {
                material_asset.double_sided = two_sided != 0;
            }

            if (aiString alpha_mode; material->Get("$mat.gltf.alphaMode", 0, 0, alpha_mode) == AI_SUCCESS)
            {
                const std::string mode = alpha_mode.C_Str();
                if (mode == "MASK")
                {
                    material_asset.alpha_mode = hybrid::core::scene::AlphaMode::Mask;
                }
                else if (mode == "BLEND")
                {
                    material_asset.alpha_mode = hybrid::core::scene::AlphaMode::Blend;
                }
            }

            if (float alpha_cutoff = material_asset.alpha_cutoff; material->Get("$mat.gltf.alphaCutoff", 0, 0, alpha_cutoff) == AI_SUCCESS)
            {
                material_asset.alpha_cutoff = alpha_cutoff;
            }

            FillMaterialTexture(*material, aiTextureType_BASE_COLOR, request.path,
                                hybrid::core::scene::TextureColorSpace::Srgb, *m_assets,
                                material_asset.base_color_texture);

            if (!FillMaterialTexture(*material, aiTextureType_METALNESS, request.path,
                                     hybrid::core::scene::TextureColorSpace::Linear, *m_assets,
                                     material_asset.metallic_roughness_texture))
            {
                FillMaterialTexture(*material, aiTextureType_DIFFUSE_ROUGHNESS, request.path,
                                    hybrid::core::scene::TextureColorSpace::Linear, *m_assets,
                                    material_asset.metallic_roughness_texture);
            }

            FillMaterialTexture(*material, aiTextureType_NORMALS, request.path,
                                hybrid::core::scene::TextureColorSpace::Linear, *m_assets,
                                material_asset.normal_texture);

            FillMaterialTexture(*material, aiTextureType_LIGHTMAP, request.path,
                                hybrid::core::scene::TextureColorSpace::Linear, *m_assets,
                                material_asset.occlusion_texture);

            FillMaterialTexture(*material, aiTextureType_EMISSIVE, request.path,
                                hybrid::core::scene::TextureColorSpace::Srgb, *m_assets,
                                material_asset.emissive_texture);

            const std::string asset_path = request.path + "#material/" + std::to_string(i);
            auto material_handle = m_assets->Add(asset_path, std::make_shared<hybrid::core::scene::MaterialAsset>(
                                                                 std::move(material_asset)));
            material_handles.push_back(material_handle);
        }

        std::vector<assets::AssetHandle<hybrid::core::scene::MeshAsset>> mesh_handles;
        mesh_handles.reserve(scene->mNumMeshes);

        LOG_INFO("[AssimpSceneLoader] \t Loading meshes...");

        for (unsigned int i = 0; i < scene->mNumMeshes; ++i)
        {
            const aiMesh *mesh = scene->mMeshes[i];
            if (!mesh)
            {
                mesh_handles.emplace_back();
                continue;
            }

            hybrid::core::scene::MeshAsset mesh_asset{};
            mesh_asset.name = mesh->mName.C_Str();

            hybrid::core::scene::MeshPrimitive primitive{};
            primitive.vertices.resize(mesh->mNumVertices);

            for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            {
                auto &vertex = primitive.vertices[v];
                const aiVector3D &pos = mesh->mVertices[v];
                vertex.position = {pos.x, pos.y, pos.z};

                if (mesh->HasNormals())
                {
                    const aiVector3D &n = mesh->mNormals[v];
                    vertex.normal = {n.x, n.y, n.z};
                }

                if (mesh->HasTangentsAndBitangents())
                {
                    const aiVector3D &t = mesh->mTangents[v];
                    vertex.tangent = {t.x, t.y, t.z, 1.0f};
                }

                if (mesh->HasTextureCoords(0))
                {
                    const aiVector3D &uv = mesh->mTextureCoords[0][v];
                    vertex.uv0 = {uv.x, uv.y};
                }

                if (mesh->HasTextureCoords(1))
                {
                    const aiVector3D &uv = mesh->mTextureCoords[1][v];
                    vertex.uv1 = {uv.x, uv.y};
                }

                if (mesh->HasVertexColors(0))
                {
                    const aiColor4D &c = mesh->mColors[0][v];
                    vertex.color0 = {c.r, c.g, c.b, c.a};
                }
            }

            if (mesh->mNumVertices <= 0)
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) + " has zero vertices");
            }

            primitive.indices.reserve(mesh->mNumFaces * 3);
            unsigned int skipped_faces = 0;
            for (unsigned int f = 0; f < mesh->mNumFaces; ++f)
            {
                const aiFace &face = mesh->mFaces[f];
                if (face.mNumIndices != 3)
                {
                    ++skipped_faces;
                    continue;
                }
                primitive.indices.push_back(face.mIndices[0]);
                primitive.indices.push_back(face.mIndices[1]);
                primitive.indices.push_back(face.mIndices[2]);
            }
            if (mesh->mMaterialIndex < material_handles.size())
            {
                primitive.material = material_handles[mesh->mMaterialIndex];
            }
            if (!primitive.material.IsValid())
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) + " material handle invalid");
            }

            primitive.bounds = ComputeBounds(primitive.vertices);
            if (!primitive.bounds.valid)
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) + " has invalid bounds");
            }
            mesh_asset.primitives.push_back(std::move(primitive));
            mesh_asset.bounds = mesh_asset.primitives.front().bounds;

            const std::string asset_path = request.path + "#mesh/" + std::to_string(i);
            auto mesh_handle = m_assets->Add(asset_path, std::make_shared<hybrid::core::scene::MeshAsset>(
                                                             std::move(mesh_asset)));
            mesh_handles.push_back(mesh_handle);
        }

        LOG_INFO("[AssimpSceneLoader] \t Building scene tree...");

        if (scene->mRootNode)
        {
            AppendNode(*scene->mRootNode, entt::null, mesh_handles, *result);
        }

        LOG_INFO("[AssimpSceneLoader] glTF scene loaded");
        return result;
    }

    void LogSceneSummary(const aiScene *scene)
    {
        if (!scene)
        {
            LOG_INFO("[AssimpSceneLoader] Scene summary: <null>");
            return;
        }

        const std::function<unsigned int(const aiNode *)> CountNodes = [&](const aiNode *node) -> unsigned int
        {
            if (!node)
            {
                return 0;
            }

            unsigned int count = 1;
            for (unsigned int i = 0; i < node->mNumChildren; ++i)
            {
                count += CountNodes(node->mChildren[i]);
            }
            return count;
        };

        const unsigned int node_count = CountNodes(scene->mRootNode);

        LOG_INFO("[AssimpSceneLoader] Scene summary:");
        LOG_INFO("  \t Nodes: " + std::to_string(node_count));
        LOG_INFO("  \t Materials: " + std::to_string(scene->mNumMaterials));
        LOG_INFO("  \t Meshes: " + std::to_string(scene->mNumMeshes));
        LOG_INFO("  \t Embedded textures: " + std::to_string(scene->mNumTextures));
        LOG_INFO("  \t Animations: " + std::to_string(scene->mNumAnimations));
        LOG_INFO("  \t Cameras: " + std::to_string(scene->mNumCameras));
        LOG_INFO("  \t Lights: " + std::to_string(scene->mNumLights));
    }

} // namespace hybrid::assets

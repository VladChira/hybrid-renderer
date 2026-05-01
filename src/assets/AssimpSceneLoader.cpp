#include "assets/AssimpSceneLoader.h"
#include "assets/DiskAssetDataSource.h"
#include "assets/AssetUtils.h"

#include "core/Log.h"


#include <assimp/Importer.hpp>
#include <assimp/config.h>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <future>
#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

namespace hybrid::assets
{
    namespace
    {
        glm::quat RotationFromTo(const glm::vec3 &from, const glm::vec3 &to)
        {
            const glm::vec3 from_normalized = NormalizeOrDefault(from, glm::vec3(0.0f, -1.0f, 0.0f));
            const glm::vec3 to_normalized = NormalizeOrDefault(to, glm::vec3(0.0f, -1.0f, 0.0f));
            const float cosine = glm::clamp(glm::dot(from_normalized, to_normalized), -1.0f, 1.0f);

            if (cosine >= 1.0f - 1e-6f)
            {
                return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
            }

            if (cosine <= -1.0f + 1e-6f)
            {
                const glm::vec3 fallback_axis = std::abs(from_normalized.y) < 0.99f
                                                    ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                    : glm::vec3(1.0f, 0.0f, 0.0f);
                const glm::vec3 axis = NormalizeOrDefault(glm::cross(from_normalized, fallback_axis),
                                                          glm::vec3(1.0f, 0.0f, 0.0f));
                return glm::angleAxis(glm::pi<float>(), axis);
            }

            const glm::vec3 axis = NormalizeOrDefault(glm::cross(from_normalized, to_normalized),
                                                      glm::vec3(1.0f, 0.0f, 0.0f));
            const float angle = std::acos(cosine);
            return glm::angleAxis(angle, axis);
        }

        void AppendNode(const aiNode &node,
                        entt::entity parent_entity,
                        const std::vector<assets::AssetHandle<hybrid::core::scene::MeshAsset>> &mesh_handles,
                        const std::unordered_map<std::string, const aiCamera *> &cameras_by_node_name,
                        std::unordered_map<std::string, entt::entity> &entities_by_name,
                        hybrid::core::scene::SceneWorld &scene)
        {
            const std::string node_name = node.mName.C_Str();
            entt::entity entity = scene.CreateEntity(node_name);

            if (!node_name.empty() && !entities_by_name.try_emplace(node_name, entity).second)
            {
                LOG_WARN("[AssimpSceneLoader] Duplicate node name for entity lookup: '" + node_name + "', keeping first instance");
            }

            scene.SetLocalTransform(entity, ToTransform(node.mTransformation));

            if (parent_entity != entt::null)
            {
                scene.SetParent(entity, parent_entity);
            }

            if (const auto it = cameras_by_node_name.find(node_name); it != cameras_by_node_name.end() && it->second != nullptr)
            {
                const aiCamera &camera = *it->second;
                hybrid::core::scene::CameraComponent camera_component{};
                if (camera.mHorizontalFOV > 0.0f)
                {
                    camera_component.horizontal_fov_radians = camera.mHorizontalFOV;
                }
                if (camera.mClipPlaneNear > 0.0f)
                {
                    camera_component.near_plane = camera.mClipPlaneNear;
                }
                if (camera.mClipPlaneFar > camera_component.near_plane)
                {
                    camera_component.far_plane = camera.mClipPlaneFar;
                }

                scene.AddCamera(entity, camera_component);
                scene.SetCameraTarget(entity, false, entt::null);
            }

            if (node.mNumMeshes == 1)
            {
                const unsigned int mesh_index = node.mMeshes[0];
                if (mesh_index < mesh_handles.size())
                {
                    scene.AddMeshRenderer(entity, hybrid::core::scene::MeshRendererComponent{mesh_handles[mesh_index]});
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
                    scene.SetLocalTransform(mesh_entity, hybrid::core::scene::Transform{});
                    scene.SetParent(mesh_entity, entity);
                    scene.AddMeshRenderer(mesh_entity, hybrid::core::scene::MeshRendererComponent{mesh_handles[mesh_index]});
                }
            }

            for (unsigned int i = 0; i < node.mNumChildren; ++i)
            {
                AppendNode(*node.mChildren[i], entity, mesh_handles, cameras_by_node_name, entities_by_name, scene);
            }
        }

        void AppendLights(const aiScene &ai_scene,
                          std::unordered_map<std::string, entt::entity> &entities_by_name,
                          AssetManager *assets,
                          hybrid::core::scene::SceneWorld &scene)
        {
            LOG_INFO("[AssimpSceneLoader] \t Processing lights...");
            bool has_hdri_light = false;
            const std::string default_hdri_path = "hdris/shanghai_bund_2k.hdr";
            assets::AssetHandle<assets::ImageAsset> default_hdri{};
            if (assets != nullptr)
            {
                default_hdri = assets->LoadHandle<assets::ImageAsset>(default_hdri_path);
                if (!default_hdri.IsValid())
                {
                    LOG_WARN("[AssimpSceneLoader] Failed to load default HDRI");
                }
            }

            for (unsigned int i = 0; i < ai_scene.mNumLights; ++i)
            {
                const aiLight *light = ai_scene.mLights[i];
                if (light == nullptr)
                {
                    continue;
                }

                const std::string source_name = light->mName.C_Str();
                const std::string fallback_name = "Light_" + std::to_string(i);
                const std::string entity_name = source_name.empty() ? fallback_name : source_name;

                entt::entity light_entity = entt::null;
                if (!source_name.empty())
                {
                    const auto it = entities_by_name.find(source_name);
                    if (it != entities_by_name.end())
                    {
                        light_entity = it->second;
                    }
                }

                if (light_entity == entt::null)
                {
                    light_entity = scene.CreateEntity(entity_name);
                    if (!source_name.empty())
                    {
                        entities_by_name.try_emplace(source_name, light_entity);
                    }
                    scene.SetLocalTranslation(light_entity, ToVec3(light->mPosition));
                }

                const glm::vec3 color = ToVec3(light->mColorDiffuse);
                hybrid::core::scene::LightCommonComponent common{};
                common.color = color;
                common.intensity = 1.0f;
                scene.RemoveLight(light_entity);

                switch (light->mType)
                {
                case aiLightSource_POINT:
                {
                    hybrid::core::scene::PointLightComponent point{};
                    point.attenuation_constant = light->mAttenuationConstant;
                    point.attenuation_linear = light->mAttenuationLinear;
                    point.attenuation_quadratic = light->mAttenuationQuadratic;
                    if (light->mAttenuationQuadratic > 0.0f)
                    {
                        point.range = std::sqrt(1.0f / light->mAttenuationQuadratic);
                    }
                    scene.AddPointLight(light_entity, common, point);
                    break;
                }
                case aiLightSource_DIRECTIONAL:
                {
                    scene.AddDirectionalLight(light_entity, common, hybrid::core::scene::DirectionalLightComponent{});
                    const glm::vec3 imported_direction =
                        NormalizeOrDefault(ToVec3(light->mDirection), glm::vec3(0.0f, -1.0f, 0.0f));
                    scene.SetLocalRotation(light_entity, RotationFromTo(glm::vec3(0.0f, -1.0f, 0.0f), imported_direction));
                    break;
                }
                case aiLightSource_AREA:
                {
                    hybrid::core::scene::AreaLightComponent area{};
                    area.direction = NormalizeOrDefault(ToVec3(light->mDirection), glm::vec3(0.0f, -1.0f, 0.0f));
                    area.size = glm::vec2(1.0f);
                    scene.AddAreaLight(light_entity, common, area);
                    break;
                }
                case aiLightSource_AMBIENT:
                {
                    if (has_hdri_light)
                    {
                        LOG_WARN("[AssimpSceneLoader] Skipping extra ambient/HDRI light '" + entity_name + "': only one HDRI light is supported.");
                        break;
                    }

                    hybrid::core::scene::HdriLightComponent ambient{};
                    ambient.yaw_radians = 0.0f;
                    ambient.texture = default_hdri;
                    ambient.texture_path = default_hdri_path;
                    scene.AddHdriLight(light_entity, common, ambient);
                    has_hdri_light = true;
                    break;
                }
                default:
                    LOG_INFO("[AssimpSceneLoader] Skipping unsupported light type for '" + entity_name + "'");
                    break;
                }
            }
            if (!has_hdri_light)
            {
                const entt::entity default_hdri_entity = scene.CreateEntity("Default HDRI");
                hybrid::core::scene::LightCommonComponent common{};
                common.intensity = 1.0f;

                hybrid::core::scene::HdriLightComponent ambient{};
                ambient.yaw_radians = 0.0f;
                ambient.texture = default_hdri;
                ambient.texture_path = default_hdri_path;
                scene.AddHdriLight(default_hdri_entity, common, ambient);
            }
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

        const unsigned int flags = aiProcess_Triangulate | aiProcess_CalcTangentSpace;
        const aiScene *scene = importer.ReadFile(resolved_path.c_str(), flags);
        if (!scene)
        {
            LOG_ERROR(std::string("[AssimpSceneLoader] Failed to parse scene file: ") + importer.GetErrorString());
            return {};
        }

        LogSceneSummary(scene);

        LOG_INFO("[AssimpSceneLoader] \t Loading materials...");

        auto result = std::make_shared<hybrid::core::scene::SceneWorld>();

        std::vector<assets::AssetHandle<hybrid::core::scene::MaterialAsset>> material_handles(scene->mNumMaterials);
        AssimpTextureCache texture_cache(m_assets, request.path);

        auto load_material = [&](unsigned int i) -> assets::AssetHandle<hybrid::core::scene::MaterialAsset>
        {
            const aiMaterial *material = scene->mMaterials[i];
            if (!material)
            {
                return {};
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

            FillMaterialTexture(*material, aiTextureType_BASE_COLOR,
                                hybrid::core::scene::TextureColorSpace::Srgb,
                                texture_cache,
                                material_asset.base_color_texture);

            if (!FillMaterialTexture(*material, aiTextureType_METALNESS,
                                     hybrid::core::scene::TextureColorSpace::Linear,
                                     texture_cache,
                                     material_asset.metallic_roughness_texture))
            {
                FillMaterialTexture(*material, aiTextureType_DIFFUSE_ROUGHNESS,
                                    hybrid::core::scene::TextureColorSpace::Linear,
                                    texture_cache,
                                    material_asset.metallic_roughness_texture);
            }

            FillMaterialTexture(*material, aiTextureType_NORMALS,
                                hybrid::core::scene::TextureColorSpace::Linear,
                                texture_cache,
                                material_asset.normal_texture);

            FillMaterialTexture(*material, aiTextureType_LIGHTMAP,
                                hybrid::core::scene::TextureColorSpace::Linear,
                                texture_cache,
                                material_asset.occlusion_texture);

            FillMaterialTexture(*material, aiTextureType_EMISSIVE,
                                hybrid::core::scene::TextureColorSpace::Srgb,
                                texture_cache,
                                material_asset.emissive_texture);

            const std::string asset_path = request.path + "#material/" + std::to_string(i);
            return m_assets->Add(asset_path, std::make_shared<hybrid::core::scene::MaterialAsset>(
                                                 std::move(material_asset)));
        };

        const unsigned int material_count = scene->mNumMaterials;
        const unsigned int material_hw_threads = std::min(4u, std::max(1u, std::thread::hardware_concurrency() - 1));
        if (const unsigned int material_worker_count = std::min(material_count, material_hw_threads); material_worker_count <= 1)
        {
            for (unsigned int i = 0; i < material_count; ++i)
            {
                material_handles[i] = load_material(i);
            }
        }
        else
        {
            std::vector<std::future<void>> material_workers;
            material_workers.reserve(material_worker_count);

            LOG_INFO("[AssimpSceneLoader] \t Processing materials across " + std::to_string(material_worker_count) + " workers");

            for (unsigned int worker = 0; worker < material_worker_count; ++worker)
            {
                material_workers.emplace_back(std::async(std::launch::async, [&, worker]()
                                                         {
                                                             for (unsigned int i = worker; i < material_count; i += material_worker_count)
                                                             {
                                                                 material_handles[i] = load_material(i);
                                                             } }));
            }

            for (auto &worker : material_workers)
            {
                worker.get();
            }
        }

        std::vector<assets::AssetHandle<hybrid::core::scene::MeshAsset>> mesh_handles(scene->mNumMeshes);
        std::atomic<unsigned int> meshes_missing_normals{0};
        std::atomic<unsigned int> meshes_missing_tangents{0};

        LOG_INFO("[AssimpSceneLoader] \t Loading meshes...");

        auto load_mesh = [&](unsigned int i) -> assets::AssetHandle<hybrid::core::scene::MeshAsset>
        {
            const aiMesh *mesh = scene->mMeshes[i];
            if (!mesh)
            {
                return {};
            }

            hybrid::core::scene::MeshAsset mesh_asset{};
            mesh_asset.name = mesh->mName.C_Str();

            hybrid::core::scene::MeshPrimitive primitive{};
            primitive.vertices.resize(mesh->mNumVertices);
            const bool has_normals = mesh->HasNormals();
            const bool has_tangents = mesh->HasTangentsAndBitangents();
            const bool has_uv0 = mesh->HasTextureCoords(0);
            const bool has_uv1 = mesh->HasTextureCoords(1);
            const bool has_color0 = mesh->HasVertexColors(0);

            if (!has_normals)
            {
                meshes_missing_normals.fetch_add(1, std::memory_order_relaxed);
            }
            if (!has_tangents)
            {
                meshes_missing_tangents.fetch_add(1, std::memory_order_relaxed);
            }

            for (unsigned int v = 0; v < mesh->mNumVertices; ++v)
            {
                auto &vertex = primitive.vertices[v];
                const aiVector3D &pos = mesh->mVertices[v];
                vertex.position = {pos.x, pos.y, pos.z};

                if (has_normals)
                {
                    const aiVector3D &n = mesh->mNormals[v];
                    vertex.normal = {n.x, n.y, n.z};
                }

                if (has_tangents)
                {
                    const aiVector3D &t = mesh->mTangents[v];
                    float handedness = 1.0f;
                    if (mesh->mBitangents != nullptr)
                    {
                        const aiVector3D &n = mesh->mNormals[v];
                        const aiVector3D &b = mesh->mBitangents[v];
                        const glm::vec3 tangent = {t.x, t.y, t.z};
                        const glm::vec3 normal = {n.x, n.y, n.z};
                        const glm::vec3 bitangent = {b.x, b.y, b.z};
                        handedness = glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
                    }

                    vertex.tangent = {t.x, t.y, t.z, handedness};
                }

                if (has_uv0)
                {
                    const aiVector3D &uv = mesh->mTextureCoords[0][v];
                    vertex.uv0 = {uv.x, 1.0f - uv.y};
                }

                if (has_uv1)
                {
                    const aiVector3D &uv = mesh->mTextureCoords[1][v];
                    vertex.uv1 = {uv.x, 1.0f - uv.y};
                }

                if (has_color0)
                {
                    const aiColor4D &c = mesh->mColors[0][v];
                    vertex.color0 = {c.r, c.g, c.b, c.a};
                }
            }

            if (mesh->mNumVertices == 0)
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
            if (skipped_faces > 0)
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) +
                         " skipped non-triangle faces: " + std::to_string(skipped_faces));
            }
            if (mesh->mMaterialIndex < material_handles.size())
            {
                primitive.material = material_handles[mesh->mMaterialIndex];
            }
            if (!primitive.material.IsValid())
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) + " material handle invalid");
            }

            primitive.bounds.valid = mesh->mNumVertices > 0;
            if (primitive.bounds.valid)
            {
                const auto &first = primitive.vertices.front().position;
                primitive.bounds.min = first;
                primitive.bounds.max = first;
                for (const auto &vertex : primitive.vertices)
                {
                    primitive.bounds.min = glm::min(primitive.bounds.min, vertex.position);
                    primitive.bounds.max = glm::max(primitive.bounds.max, vertex.position);
                }
            }
            if (!primitive.bounds.valid)
            {
                LOG_WARN("[AssimpSceneLoader] Mesh " + std::to_string(i) + " has invalid bounds");
            }
            mesh_asset.primitives.push_back(std::move(primitive));
            mesh_asset.bounds = mesh_asset.primitives.front().bounds;

            const std::string asset_path = request.path + "#mesh/" + std::to_string(i);
            return m_assets->Add(asset_path, std::make_shared<hybrid::core::scene::MeshAsset>(std::move(mesh_asset)));
        };

        const unsigned int mesh_count = scene->mNumMeshes;
        const unsigned int hw_threads = std::min(4u, std::max(1u, std::thread::hardware_concurrency() - 1));
        if (const unsigned int worker_count = std::min(mesh_count, hw_threads); worker_count <= 1)
        {
            for (unsigned int i = 0; i < mesh_count; ++i)
            {
                mesh_handles[i] = load_mesh(i);
            }
        }
        else
        {
            std::vector<std::future<void>> workers;
            workers.reserve(worker_count);

            LOG_INFO("[AssimpSceneLoader] \t Processing meshes across " + std::to_string(worker_count) + " workers");

            for (unsigned int worker = 0; worker < worker_count; ++worker)
            {
                workers.emplace_back(std::async(std::launch::async, [&, worker]()
                                                {
                                                    for (unsigned int i = worker; i < mesh_count; i += worker_count)
                                                    {
                                                        mesh_handles[i] = load_mesh(i);
                                                    } }));
            }

            for (auto &worker : workers)
            {
                worker.get();
            }
        }

        const unsigned int missing_normals = meshes_missing_normals.load(std::memory_order_relaxed);
        const unsigned int missing_tangents = meshes_missing_tangents.load(std::memory_order_relaxed);
        if (missing_normals > 0)
        {
            LOG_CRITICAL("[AssimpSceneLoader] Meshes without normals: " +
                     std::to_string(missing_normals) + "/" + std::to_string(mesh_count) +
                     " . Enable aiProcess_GenNormals for this scene.");
        }
        if (missing_tangents > 0)
        {
            LOG_ERROR("[AssimpSceneLoader] Meshes without tangents: " +
                     std::to_string(missing_tangents) + "/" + std::to_string(mesh_count) +
                     " (enable aiProcess_CalcTangentSpace if normal mapping looks wrong)");
        }

        LOG_INFO("[AssimpSceneLoader] \t Building scene...");

        std::unordered_map<std::string, const aiCamera *> cameras_by_node_name;
        cameras_by_node_name.reserve(scene->mNumCameras);
        for (unsigned int i = 0; i < scene->mNumCameras; ++i)
        {
            const aiCamera *camera = scene->mCameras[i];
            if (camera == nullptr)
            {
                continue;
            }

            const std::string node_name = camera->mName.C_Str();
            if (node_name.empty())
            {
                continue;
            }

            if (!cameras_by_node_name.try_emplace(node_name, camera).second)
            {
                LOG_WARN("[AssimpSceneLoader] Duplicate camera node binding for '" + node_name + "', keeping first instance");
            }
        }

        if (scene->mRootNode)
        {
            std::unordered_map<std::string, entt::entity> entities_by_name;
            entities_by_name.reserve(static_cast<size_t>(scene->mNumMeshes + scene->mNumCameras + scene->mNumLights + 8));

            AppendNode(*scene->mRootNode, entt::null, mesh_handles, cameras_by_node_name, entities_by_name, *result);
            AppendLights(*scene, entities_by_name, m_assets, *result);
        }

        LOG_INFO("[AssimpSceneLoader] glTF scene loaded");
        return result;
    }

    void AssimpSceneLoader::LogSceneSummary(const aiScene *scene) const
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

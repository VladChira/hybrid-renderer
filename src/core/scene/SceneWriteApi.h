#pragma once

#include "core/scene/SceneReadApi.h"

namespace hybrid::core::scene
{

    class ISceneWriteApi
    {
    public:
        virtual ~ISceneWriteApi() = default;

        virtual SceneEntity CreateEntity(const std::string &name = {}) = 0;
        virtual bool DestroyEntity(SceneEntity entity) = 0;

        virtual bool SetName(SceneEntity entity, const std::string &name) = 0;

        virtual bool SetLocalTransform(SceneEntity entity, const Transform &local) = 0;
        virtual bool SetLocalTranslation(SceneEntity entity, const glm::vec3 &translation) = 0;
        virtual bool SetLocalRotation(SceneEntity entity, const glm::quat &rotation) = 0;
        virtual bool SetLocalScale(SceneEntity entity, const glm::vec3 &scale) = 0;
        virtual void MarkDirty(SceneEntity entity) = 0;

        virtual bool SetParent(SceneEntity child, SceneEntity parent) = 0;
        virtual bool ClearParent(SceneEntity child) = 0;

        virtual bool AddCamera(SceneEntity entity, const CameraComponent &camera = {}) = 0;
        virtual bool RemoveCamera(SceneEntity entity) = 0;
        virtual bool SetCameraLens(SceneEntity entity,
                                   float horizontal_fov_radians,
                                   float near_plane,
                                   float far_plane) = 0;
        virtual bool SetPrimaryCamera(SceneEntity entity, bool is_primary) = 0;
        virtual bool SetCameraTarget(SceneEntity entity, bool enabled, SceneEntity target) = 0;

        virtual bool AddMeshRenderer(SceneEntity entity, const MeshRendererComponent &mesh_renderer) = 0;
        virtual bool RemoveMeshRenderer(SceneEntity entity) = 0;
        virtual bool SetMeshRenderer(SceneEntity entity, const MeshRendererComponent &mesh_renderer) = 0;

        virtual bool AddDirectionalLight(SceneEntity entity,
                                         const LightCommonComponent &common,
                                         const DirectionalLightComponent &directional = {}) = 0;
        virtual bool AddPointLight(SceneEntity entity,
                                   const LightCommonComponent &common,
                                   const PointLightComponent &point = {}) = 0;
        virtual bool AddAreaLight(SceneEntity entity,
                                  const LightCommonComponent &common,
                                  const AreaLightComponent &area = {}) = 0;
        virtual bool AddHdriLight(SceneEntity entity,
                                  const LightCommonComponent &common,
                                  const HdriLightComponent &hdri = {}) = 0;
        virtual bool RemoveLight(SceneEntity entity) = 0;
        virtual bool SetLightCommon(SceneEntity entity, const LightCommonComponent &common) = 0;
        virtual bool SetPointLight(SceneEntity entity, const PointLightComponent &point) = 0;
        virtual bool SetAreaLight(SceneEntity entity, const AreaLightComponent &area) = 0;
        virtual bool SetHdriLight(SceneEntity entity, const HdriLightComponent &hdri) = 0;

        virtual void FlushPendingChanges() = 0;
    };

} // namespace hybrid::core::scene


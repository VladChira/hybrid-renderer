#pragma once

#include "core/scene/SceneTypes.h"

#include <entt/entt.hpp>

#include <cstdint>
#include <vector>

namespace hybrid::core::scene
{

    using SceneEntity = entt::entity;

    class ISceneReadApi
    {
    public:
        virtual ~ISceneReadApi() = default;

        virtual bool IsValid(SceneEntity entity) const = 0;

        virtual const NameComponent *TryGetName(SceneEntity entity) const = 0;
        virtual const TransformComponent *TryGetTransform(SceneEntity entity) const = 0;
        virtual const HierarchyComponent *TryGetHierarchy(SceneEntity entity) const = 0;
        virtual const MeshRendererComponent *TryGetMeshRenderer(SceneEntity entity) const = 0;
        virtual const CameraComponent *TryGetCamera(SceneEntity entity) const = 0;
        virtual const PrimaryCameraComponent *TryGetPrimaryCamera(SceneEntity entity) const = 0;
        virtual const CameraTargetComponent *TryGetCameraTarget(SceneEntity entity) const = 0;
        virtual const LightCommonComponent *TryGetLightCommon(SceneEntity entity) const = 0;
        virtual const DirectionalLightComponent *TryGetDirectionalLight(SceneEntity entity) const = 0;
        virtual const PointLightComponent *TryGetPointLight(SceneEntity entity) const = 0;
        virtual const AreaLightComponent *TryGetAreaLight(SceneEntity entity) const = 0;
        virtual const HdriLightComponent *TryGetHdriLight(SceneEntity entity) const = 0;

        virtual SceneEntity GetParent(SceneEntity child) const = 0;
        virtual const std::vector<SceneEntity> &GetChildren(SceneEntity parent) const = 0;

        virtual uint32_t GetEntityCount() const = 0;
        virtual void GetEntities(std::vector<SceneEntity> &out_entities) const = 0;
        virtual void GetEntitiesWithTransform(std::vector<SceneEntity> &out_entities) const = 0;
        virtual void GetEntitiesWithMeshRenderer(std::vector<SceneEntity> &out_entities) const = 0;
        virtual void GetEntitiesWithCamera(std::vector<SceneEntity> &out_entities) const = 0;
        virtual void GetEntitiesWithLight(std::vector<SceneEntity> &out_entities) const = 0;
    };

} // namespace hybrid::core::scene


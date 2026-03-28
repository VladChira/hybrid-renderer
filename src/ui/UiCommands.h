#pragma once

#include "core/scene/types/SceneMath.h"

#include <entt/entt.hpp>

#include <glm/glm.hpp>

#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace hybrid::ui
{

    struct QuitCommand
    {
    };

    struct EntityRenameCommand
    {
        entt::entity entity = entt::null;
        std::string name;
    };

    struct EntitySetLocalTransformCommand
    {
        entt::entity entity = entt::null;
        core::scene::Transform local{};
    };

    struct CameraSetLensCommand
    {
        entt::entity entity = entt::null;
        float horizontal_fov_radians = 1.0471976f;
        float near_plane = 0.1f;
        float far_plane = 1000.0f;
    };

    struct CameraSetTargetCommand
    {
        entt::entity entity = entt::null;
        bool enabled = false;
        entt::entity target = entt::null;
    };

    struct CameraSetPrimaryCommand
    {
        entt::entity entity = entt::null;
    };

    struct AddCameraCommand
    {
    };

    struct AddPointLightCommand
    {
    };

    struct AddAreaLightCommand
    {
    };

    struct AddDirectionalLightCommand
    {
    };

    struct EditLightCommonCommand
    {
        entt::entity entity = entt::null;
        glm::vec3 color{1.0f};
        float intensity = 1.0f;
        bool cast_shadows = true;
    };

    struct EditPointLightCommand
    {
        entt::entity entity = entt::null;
        float range = 0.0f;
        float attenuation_constant = 1.0f;
        float attenuation_linear = 0.0f;
        float attenuation_quadratic = 1.0f;
    };

    struct EditAreaLightCommand
    {
        entt::entity entity = entt::null;
        glm::vec2 size{1.0f, 1.0f};
        glm::vec3 direction{0.0f, -1.0f, 0.0f};
        bool two_sided = false;
    };

    struct EditHdriLightCommand
    {
        entt::entity entity = entt::null;
        float yaw_radians = 0.0f;
    };

    enum class MaterialScalarProperty
    {
        MetallicFactor,
        RoughnessFactor,
        AlphaCutoff,
        NormalScale,
        OcclusionStrength
    };

    enum class MaterialVec4Property
    {
        BaseColorFactor
    };

    enum class MaterialVec3Property
    {
        EmissiveFactor
    };

    struct MaterialSetScalarCommand
    {
        uint64_t material_asset_id = 0;
        MaterialScalarProperty property = MaterialScalarProperty::MetallicFactor;
        float value = 0.0f;
    };

    struct MaterialSetVec4Command
    {
        uint64_t material_asset_id = 0;
        MaterialVec4Property property = MaterialVec4Property::BaseColorFactor;
        glm::vec4 value{1.0f};
    };

    struct MaterialSetVec3Command
    {
        uint64_t material_asset_id = 0;
        MaterialVec3Property property = MaterialVec3Property::EmissiveFactor;
        glm::vec3 value{0.0f};
    };

    enum class EditorCameraNavigationMode
    {
        Orbit,
        Pan,
        Dolly
    };

    struct EditorCameraNavigateCommand
    {
        EditorCameraNavigationMode mode = EditorCameraNavigationMode::Orbit;
        glm::vec2 delta{0.0f};
        float amount = 0.0f;
    };

    using UiCommand = std::variant<QuitCommand,
                                   EntityRenameCommand,
                                   EntitySetLocalTransformCommand,
                                   CameraSetLensCommand,
                                   CameraSetTargetCommand,
                                   CameraSetPrimaryCommand,
                                   AddCameraCommand,
                                   AddPointLightCommand,
                                   AddAreaLightCommand,
                                   AddDirectionalLightCommand,
                                   EditLightCommonCommand,
                                   EditPointLightCommand,
                                   EditAreaLightCommand,
                                   EditHdriLightCommand,
                                   MaterialSetScalarCommand,
                                   MaterialSetVec4Command,
                                   MaterialSetVec3Command,
                                   EditorCameraNavigateCommand>;
    using CommandBuffer = std::vector<UiCommand>;

    template <typename T>
    void EnqueueCommand(std::vector<UiCommand> &buffer, T &&command)
    {
        buffer.emplace_back(std::forward<T>(command));
    }

    template <typename T>
    bool IsCommandType(const UiCommand &command)
    {
        return std::holds_alternative<T>(command);
    }

    template <typename T>
    const T *GetCommandIf(const UiCommand *command)
    {
        if (command == nullptr)
        {
            return nullptr;
        }
        return std::get_if<T>(command);
    }

} // namespace hybrid::ui

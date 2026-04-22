#include "ViewportPanel.h"

#include "core/Profiling.h"
#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneAssets.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"
#include "ui/UiState.h"

#include <ImGuizmo.h>
#include <glad.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        constexpr uint32_t kNoEntityId = std::numeric_limits<uint32_t>::max();
        constexpr float kOrbitSensitivity = 0.0045f;
        constexpr float kOrbitMinDistance = 0.05f;
        constexpr float kOrbitPitchDotLimit = 0.995f;
        constexpr float kPanPixelsToWorld = 0.002f;
        constexpr float kDollyBaseSpeed = 0.5f;
        constexpr float kDollyDistanceFactor = 0.15f;

        uint64_t ResolveViewportTexture(const UiState &state)
        {
            return state.viewport_color_texture;
        }

        bool ReadEntityIdPixel(uint64_t texture_id, int32_t pixel_x, int32_t pixel_y, uint32_t &out_entity_id)
        {
            HYBRID_PROFILE_ZONE_N("ViewportPanel::ReadEntityIdPixel");
            if (texture_id == 0)
            {
                return false;
            }

            static GLuint readback_framebuffer = 0;
            if (readback_framebuffer == 0)
            {
                glGenFramebuffers(1, &readback_framebuffer);
                if (readback_framebuffer == 0)
                {
                    return false;
                }
            }

            GLint previous_read_framebuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read_framebuffer);

            glBindFramebuffer(GL_READ_FRAMEBUFFER, readback_framebuffer);
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   static_cast<GLuint>(texture_id),
                                   0);
            if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_framebuffer));
                return false;
            }

            glReadBuffer(GL_COLOR_ATTACHMENT0);
            uint32_t entity_id = kNoEntityId;
            glReadPixels(pixel_x, pixel_y, 1, 1, GL_RED_INTEGER, GL_UNSIGNED_INT, &entity_id);
            out_entity_id = entity_id;

            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_framebuffer));
            return glGetError() == GL_NO_ERROR;
        }

        bool ComputeHoveredPixel(const ImVec2 &mouse,
                                 const ImVec2 &image_min,
                                 const ImVec2 &image_size,
                                 const renderer::RenderExtent &render_extent,
                                 int32_t &out_pixel_x,
                                 int32_t &out_pixel_y)
        {
            const float render_width = static_cast<float>(std::max(render_extent.width, 1u));
            const float render_height = static_cast<float>(std::max(render_extent.height, 1u));

            const float normalized_x = std::clamp((mouse.x - image_min.x) / std::max(image_size.x, 1.0f), 0.0f, 0.999999f);
            const float normalized_y = std::clamp((mouse.y - image_min.y) / std::max(image_size.y, 1.0f), 0.0f, 0.999999f);

            out_pixel_x = static_cast<int32_t>(normalized_x * render_width);
            const auto pixel_from_top = static_cast<int32_t>(normalized_y * render_height);
            out_pixel_y = static_cast<int32_t>(render_extent.height) - 1 - pixel_from_top;
            return true;
        }

        entt::entity ResolveHoveredEntity(const UiState &state,
                                          const ImVec2 &mouse,
                                          const ImVec2 &image_min,
                                          const ImVec2 &image_size)
        {
            HYBRID_PROFILE_ZONE_N("ViewportPanel::ResolveHoveredEntity");
            if (state.scene_world == nullptr)
            {
                return entt::null;
            }

            int32_t pixel_x = 0;
            int32_t pixel_y = 0;
            if (!ComputeHoveredPixel(mouse, image_min, image_size, state.viewport_render_extent, pixel_x, pixel_y))
            {
                return entt::null;
            }

            uint32_t entity_id = kNoEntityId;
            if (!ReadEntityIdPixel(state.viewport_entity_id_texture, pixel_x, pixel_y, entity_id) || entity_id == kNoEntityId)
            {
                return entt::null;
            }

            const entt::entity entity = static_cast<entt::entity>(entity_id);
            return state.scene_world->IsValid(entity) ? entity : entt::null;
        }

        entt::entity ResolvePrimaryCameraEntity(const core::scene::SceneWorld &scene_world)
        {
            const auto &registry = scene_world.Registry();

            auto primary_view =
                registry.view<core::scene::PrimaryCameraComponent, core::scene::CameraComponent, core::scene::TransformComponent>();
            if (primary_view.begin() != primary_view.end())
            {
                return *primary_view.begin();
            }

            auto any_camera_view = registry.view<core::scene::CameraComponent, core::scene::TransformComponent>();
            if (any_camera_view.begin() != any_camera_view.end())
            {
                return *any_camera_view.begin();
            }

            return entt::null;
        }

        glm::vec3 ResolveEntityOrbitPivot(const core::scene::SceneWorld &scene_world, entt::entity entity)
        {
            const auto &registry = scene_world.Registry();
            const auto *transform = registry.try_get<core::scene::TransformComponent>(entity);
            if (transform == nullptr)
            {
                return glm::vec3(0.0f);
            }

            glm::vec3 local_center{0.0f};
            if (const auto *mesh_renderer = registry.try_get<core::scene::MeshRendererComponent>(entity);
                mesh_renderer != nullptr)
            {
                if (const auto *mesh = mesh_renderer->mesh.Get(); mesh != nullptr && mesh->bounds.valid)
                {
                    local_center = 0.5f * (mesh->bounds.min + mesh->bounds.max);
                }
            }

            return glm::vec3(transform->world * glm::vec4(local_center, 1.0f));
        }

        core::scene::Transform ComputeCameraLocalFromWorld(const core::scene::SceneWorld &scene_world,
                                                           entt::entity camera_entity,
                                                           const glm::mat4 &camera_world)
        {
            const auto &registry = scene_world.Registry();
            glm::mat4 local_matrix = camera_world;
            if (const auto *hierarchy = registry.try_get<core::scene::HierarchyComponent>(camera_entity);
                hierarchy != nullptr && hierarchy->parent != entt::null && scene_world.IsValid(hierarchy->parent))
            {
                if (const auto *parent_transform = registry.try_get<core::scene::TransformComponent>(hierarchy->parent))
                {
                    local_matrix = glm::inverse(parent_transform->world) * camera_world;
                }
            }

            float translation[3]{0.0f, 0.0f, 0.0f};
            float rotation_degrees[3]{0.0f, 0.0f, 0.0f};
            float scale[3]{1.0f, 1.0f, 1.0f};
            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local_matrix), translation, rotation_degrees, scale);

            core::scene::Transform updated_local{};
            updated_local.translation = glm::vec3(translation[0], translation[1], translation[2]);
            updated_local.rotation = glm::quat(glm::radians(glm::vec3(rotation_degrees[0],
                                                                       rotation_degrees[1],
                                                                       rotation_degrees[2])));
            updated_local.scale = glm::vec3(scale[0], scale[1], scale[2]);
            return updated_local;
        }

        bool MoveCameraInWorld(const core::scene::SceneWorld &scene_world,
                               entt::entity camera_entity,
                               const glm::vec3 &translation_delta,
                               core::scene::Transform &out_local)
        {
            const auto &registry = scene_world.Registry();
            const auto *camera_transform = registry.try_get<core::scene::TransformComponent>(camera_entity);
            if (camera_transform == nullptr)
            {
                return false;
            }

            glm::mat4 camera_world = camera_transform->world;
            camera_world[3] = camera_world[3] + glm::vec4(translation_delta, 0.0f);
            out_local = ComputeCameraLocalFromWorld(scene_world, camera_entity, camera_world);
            return true;
        }
    } // namespace

    ViewportPanel::ViewportPanel()
        : Panel("Viewport")
    {
    }

    ImGuiWindowFlags ViewportPanel::WindowFlags() const
    {
        return ImGuiWindowFlags_NoBackground;
    }

    void ViewportPanel::DrawContents(PanelContext &context)
    {
        HYBRID_PROFILE_ZONE_N("ViewportPanel::DrawContents");
        const ImVec2 current_size = ImGui::GetContentRegionAvail();
        if (context.state != nullptr)
        {
            const uint64_t viewport_texture = ResolveViewportTexture(*context.state);
            if (viewport_texture == 0)
            {
                m_last_content_size = current_size;
                return;
            }

            const auto &render_extent = context.state->viewport_render_extent;
            const float render_width = static_cast<float>(std::max(render_extent.width, 1u));
            const float render_height = static_cast<float>(std::max(render_extent.height, 1u));
            const float render_aspect = render_width / render_height;
            const float viewport_width = std::max(current_size.x, 1.0f);
            const float viewport_height = std::max(current_size.y, 1.0f);
            const float viewport_aspect = viewport_width / viewport_height;

            ImVec2 image_size = current_size;
            if (viewport_aspect > render_aspect)
            {
                image_size.x = viewport_height * render_aspect;
                image_size.y = viewport_height;
            }
            else
            {
                image_size.x = viewport_width;
                image_size.y = viewport_width / render_aspect;
            }

            const float x_offset = (current_size.x - image_size.x) * 0.5f;
            const float y_offset = (current_size.y - image_size.y) * 0.5f;
            ImGui::SetCursorPos(ImVec2(ImGui::GetCursorPosX() + x_offset,
                                       ImGui::GetCursorPosY() + y_offset));

            const ImVec2 image_min = ImGui::GetCursorScreenPos();
            const ImVec2 image_max(image_min.x + image_size.x, image_min.y + image_size.y);

            ImGui::Image(
                static_cast<ImTextureID>(static_cast<intptr_t>(viewport_texture)),
                image_size,
                ImVec2(0.0f, 1.0f),
                ImVec2(1.0f, 0.0f));

            const bool image_hovered = ImGui::IsItemHovered();
            const ImVec2 mouse = ImGui::GetMousePos();
            ImGuiIO &io = ImGui::GetIO();

            if (context.state->scene_world != nullptr && context.commands != nullptr)
            {
                const bool space_down = ImGui::IsKeyDown(ImGuiKey_Space);

                if (!m_orbit.active && image_hovered && io.KeyAlt && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const entt::entity hovered_entity = ResolveHoveredEntity(*context.state, mouse, image_min, image_size);
                    const entt::entity camera_entity = ResolvePrimaryCameraEntity(*context.state->scene_world);
                    if (hovered_entity != entt::null && camera_entity != entt::null)
                    {
                        m_orbit.active = true;
                        m_orbit.camera_entity = camera_entity;
                        m_orbit.pivot = ResolveEntityOrbitPivot(*context.state->scene_world, hovered_entity);
                        m_orbit.last_mouse = mouse;
                    }
                }

                if (!m_orbit.active && !m_pan.active && image_hovered && space_down && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                {
                    const entt::entity camera_entity = ResolvePrimaryCameraEntity(*context.state->scene_world);
                    if (camera_entity != entt::null)
                    {
                        m_pan.active = true;
                        m_pan.camera_entity = camera_entity;
                        m_pan.last_mouse = mouse;
                    }
                }

                if (m_orbit.active)
                {
                    if (!io.KeyAlt || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || !context.state->scene_world->IsValid(m_orbit.camera_entity))
                    {
                        m_orbit.active = false;
                        m_orbit.camera_entity = entt::null;
                    }
                    else
                    {
                        const auto &registry = context.state->scene_world->Registry();
                        if (const auto *camera_transform =
                                registry.try_get<core::scene::TransformComponent>(m_orbit.camera_entity);
                            camera_transform != nullptr)
                        {
                            const ImVec2 current_mouse = ImGui::GetMousePos();
                            const ImVec2 mouse_delta(current_mouse.x - m_orbit.last_mouse.x,
                                                     current_mouse.y - m_orbit.last_mouse.y);
                            m_orbit.last_mouse = current_mouse;

                            if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)
                            {
                                const glm::vec3 camera_position = glm::vec3(camera_transform->world[3]);
                                glm::vec3 offset = camera_position - m_orbit.pivot;
                                const float distance = glm::length(offset);
                                if (distance > kOrbitMinDistance)
                                {
                                    const glm::vec3 world_up(0.0f, 1.0f, 0.0f);
                                    offset = glm::vec3(glm::rotate(glm::mat4(1.0f),
                                                                   -mouse_delta.x * kOrbitSensitivity,
                                                                   world_up) *
                                                       glm::vec4(offset, 0.0f));

                                    glm::vec3 forward = glm::normalize(-offset);
                                    glm::vec3 right = glm::cross(forward, world_up);
                                    if (glm::dot(right, right) > 1e-8f)
                                    {
                                        right = glm::normalize(right);
                                        glm::vec3 pitched = glm::vec3(glm::rotate(glm::mat4(1.0f),
                                                                                  -mouse_delta.y * kOrbitSensitivity,
                                                                                  right) *
                                                                      glm::vec4(offset, 0.0f));
                                        const glm::vec3 pitched_forward = glm::normalize(-pitched);
                                        if (std::abs(glm::dot(pitched_forward, world_up)) < kOrbitPitchDotLimit)
                                        {
                                            offset = pitched;
                                        }
                                    }

                                    const glm::vec3 new_position = m_orbit.pivot + offset;
                                    const glm::mat4 camera_world =
                                        glm::inverse(glm::lookAt(new_position, m_orbit.pivot, world_up));

                                    const core::scene::Transform updated_local = ComputeCameraLocalFromWorld(
                                        *context.state->scene_world, m_orbit.camera_entity, camera_world);
                                    EnqueueCommand(*context.commands,
                                                   EntitySetLocalTransformCommand{m_orbit.camera_entity, updated_local});
                                }
                            }
                        }
                    }
                }

                if (m_pan.active)
                {
                    if (!space_down || !ImGui::IsMouseDown(ImGuiMouseButton_Left) || !context.state->scene_world->IsValid(m_pan.camera_entity))
                    {
                        m_pan.active = false;
                        m_pan.camera_entity = entt::null;
                    }
                    else
                    {
                        const auto &registry = context.state->scene_world->Registry();
                        if (const auto *camera_transform = registry.try_get<core::scene::TransformComponent>(m_pan.camera_entity);
                            camera_transform != nullptr)
                        {
                            const ImVec2 current_mouse = ImGui::GetMousePos();
                            const ImVec2 mouse_delta(current_mouse.x - m_pan.last_mouse.x,
                                                     current_mouse.y - m_pan.last_mouse.y);
                            m_pan.last_mouse = current_mouse;

                            if (mouse_delta.x != 0.0f || mouse_delta.y != 0.0f)
                            {
                                const glm::vec3 camera_position = glm::vec3(camera_transform->world[3]);
                                const glm::vec3 world_up = glm::normalize(
                                    glm::vec3(camera_transform->world * glm::vec4(0.0f, 1.0f, 0.0f, 0.0f)));
                                const glm::vec3 forward = glm::normalize(
                                    glm::vec3(camera_transform->world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                                glm::vec3 right = glm::cross(forward, world_up);
                                if (glm::dot(right, right) > 1e-8f)
                                {
                                    right = glm::normalize(right);

                                    const float reference_distance =
                                        std::max(glm::length(camera_position - m_orbit.pivot), 1.0f);
                                    const float world_per_pixel = reference_distance * kPanPixelsToWorld;

                                    const glm::vec3 delta_world =
                                        (-mouse_delta.x * world_per_pixel) * right +
                                        (mouse_delta.y * world_per_pixel) * world_up;

                                    core::scene::Transform updated_local{};
                                    if (MoveCameraInWorld(*context.state->scene_world,
                                                          m_pan.camera_entity,
                                                          delta_world,
                                                          updated_local))
                                    {
                                        m_orbit.pivot += delta_world;
                                        EnqueueCommand(*context.commands,
                                                       EntitySetLocalTransformCommand{m_pan.camera_entity, updated_local});
                                    }
                                }
                            }
                        }
                    }
                }

                if (image_hovered && io.MouseWheel != 0.0f)
                {
                    const entt::entity camera_entity = ResolvePrimaryCameraEntity(*context.state->scene_world);
                    if (camera_entity != entt::null)
                    {
                        const auto &registry = context.state->scene_world->Registry();
                        if (const auto *camera_transform = registry.try_get<core::scene::TransformComponent>(camera_entity);
                            camera_transform != nullptr)
                        {
                            const glm::vec3 camera_position = glm::vec3(camera_transform->world[3]);
                            const glm::vec3 forward = glm::normalize(
                                glm::vec3(camera_transform->world * glm::vec4(0.0f, 0.0f, -1.0f, 0.0f)));
                            const float distance_to_pivot = glm::length(camera_position - m_orbit.pivot);
                            const float dolly_speed =
                                std::max(kDollyBaseSpeed, distance_to_pivot * kDollyDistanceFactor);
                            const glm::vec3 dolly_delta = io.MouseWheel * dolly_speed * forward;

                            core::scene::Transform updated_local{};
                            if (MoveCameraInWorld(*context.state->scene_world, camera_entity, dolly_delta, updated_local))
                            {
                                EnqueueCommand(*context.commands,
                                               EntitySetLocalTransformCommand{camera_entity, updated_local});
                            }
                        }
                    }
                }
            }

            if (context.selection != nullptr &&
                context.state->scene_world != nullptr &&
                image_hovered &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !io.KeyAlt &&
                !ImGui::IsKeyDown(ImGuiKey_Space) &&
                !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing())
            {
                const entt::entity entity = ResolveHoveredEntity(*context.state, mouse, image_min, image_size);
                if (entity != entt::null)
                {
                    context.selection->type = UiSelection::Type::Entity;
                    context.selection->entity = entity;
                    context.selection->material_asset_id = 0;
                }
                else
                {
                    context.selection->Clear();
                }
            }

            if (context.state->viewport_render_view_valid &&
                context.selection != nullptr &&
                context.selection->type == UiSelection::Type::Entity &&
                context.state->scene_world != nullptr &&
                context.commands != nullptr)
            {
                const core::scene::SceneWorld &scene_world = *context.state->scene_world;
                const entt::entity entity = context.selection->entity;
                if (scene_world.IsValid(entity))
                {
                    const auto &registry = scene_world.Registry();
                    const auto *transform_component = registry.try_get<core::scene::TransformComponent>(entity);
                    if (transform_component != nullptr)
                    {
                        glm::mat4 entity_world = transform_component->world;

                        ImGuizmo::SetOrthographic(false);
                        ImGuizmo::SetDrawlist();
                        ImGuizmo::SetRect(image_min.x, image_min.y, image_max.x - image_min.x, image_max.y - image_min.y);
                        ImGuizmo::Enable(true);

                        ImGuizmo::OPERATION operation = ImGuizmo::TRANSLATE;
                        if (context.transform_tool != nullptr)
                        {
                            switch (*context.transform_tool)
                            {
                            case TransformTool::Translate:
                                operation = ImGuizmo::TRANSLATE;
                                break;
                            case TransformTool::Rotate:
                                operation = ImGuizmo::ROTATE;
                                break;
                            case TransformTool::Scale:
                                operation = ImGuizmo::SCALE;
                                break;
                            }
                        }

                        const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(context.state->viewport_render_view.view),
                                                                      glm::value_ptr(context.state->viewport_render_view.projection),
                                                                      operation,
                                                                      ImGuizmo::LOCAL,
                                                                      glm::value_ptr(entity_world),
                                                                      nullptr,
                                                                      nullptr);

                        if (manipulated)
                        {
                            glm::mat4 local_matrix = entity_world;
                            if (const auto *hierarchy = registry.try_get<core::scene::HierarchyComponent>(entity);
                                hierarchy != nullptr && hierarchy->parent != entt::null && scene_world.IsValid(hierarchy->parent))
                            {
                                if (const auto *parent_transform = registry.try_get<core::scene::TransformComponent>(hierarchy->parent))
                                {
                                    local_matrix = glm::inverse(parent_transform->world) * entity_world;
                                }
                            }

                            float translation[3]{0.0f, 0.0f, 0.0f};
                            float rotation_degrees[3]{0.0f, 0.0f, 0.0f};
                            float scale[3]{1.0f, 1.0f, 1.0f};
                            ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(local_matrix),
                                                                  translation,
                                                                  rotation_degrees,
                                                                  scale);

                            core::scene::Transform updated_local{};
                            updated_local.translation = glm::vec3(translation[0], translation[1], translation[2]);
                            updated_local.rotation = glm::quat(glm::radians(glm::vec3(rotation_degrees[0],
                                                                                      rotation_degrees[1],
                                                                                      rotation_degrees[2])));
                            updated_local.scale = glm::vec3(scale[0], scale[1], scale[2]);

                            EnqueueCommand(*context.commands, EntitySetLocalTransformCommand{entity, updated_local});
                        }
                    }
                }
            }
        }

        m_last_content_size = current_size;
    }

} // namespace hybrid::ui

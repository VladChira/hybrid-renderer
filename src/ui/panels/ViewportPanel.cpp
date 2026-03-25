#include "ViewportPanel.h"

#include "core/Log.h"
#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"
#include "ui/UiCommands.h"
#include "ui/UiState.h"

#include <ImGuizmo.h>
#include <glad.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>

#include <glm/gtc/type_ptr.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/quaternion.hpp>

namespace hybrid::ui
{
    namespace
    {
        constexpr uint32_t kNoEntityId = std::numeric_limits<uint32_t>::max();

        struct ChannelPreviewResources
        {
            GLuint framebuffer = 0;
            GLuint texture = 0;
            GLuint program = 0;
            GLuint vao = 0;
            uint32_t width = 0;
            uint32_t height = 0;
            bool shader_failed = false;
        };

        ChannelPreviewResources &GetChannelPreviewResources()
        {
            static ChannelPreviewResources resources{};
            return resources;
        }

        GLuint CompileShader(GLenum stage, const char *source)
        {
            const GLuint shader = glCreateShader(stage);
            if (shader == 0)
            {
                return 0;
            }

            glShaderSource(shader, 1, &source, nullptr);
            glCompileShader(shader);

            GLint status = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
            if (status == GL_TRUE)
            {
                return shader;
            }

            GLint log_length = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &log_length);
            std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
            GLsizei written = 0;
            glGetShaderInfoLog(shader, static_cast<GLsizei>(log.size()), &written, log.data());
            log.resize(static_cast<size_t>(std::max(written, 0)));
            LOG_ERROR("Viewport channel shader compile failed: " + log);
            glDeleteShader(shader);
            return 0;
        }

        bool EnsureChannelPreviewProgram(ChannelPreviewResources &resources)
        {
            if (resources.program != 0)
            {
                return true;
            }

            if (resources.shader_failed)
            {
                return false;
            }

            static constexpr const char *kVertexShader = R"(
                #version 330 core

                out vec2 v_uv;

                void main()
                {
                    vec2 positions[3] = vec2[](
                        vec2(-1.0, -1.0),
                        vec2(3.0, -1.0),
                        vec2(-1.0, 3.0));
                    vec2 pos = positions[gl_VertexID];
                    v_uv = pos * 0.5 + 0.5;
                    gl_Position = vec4(pos, 0.0, 1.0);
                }
            )";

            static constexpr const char *kFragmentShader = R"(
                #version 330 core

                in vec2 v_uv;
                out vec4 out_color;

                uniform sampler2D u_source_texture;
                uniform vec4 u_channel_mask;

                void main()
                {
                    vec4 source = texture(u_source_texture, v_uv);
                    vec3 color = source.rgb * u_channel_mask.rgb;
                    color += vec3(source.a * u_channel_mask.a);
                    out_color = vec4(color, 1.0);
                }
            )";

            const GLuint vertex_shader = CompileShader(GL_VERTEX_SHADER, kVertexShader);
            const GLuint fragment_shader = CompileShader(GL_FRAGMENT_SHADER, kFragmentShader);
            if (vertex_shader == 0 || fragment_shader == 0)
            {
                if (vertex_shader != 0)
                {
                    glDeleteShader(vertex_shader);
                }
                if (fragment_shader != 0)
                {
                    glDeleteShader(fragment_shader);
                }
                resources.shader_failed = true;
                return false;
            }

            const GLuint program = glCreateProgram();
            glAttachShader(program, vertex_shader);
            glAttachShader(program, fragment_shader);
            glLinkProgram(program);

            glDeleteShader(vertex_shader);
            glDeleteShader(fragment_shader);

            GLint status = GL_FALSE;
            glGetProgramiv(program, GL_LINK_STATUS, &status);
            if (status != GL_TRUE)
            {
                GLint log_length = 0;
                glGetProgramiv(program, GL_INFO_LOG_LENGTH, &log_length);
                std::string log(static_cast<size_t>(std::max(log_length, 1)), '\0');
                GLsizei written = 0;
                glGetProgramInfoLog(program, static_cast<GLsizei>(log.size()), &written, log.data());
                log.resize(static_cast<size_t>(std::max(written, 0)));
                LOG_ERROR("Viewport channel shader link failed: " + log);
                glDeleteProgram(program);
                resources.shader_failed = true;
                return false;
            }

            resources.program = program;
            glGenVertexArrays(1, &resources.vao);
            return resources.program != 0 && resources.vao != 0;
        }

        bool EnsureChannelPreviewTarget(ChannelPreviewResources &resources, uint32_t width, uint32_t height)
        {
            if (width == 0 || height == 0)
            {
                return false;
            }

            if (resources.framebuffer == 0)
            {
                glGenFramebuffers(1, &resources.framebuffer);
            }
            if (resources.texture == 0)
            {
                glGenTextures(1, &resources.texture);
            }
            if (resources.framebuffer == 0 || resources.texture == 0)
            {
                return false;
            }

            if (resources.width != width || resources.height != height)
            {
                glBindTexture(GL_TEXTURE_2D, resources.texture);
                glTexImage2D(GL_TEXTURE_2D,
                             0,
                             GL_RGBA8,
                             static_cast<GLsizei>(width),
                             static_cast<GLsizei>(height),
                             0,
                             GL_RGBA,
                             GL_UNSIGNED_BYTE,
                             nullptr);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
                resources.width = width;
                resources.height = height;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER,
                                   GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D,
                                   resources.texture,
                                   0);

            const bool complete = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
            return complete;
        }

        bool IsIdentityChannelMask(const UiViewportChannelMask &mask)
        {
            return mask.show_r && mask.show_g && mask.show_b && !mask.show_a;
        }

        uint64_t BuildChannelPreviewTexture(uint64_t source_texture,
                                            const renderer::RenderExtent &extent,
                                            const UiViewportChannelMask &mask)
        {
            if (source_texture == 0 || IsIdentityChannelMask(mask))
            {
                return source_texture;
            }

            GLint previous_framebuffer = 0;
            GLint previous_viewport[4]{0, 0, 0, 0};
            GLint previous_program = 0;
            GLint previous_vertex_array = 0;
            GLint previous_active_texture = 0;
            const GLboolean blend_enabled = glIsEnabled(GL_BLEND);
            const GLboolean depth_enabled = glIsEnabled(GL_DEPTH_TEST);
            const GLboolean cull_enabled = glIsEnabled(GL_CULL_FACE);

            glGetIntegerv(GL_FRAMEBUFFER_BINDING, &previous_framebuffer);
            glGetIntegerv(GL_VIEWPORT, previous_viewport);
            glGetIntegerv(GL_CURRENT_PROGRAM, &previous_program);
            glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &previous_vertex_array);
            glGetIntegerv(GL_ACTIVE_TEXTURE, &previous_active_texture);

            ChannelPreviewResources &resources = GetChannelPreviewResources();
            if (!EnsureChannelPreviewProgram(resources) ||
                !EnsureChannelPreviewTarget(resources, extent.width, extent.height))
            {
                glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
                return source_texture;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, resources.framebuffer);
            glViewport(0, 0, static_cast<GLsizei>(extent.width), static_cast<GLsizei>(extent.height));
            glDisable(GL_BLEND);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_CULL_FACE);

            glUseProgram(resources.program);
            glBindVertexArray(resources.vao);

            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(source_texture));
            glUniform1i(glGetUniformLocation(resources.program, "u_source_texture"), 0);
            glUniform4f(glGetUniformLocation(resources.program, "u_channel_mask"),
                        mask.show_r ? 1.0f : 0.0f,
                        mask.show_g ? 1.0f : 0.0f,
                        mask.show_b ? 1.0f : 0.0f,
                        mask.show_a ? 1.0f : 0.0f);
            glDrawArrays(GL_TRIANGLES, 0, 3);

            glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(previous_framebuffer));
            glViewport(previous_viewport[0], previous_viewport[1], previous_viewport[2], previous_viewport[3]);
            glUseProgram(static_cast<GLuint>(previous_program));
            glBindVertexArray(static_cast<GLuint>(previous_vertex_array));
            glActiveTexture(static_cast<GLenum>(previous_active_texture));
            if (blend_enabled == GL_TRUE)
            {
                glEnable(GL_BLEND);
            }
            else
            {
                glDisable(GL_BLEND);
            }
            if (depth_enabled == GL_TRUE)
            {
                glEnable(GL_DEPTH_TEST);
            }
            else
            {
                glDisable(GL_DEPTH_TEST);
            }
            if (cull_enabled == GL_TRUE)
            {
                glEnable(GL_CULL_FACE);
            }
            else
            {
                glDisable(GL_CULL_FACE);
            }

            return static_cast<uint64_t>(resources.texture);
        }

        uint64_t ResolveViewportTexture(const UiState &state)
        {
            switch (state.viewport_visualization)
            {
            case UiViewportVisualization::FinalColor:
                return state.viewport_color_texture;
            case UiViewportVisualization::GBufferRt0:
                return state.viewport_gbuffer_rt0_texture != 0
                           ? state.viewport_gbuffer_rt0_texture
                           : state.viewport_color_texture;
            case UiViewportVisualization::GBufferRt1:
                return state.viewport_gbuffer_rt1_texture != 0
                           ? state.viewport_gbuffer_rt1_texture
                           : state.viewport_color_texture;
            }

            return state.viewport_color_texture;
        }

        bool ReadEntityIdPixel(uint64_t texture_id, int32_t pixel_x, int32_t pixel_y, uint32_t &out_entity_id)
        {
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
        const ImVec2 current_size = ImGui::GetContentRegionAvail();
        if (context.state != nullptr)
        {
            uint64_t viewport_texture = ResolveViewportTexture(*context.state);
            if (context.viewport_channel_mask != nullptr)
            {
                viewport_texture = BuildChannelPreviewTexture(viewport_texture,
                                                              context.state->viewport_render_extent,
                                                              *context.viewport_channel_mask);
            }
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

            if (context.selection != nullptr &&
                context.state->scene_world != nullptr &&
                ImGui::IsItemHovered() &&
                ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
                !ImGuizmo::IsOver() &&
                !ImGuizmo::IsUsing())
            {
                const ImVec2 mouse = ImGui::GetMousePos();
                const float normalized_x = std::clamp((mouse.x - image_min.x) / std::max(image_size.x, 1.0f), 0.0f, 0.999999f);
                const float normalized_y = std::clamp((mouse.y - image_min.y) / std::max(image_size.y, 1.0f), 0.0f, 0.999999f);

                const auto pixel_x = static_cast<int32_t>(normalized_x * render_width);
                const auto pixel_from_top = static_cast<int32_t>(normalized_y * render_height);
                const int32_t pixel_y = static_cast<int32_t>(render_extent.height) - 1 - pixel_from_top;

                uint32_t entity_id = kNoEntityId;
                if (ReadEntityIdPixel(context.state->viewport_entity_id_texture, pixel_x, pixel_y, entity_id) &&
                    entity_id != kNoEntityId)
                {
                    const auto entity = static_cast<entt::entity>(entity_id);
                    if (context.state->scene_world->IsValid(entity))
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

                        const bool manipulated = ImGuizmo::Manipulate(glm::value_ptr(context.state->viewport_render_view.view),
                                                                      glm::value_ptr(context.state->viewport_render_view.projection),
                                                                      ImGuizmo::TRANSLATE,
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

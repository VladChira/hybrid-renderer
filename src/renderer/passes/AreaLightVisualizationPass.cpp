#include "renderer/passes/AreaLightVisualizationPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLBuffer.h"
#include "renderer/opengl/GLFramebuffer.h"
#include "renderer/opengl/GLShaderProgram.h"
#include "renderer/opengl/GLVertexArray.h"

#include <array>
#include <cmath>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr float kAxisEpsilon = 1e-5f;

        glm::vec3 NormalizeOrFallback(const glm::vec3 &vector, const glm::vec3 &fallback)
        {
            const float magnitude_squared = glm::dot(vector, vector);
            if (magnitude_squared <= kAxisEpsilon)
            {
                return fallback;
            }

            return glm::normalize(vector);
        }

        glm::vec3 BuildFallbackRight(const glm::vec3 &normal)
        {
            const glm::vec3 helper_axis = std::abs(normal.y) < 0.999f
                                              ? glm::vec3(0.0f, 1.0f, 0.0f)
                                              : glm::vec3(1.0f, 0.0f, 0.0f);
            return NormalizeOrFallback(glm::cross(helper_axis, normal), glm::vec3(1.0f, 0.0f, 0.0f));
        }

        glm::mat4 BuildAreaLightModelMatrix(const RenderAreaLight &light)
        {
            const glm::vec3 normal = NormalizeOrFallback(light.direction, glm::vec3(0.0f, -1.0f, 0.0f));
            glm::vec3 right = NormalizeOrFallback(light.right, BuildFallbackRight(normal));
            right = right - normal * glm::dot(right, normal);
            right = NormalizeOrFallback(right, BuildFallbackRight(normal));
            const glm::vec3 up = NormalizeOrFallback(glm::cross(normal, right), glm::vec3(0.0f, 0.0f, 1.0f));

            glm::mat4 model(1.0f);
            model[0] = glm::vec4(right * light.size.x, 0.0f);
            model[1] = glm::vec4(up * light.size.y, 0.0f);
            model[2] = glm::vec4(normal, 0.0f);
            model[3] = glm::vec4(light.position, 1.0f);
            return model;
        }
    } // namespace

    struct AreaLightVisualizationPass::Impl
    {
        GLVertexArray quad_vao{};
        GLBuffer quad_vbo{};
        bool geometry_ready = false;
    };

    AreaLightVisualizationPass::AreaLightVisualizationPass(GLShaderProgram *area_light_visualization_shader)
        : m_area_light_visualization_shader(area_light_visualization_shader),
          m_impl(std::make_unique<Impl>())
    {
    }

    AreaLightVisualizationPass::~AreaLightVisualizationPass() = default;

    const char *AreaLightVisualizationPass::Name() const
    {
        return "AreaLightVisualization";
    }

    bool AreaLightVisualizationPass::Execute(const AreaLightVisualizationPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("AreaLightVisualizationPass::Execute");
        HYBRID_PROFILE_GL_ZONE("AreaLightVisualizationPass");

        if (m_area_light_visualization_shader == nullptr ||
            m_impl == nullptr ||
            input.settings == nullptr ||
            input.scene_data == nullptr ||
            input.effective_view == nullptr ||
            input.scene_framebuffer_id == 0 ||
            input.gbuffer_framebuffer_id == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const FrameSceneData &scene_data = *input.scene_data;
        if (!settings.render_extent.IsValid())
        {
            return false;
        }

        bool has_visible_lights = false;
        for (const RenderAreaLight &light : scene_data.area_lights)
        {
            if (light.visible)
            {
                has_visible_lights = true;
                break;
            }
        }

        if (!has_visible_lights)
        {
            return true;
        }

        if (!m_impl->geometry_ready)
        {
            if (!m_impl->quad_vao.Create())
            {
                return false;
            }
            if (!m_impl->quad_vbo.Create(GL_ARRAY_BUFFER))
            {
                return false;
            }

            constexpr std::array<float, 12> kQuadVertices = {
                -0.5f, -0.5f,
                0.5f, -0.5f,
                0.5f, 0.5f,
                -0.5f, -0.5f,
                0.5f, 0.5f,
                -0.5f, 0.5f};

            m_impl->quad_vao.Bind();
            m_impl->quad_vbo.Bind();
            m_impl->quad_vbo.SetData(static_cast<GLsizeiptr>(sizeof(kQuadVertices)),
                                     kQuadVertices.data(),
                                     GL_STATIC_DRAW);

            m_impl->quad_vao.EnableAttrib(0);
            m_impl->quad_vao.SetAttribPointer(0,
                                              2,
                                              GL_FLOAT,
                                              false,
                                              static_cast<GLsizei>(2 * sizeof(float)),
                                              0);

            GLBuffer::Unbind(GL_ARRAY_BUFFER);
            GLVertexArray::Unbind();
            m_impl->geometry_ready = true;
        }

        const GLsizei viewport_width = static_cast<GLsizei>(settings.render_extent.width);
        const GLsizei viewport_height = static_cast<GLsizei>(settings.render_extent.height);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, input.gbuffer_framebuffer_id);
        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, input.scene_framebuffer_id);
        glBlitFramebuffer(0, 0, viewport_width, viewport_height,
                          0, 0, viewport_width, viewport_height,
                          GL_DEPTH_BUFFER_BIT,
                          GL_NEAREST);

        glBindFramebuffer(GL_FRAMEBUFFER, input.scene_framebuffer_id);
        glViewport(0, 0, viewport_width, viewport_height);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LEQUAL);
        glDepthMask(GL_FALSE);
        glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

        m_area_light_visualization_shader->Use();
        m_area_light_visualization_shader->SetUniformMat4("u_view", input.effective_view->view);
        m_area_light_visualization_shader->SetUniformMat4("u_projection", input.effective_view->projection);

        m_impl->quad_vao.Bind();
        for (const RenderAreaLight &light : scene_data.area_lights)
        {
            if (!light.visible)
            {
                continue;
            }
            if (light.size.x <= 0.0f || light.size.y <= 0.0f)
            {
                continue;
            }

            m_area_light_visualization_shader->SetUniformMat4("u_model", BuildAreaLightModelMatrix(light));
            glDrawArrays(GL_TRIANGLES, 0, 6);
        }

        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();
        glDepthMask(GL_TRUE);
        glDepthFunc(GL_LESS);
        GLFramebuffer::BindDefault(GL_READ_FRAMEBUFFER);
        GLFramebuffer::BindDefault(GL_DRAW_FRAMEBUFFER);
        return true;
    }

} // namespace hybrid::renderer

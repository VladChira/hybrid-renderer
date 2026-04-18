#include "renderer/passes/AreaLightDebugPass.h"

#include "core/Profiling.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hybrid::renderer
{

    namespace
    {
        struct DebugVertex
        {
            glm::vec3 position;
            glm::vec3 color;
        };

        constexpr size_t kMinCapacityBytes = 4 * 1024;

        // Same helper used elsewhere in the renderer — derives any two
        // in-plane axes from a surface normal.
        void BuildOrthonormalBasis(const glm::vec3 &n, glm::vec3 &tangent, glm::vec3 &bitangent)
        {
            const glm::vec3 helper = std::abs(n.y) < 0.999f ? glm::vec3(0.0f, 1.0f, 0.0f)
                                                            : glm::vec3(1.0f, 0.0f, 0.0f);
            tangent = glm::normalize(glm::cross(helper, n));
            bitangent = glm::normalize(glm::cross(n, tangent));
        }

        void EmitRectangle(std::vector<DebugVertex> &out,
                           const glm::vec3 &center,
                           const glm::vec3 &tangent,
                           const glm::vec3 &bitangent,
                           const glm::vec2 &size,
                           const glm::vec3 &color)
        {
            const glm::vec3 half_t = tangent   * (size.x * 0.5f);
            const glm::vec3 half_b = bitangent * (size.y * 0.5f);
            const glm::vec3 c0 = center - half_t - half_b;
            const glm::vec3 c1 = center + half_t - half_b;
            const glm::vec3 c2 = center + half_t + half_b;
            const glm::vec3 c3 = center - half_t + half_b;

            out.push_back({c0, color});
            out.push_back({c1, color});
            out.push_back({c2, color});
            out.push_back({c0, color});
            out.push_back({c2, color});
            out.push_back({c3, color});
        }
    } // namespace

    AreaLightDebugPass::AreaLightDebugPass(GLShaderProgram *program)
        : m_program(program)
    {
    }

    AreaLightDebugPass::~AreaLightDebugPass() = default;

    bool AreaLightDebugPass::EnsureGpuResources()
    {
        if (m_initialized)
        {
            return true;
        }
        if (!m_vertex_buffer.Create(GL_ARRAY_BUFFER))
        {
            return false;
        }
        if (!m_vao.Create())
        {
            return false;
        }

        m_vao.Bind();
        m_vertex_buffer.Bind();
        m_vao.EnableAttrib(0);
        m_vao.SetAttribPointer(0, 3, GL_FLOAT, false, sizeof(DebugVertex), offsetof(DebugVertex, position));
        m_vao.EnableAttrib(1);
        m_vao.SetAttribPointer(1, 3, GL_FLOAT, false, sizeof(DebugVertex), offsetof(DebugVertex, color));
        GLVertexArray::Unbind();
        GLBuffer::Unbind(GL_ARRAY_BUFFER);

        m_initialized = true;
        return true;
    }

    bool AreaLightDebugPass::Execute(const AreaLightDebugPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("AreaLightDebugPass::Execute");
        HYBRID_PROFILE_GL_ZONE("AreaLightDebugPass");

        if (m_program == nullptr ||
            input.settings == nullptr ||
            input.scene_data == nullptr ||
            input.effective_view == nullptr ||
            input.scene_framebuffer_id == 0 ||
            input.gbuffer_depth == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const FrameSceneData &scene = *input.scene_data;
        const auto &extent = settings.render_extent;
        if (!extent.IsValid())
        {
            return false;
        }

        std::vector<DebugVertex> vertices;
        vertices.reserve(scene.area_lights.size() * 6);
        for (const RenderAreaLight &light : scene.area_lights)
        {
            if (!light.visible)
            {
                continue;
            }
            glm::vec3 n = glm::normalize(light.direction);
            if (glm::dot(n, n) < 0.5f)
            {
                continue;
            }
            glm::vec3 tangent{0.0f}, bitangent{0.0f};
            BuildOrthonormalBasis(n, tangent, bitangent);
            EmitRectangle(vertices, light.position, tangent, bitangent, light.size, light.color);
        }

        if (vertices.empty())
        {
            return true;
        }

        if (!EnsureGpuResources())
        {
            return false;
        }

        const size_t needed_bytes = vertices.size() * sizeof(DebugVertex);
        m_vertex_buffer.Bind();
        if (needed_bytes > m_capacity_bytes)
        {
            m_capacity_bytes = std::max(needed_bytes * 2, kMinCapacityBytes);
            glBufferData(GL_ARRAY_BUFFER,
                         static_cast<GLsizeiptr>(m_capacity_bytes),
                         nullptr,
                         GL_DYNAMIC_DRAW);
        }
        glBufferSubData(GL_ARRAY_BUFFER, 0,
                        static_cast<GLsizeiptr>(needed_bytes),
                        vertices.data());
        GLBuffer::Unbind(GL_ARRAY_BUFFER);

        glBindFramebuffer(GL_FRAMEBUFFER, input.scene_framebuffer_id);
        glViewport(0, 0,
                   static_cast<GLsizei>(extent.width),
                   static_cast<GLsizei>(extent.height));

        // Manual depth test in the fragment shader — the scene framebuffer's
        // depth attachment is not populated, so disable hardware depth test.
        glDisable(GL_DEPTH_TEST);
        glDepthMask(GL_FALSE);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);

        m_program->Use();
        m_program->SetUniformMat4("u_view",       view.view);
        m_program->SetUniformMat4("u_projection", view.projection);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
        m_program->SetUniform1i("u_gbuffer_depth", 0);

        const GLint inv_loc = m_program->GetUniformLocation("u_inv_render_extent");
        if (inv_loc >= 0)
        {
            glUniform2f(inv_loc, 1.0f / static_cast<float>(extent.width),
                                 1.0f / static_cast<float>(extent.height));
        }

        m_vao.Bind();
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(vertices.size()));
        GLVertexArray::Unbind();
        GLShaderProgram::Unuse();

        glBindTexture(GL_TEXTURE_2D, 0);
        glDepthMask(GL_TRUE);
        glEnable(GL_DEPTH_TEST);
        return true;
    }

} // namespace hybrid::renderer

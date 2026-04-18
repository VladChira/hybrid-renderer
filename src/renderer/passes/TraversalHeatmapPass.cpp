#include "renderer/passes/TraversalHeatmapPass.h"

#include "core/Profiling.h"
#include "renderer/GeometryStore.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize = 8;
        constexpr GLuint kHeatmapImageBinding = 0;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    TraversalHeatmapPass::TraversalHeatmapPass(GLShaderProgram *program,
                                               GeometryStore *geometry_store,
                                               raytracing::AccelerationStructureCache *as_cache)
        : m_program(program),
          m_geometry_store(geometry_store),
          m_as_cache(as_cache)
    {
    }

    bool TraversalHeatmapPass::Execute(const TraversalHeatmapPassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("TraversalHeatmapPass::Execute");
        HYBRID_PROFILE_GL_ZONE("TraversalHeatmapPass");

        if (m_program == nullptr ||
            m_geometry_store == nullptr ||
            m_as_cache == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.heatmap_texture == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const auto &extent = settings.render_extent;
        if (!extent.IsValid())
        {
            return false;
        }

        const auto &stats = m_as_cache->Stats();
        // Always dispatch — when the TLAS is empty the shader fills the image
        // with the zero-visit colour, which keeps the UI from sampling
        // uninitialised texture contents.
        m_program->Use();

        // Output image binding.
        glBindImageTexture(kHeatmapImageBinding,
                           input.heatmap_texture,
                           0,
                           GL_FALSE,
                           0,
                           GL_WRITE_ONLY,
                           GL_RGBA8);

        // SSBOs.
        m_geometry_store->BindSsbos();
        m_as_cache->BindSsbos();

        m_program->SetUniformMat4("u_inv_view", glm::affineInverse(view.view));
        m_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
        m_program->SetUniformVec3("u_camera_position", view.position);
        m_program->SetUniform1ui("u_tlas_node_count", stats.tlas_nodes);

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(extent.width),
                         static_cast<GLuint>(extent.height));
        }
        // Scale so a "typical" Sponza pixel lands mid-gradient. Tunable from
        // the UI later; 256 is a reasonable starting point for heavily-shared
        // scenes.
        m_program->SetUniform1f("u_heatmap_scale", 256.0f);

        const GLuint groups_x = CeilDiv(extent.width, kWorkgroupSize);
        const GLuint groups_y = CeilDiv(extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);

        // Ensure subsequent texture samples see the compute writes.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer

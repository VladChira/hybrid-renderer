#include "renderer/passes/SsgiTracePass.h"

#include "core/Profiling.h"
#include "renderer/FrameResources.h"
#include "renderer/GeometryStore.h"
#include "renderer/MaterialStore.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/opengl/GLShaderProgram.h"

#include <algorithm>

#include <glm/gtc/matrix_inverse.hpp>

namespace hybrid::renderer
{

    namespace
    {
        constexpr GLuint kWorkgroupSize    = 8;
        constexpr GLuint kOutImageBinding  = 0;

        constexpr GLuint kTexUnitGbufferDepth   = 0;
        constexpr GLuint kTexUnitGbufferRt1     = 1;
        constexpr GLuint kTexUnitSceneRadiance  = 2;
        constexpr GLuint kTexUnitIrradiance     = 3;

        GLuint CeilDiv(GLuint value, GLuint divisor)
        {
            return (value + divisor - 1) / divisor;
        }
    } // namespace

    SsgiTracePass::SsgiTracePass(GLShaderProgram *program,
                                 GeometryStore *geometry_store,
                                 MaterialStore *material_store,
                                 raytracing::AccelerationStructureCache *as_cache)
        : m_program(program),
          m_geometry_store(geometry_store),
          m_material_store(material_store),
          m_as_cache(as_cache)
    {
    }

    bool SsgiTracePass::Execute(const SsgiTracePassInput &input)
    {
        HYBRID_PROFILE_ZONE_N("SsgiTracePass::Execute");
        HYBRID_PROFILE_GL_ZONE("SsgiTracePass");

        if (m_program == nullptr ||
            m_geometry_store == nullptr ||
            m_material_store == nullptr ||
            m_as_cache == nullptr ||
            input.settings == nullptr ||
            input.effective_view == nullptr ||
            input.gbuffer_depth == 0 ||
            input.gbuffer_rt1 == 0 ||
            input.ssgi_raw_texture == 0)
        {
            return false;
        }

        const RenderSettings &settings = *input.settings;
        const RenderView &view = *input.effective_view;
        const RenderExtent ssgi_extent = SsgiExtent(settings.render_extent);
        if (!ssgi_extent.IsValid())
        {
            return false;
        }

        if (!settings.enable_ssgi)
        {
            return true;
        }

        m_program->Use();

        glBindImageTexture(kOutImageBinding,
                           input.ssgi_raw_texture,
                           0, GL_FALSE, 0,
                           GL_WRITE_ONLY,
                           GL_RGBA16F);

        m_geometry_store->BindSsbos();
        m_material_store->BindSsbo();
        m_as_cache->BindSsbos();

        glActiveTexture(GL_TEXTURE0 + kTexUnitGbufferDepth);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_depth);
        glActiveTexture(GL_TEXTURE0 + kTexUnitGbufferRt1);
        glBindTexture(GL_TEXTURE_2D, input.gbuffer_rt1);
        glActiveTexture(GL_TEXTURE0 + kTexUnitSceneRadiance);
        glBindTexture(GL_TEXTURE_2D, input.scene_radiance_prev);
        glActiveTexture(GL_TEXTURE0 + kTexUnitIrradiance);
        glBindTexture(GL_TEXTURE_CUBE_MAP, input.irradiance_cubemap);

        m_program->SetUniform1i("u_gbuffer_depth",       static_cast<GLint>(kTexUnitGbufferDepth));
        m_program->SetUniform1i("u_gbuffer_rt1",         static_cast<GLint>(kTexUnitGbufferRt1));
        m_program->SetUniform1i("u_scene_radiance_prev", static_cast<GLint>(kTexUnitSceneRadiance));
        m_program->SetUniform1i("u_irradiance_cubemap",  static_cast<GLint>(kTexUnitIrradiance));

        m_program->SetUniformMat4("u_view",           view.view);
        m_program->SetUniformMat4("u_projection",     view.projection);
        m_program->SetUniformMat4("u_view_projection", view.projection * view.view);
        m_program->SetUniformMat4("u_inv_view",       glm::affineInverse(view.view));
        m_program->SetUniformMat4("u_inv_projection", glm::inverse(view.projection));
        m_program->SetUniformVec3("u_camera_position", view.position);

        const GLint output_size_loc = m_program->GetUniformLocation("u_output_size");
        if (output_size_loc >= 0)
        {
            glUniform2ui(output_size_loc,
                         static_cast<GLuint>(ssgi_extent.width),
                         static_cast<GLuint>(ssgi_extent.height));
        }
        m_program->SetUniform1ui("u_frame_index",     input.frame_index);
        m_program->SetUniform1ui("u_tlas_node_count", m_as_cache->Stats().tlas_nodes);
        m_program->SetUniform1f ("u_max_ray_distance", std::max(settings.ssgi_max_ray_distance, 0.1f));
        m_program->SetUniform1f ("u_ssgi_intensity",   std::max(settings.ssgi_intensity, 0.0f));
        m_program->SetUniform1f ("u_screen_march_thickness", std::max(settings.ssgi_screen_thickness, 1e-4f));
        m_program->SetUniform1ui("u_has_irradiance",   input.has_irradiance ? 1u : 0u);
        m_program->SetUniform1f ("u_skybox_intensity", input.skybox_intensity);
        m_program->SetUniform1f ("u_skybox_yaw_radians", input.skybox_yaw_radians);
        m_program->SetUniform1i ("u_debug_mode",       settings.ssgi_debug_mode);

        const GLuint groups_x = CeilDiv(ssgi_extent.width,  kWorkgroupSize);
        const GLuint groups_y = CeilDiv(ssgi_extent.height, kWorkgroupSize);
        glDispatchCompute(groups_x, groups_y, 1);
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT | GL_TEXTURE_FETCH_BARRIER_BIT);

        glActiveTexture(GL_TEXTURE0 + kTexUnitIrradiance);
        glBindTexture(GL_TEXTURE_CUBE_MAP, 0);
        for (GLuint u : {kTexUnitSceneRadiance, kTexUnitGbufferRt1, kTexUnitGbufferDepth})
        {
            glActiveTexture(GL_TEXTURE0 + u);
            glBindTexture(GL_TEXTURE_2D, 0);
        }
        glActiveTexture(GL_TEXTURE0);
        GLShaderProgram::Unuse();
        return true;
    }

} // namespace hybrid::renderer

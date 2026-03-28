#pragma once

#include "renderer/RendererTypes.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    struct HdriPrecomputePassInput
    {
        const FrameSceneData *scene_data = nullptr;
    };

    struct HdriPrecomputePassOutput
    {
        bool has_skybox = false;
        GlTextureId skybox_cubemap = 0;
        float skybox_intensity = 1.0f;
        float skybox_yaw_radians = 0.0f;
    };

    class HdriPrecomputePass final
    {
    public:
        explicit HdriPrecomputePass(GLShaderProgram *equirect_to_cubemap_shader);
        ~HdriPrecomputePass();

        const char *Name() const;
        bool Execute(const HdriPrecomputePassInput &input, HdriPrecomputePassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_equirect_to_cubemap_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace hybrid::renderer

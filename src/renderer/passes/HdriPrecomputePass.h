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

    };

    class HdriPrecomputePass final
    {
    public:
        explicit HdriPrecomputePass(GLShaderProgram *equirect_to_cubemap_shader, GLShaderProgram *irradiance_convolution);
        ~HdriPrecomputePass();

        const char *Name() const;
        bool Execute(const HdriPrecomputePassInput &input, HdriPrecomputePassOutput &output);

    private:
        struct Impl;
        GLShaderProgram *m_equirect_to_cubemap_shader = nullptr;
        GLShaderProgram *m_irradiance_convolution = nullptr;
        std::unique_ptr<Impl> m_impl;
    };
} // namespace hybrid::renderer

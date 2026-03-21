#pragma once

#include "renderer/RenderPass.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    class GBufferPass final : public IRenderPass
    {
    public:
        explicit GBufferPass(GLShaderProgram *gbuffer_shader);
        ~GBufferPass() override;

        const char *Name() const override;
        bool Execute(PassContext &context) override;

    private:
        struct Impl;
        GLShaderProgram *m_gbuffer_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer

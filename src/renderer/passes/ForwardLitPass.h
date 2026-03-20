#pragma once

#include "renderer/RenderPass.h"

#include <memory>

namespace hybrid::renderer
{
    class GLShaderProgram;

    class ForwardLitPass final : public IRenderPass
    {
    public:
        explicit ForwardLitPass(GLShaderProgram *forward_shader);
        ~ForwardLitPass() override;

        const char *Name() const override;
        bool Execute(PassContext &context) override;

    private:
        struct Impl;
        GLShaderProgram *m_forward_shader = nullptr;
        std::unique_ptr<Impl> m_impl;
    };

} // namespace hybrid::renderer

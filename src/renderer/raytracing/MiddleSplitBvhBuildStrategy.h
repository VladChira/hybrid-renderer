#pragma once

#include "renderer/raytracing/BvhBuilder.h"

namespace hybrid::renderer::raytracing
{

    class MiddleSplitBvhBuildStrategy final : public IBvhBuildStrategy
    {
    public:
        BvhBuildStrategyKind Kind() const override;
        const char *DebugName() const override;
        BvhBuildResult Build(const std::vector<BvhInput> &inputs, const BvhBuildConfig &config) const override;
    };

} // namespace hybrid::renderer::raytracing

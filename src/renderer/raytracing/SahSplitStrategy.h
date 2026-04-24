#pragma once

#include "renderer/raytracing/BvhBuilder.h"

namespace hybrid::renderer::raytracing
{

    class SahSplitStrategy final : public IBvhSplitStrategy
    {
    public:
        BvhSplitStrategyKind Kind() const override;
        const char *DebugName() const override;
        BvhSplitDecision ChooseSplit(const BvhSplitRequest &request, const BvhBuildConfig &config) const override;
    };

} // namespace hybrid::renderer::raytracing

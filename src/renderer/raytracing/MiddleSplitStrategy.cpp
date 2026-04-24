#include "renderer/raytracing/MiddleSplitStrategy.h"

namespace hybrid::renderer::raytracing
{

    BvhSplitStrategyKind MiddleSplitStrategy::Kind() const
    {
        return BvhSplitStrategyKind::MiddleSplit;
    }

    const char *MiddleSplitStrategy::DebugName() const
    {
        return "MiddleSplit";
    }

    BvhSplitDecision MiddleSplitStrategy::ChooseSplit(const BvhSplitRequest &request, const BvhBuildConfig &config) const
    {
        (void)request;
        (void)config;
        return {};
    }

} // namespace hybrid::renderer::raytracing

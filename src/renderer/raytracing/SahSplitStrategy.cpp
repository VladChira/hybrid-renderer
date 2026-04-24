#include "renderer/raytracing/SahSplitStrategy.h"

namespace hybrid::renderer::raytracing
{

    BvhSplitStrategyKind SahSplitStrategy::Kind() const
    {
        return BvhSplitStrategyKind::Sah;
    }

    const char *SahSplitStrategy::DebugName() const
    {
        return "SAH";
    }

    BvhSplitDecision SahSplitStrategy::ChooseSplit(const BvhSplitRequest &request, const BvhBuildConfig &config) const
    {
        (void)request;
        (void)config;
        return {};
    }

} // namespace hybrid::renderer::raytracing

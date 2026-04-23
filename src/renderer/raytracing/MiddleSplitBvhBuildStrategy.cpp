#include "renderer/raytracing/MiddleSplitBvhBuildStrategy.h"

namespace hybrid::renderer::raytracing
{

    BvhBuildStrategyKind MiddleSplitBvhBuildStrategy::Kind() const
    {
        return BvhBuildStrategyKind::MiddleSplit;
    }

    const char *MiddleSplitBvhBuildStrategy::DebugName() const
    {
        return "MiddleSplit";
    }

    BvhBuildResult MiddleSplitBvhBuildStrategy::Build(const std::vector<BvhInput> &inputs, const BvhBuildConfig &config) const
    {
        (void)inputs;
        (void)config;
        return {};
    }

} // namespace hybrid::renderer::raytracing

#include "renderer/raytracing/SahBvhBuildStrategy.h"

namespace hybrid::renderer::raytracing
{

    BvhBuildStrategyKind SahBvhBuildStrategy::Kind() const
    {
        return BvhBuildStrategyKind::Sah;
    }

    const char *SahBvhBuildStrategy::DebugName() const
    {
        return "SAH";
    }

    BvhBuildResult SahBvhBuildStrategy::Build(const std::vector<BvhInput> &inputs, const BvhBuildConfig &config) const
    {
        (void)inputs;
        (void)config;
        return {};
    }

} // namespace hybrid::renderer::raytracing

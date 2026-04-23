#include "renderer/raytracing/BvhBuilder.h"
#include "renderer/raytracing/MiddleSplitBvhBuildStrategy.h"
#include "renderer/raytracing/SahBvhBuildStrategy.h"

#include <memory>

namespace hybrid::renderer::raytracing
{

    const IBvhBuildStrategy &GetBvhBuildStrategy(BvhBuildStrategyKind kind)
    {
        static const MiddleSplitBvhBuildStrategy middle_split_strategy{};
        static const SahBvhBuildStrategy sah_strategy{};

        switch (kind)
        {
        case BvhBuildStrategyKind::MiddleSplit:
            return middle_split_strategy;
        case BvhBuildStrategyKind::Sah:
            return sah_strategy;
        default:
            return middle_split_strategy;
        }
    }

    std::unique_ptr<IBvhBuildStrategy> CreateBvhBuildStrategy(BvhBuildStrategyKind kind)
    {
        switch (kind)
        {
        case BvhBuildStrategyKind::MiddleSplit:
            return std::make_unique<MiddleSplitBvhBuildStrategy>();
        case BvhBuildStrategyKind::Sah:
            return std::make_unique<SahBvhBuildStrategy>();
        default:
            return std::make_unique<MiddleSplitBvhBuildStrategy>();
        }
    }

    BvhBuildResult BuildBvh(const std::vector<BvhInput> &inputs,
                            const BvhBuildConfig &config,
                            const IBvhBuildStrategy *strategy_override)
    {
        const IBvhBuildStrategy &strategy = strategy_override != nullptr
                                                ? *strategy_override
                                                : GetBvhBuildStrategy(config.strategy);

        BvhBuildResult result = strategy.Build(inputs, config);
        result.stats.strategy = strategy.Kind();
        return result;
    }

    Blas BuildBlas(const core::scene::MeshPrimitive &primitive,
                   const BvhBuildConfig &config,
                   const IBvhBuildStrategy *strategy_override)
    {
        (void)primitive;

        const IBvhBuildStrategy &strategy = strategy_override != nullptr
                                                ? *strategy_override
                                                : GetBvhBuildStrategy(config.strategy);

        Blas out{};
        out.stats.strategy = strategy.Kind();
        return out;
    }

} // namespace hybrid::renderer::raytracing

#include "renderer/raytracing/BvhBuilder.h"
#include "renderer/raytracing/MiddleSplitStrategy.h"
#include "renderer/raytracing/SahSplitStrategy.h"

#include <memory>

namespace hybrid::renderer::raytracing
{

    const IBvhSplitStrategy &GetBvhSplitStrategy(BvhSplitStrategyKind kind)
    {
        static const MiddleSplitStrategy middle_split_strategy{};
        static const SahSplitStrategy sah_strategy{};

        switch (kind)
        {
        case BvhSplitStrategyKind::MiddleSplit:
            return middle_split_strategy;
        case BvhSplitStrategyKind::Sah:
            return sah_strategy;
        default:
            return middle_split_strategy;
        }
    }

    std::unique_ptr<IBvhSplitStrategy> CreateBvhSplitStrategy(BvhSplitStrategyKind kind)
    {
        switch (kind)
        {
        case BvhSplitStrategyKind::MiddleSplit:
            return std::make_unique<MiddleSplitStrategy>();
        case BvhSplitStrategyKind::Sah:
            return std::make_unique<SahSplitStrategy>();
        default:
            return std::make_unique<MiddleSplitStrategy>();
        }
    }

    BvhBuildResult BuildBvh(const std::vector<BvhInput> &inputs,
                            const BvhBuildConfig &config,
                            const IBvhSplitStrategy *split_strategy_override)
    {
        const IBvhSplitStrategy &split_strategy = split_strategy_override != nullptr
                                                      ? *split_strategy_override
                                                      : GetBvhSplitStrategy(config.split_strategy);

        (void)inputs;

        BvhBuildResult result{};
        result.stats.split_strategy = split_strategy.Kind();
        return result;
    }

    Blas BuildBlas(const core::scene::MeshPrimitive &primitive,
                   const BvhBuildConfig &config,
                   const IBvhSplitStrategy *split_strategy_override)
    {
        (void)primitive;

        const IBvhSplitStrategy &split_strategy = split_strategy_override != nullptr
                                                      ? *split_strategy_override
                                                      : GetBvhSplitStrategy(config.split_strategy);

        Blas out{};
        out.stats.split_strategy = split_strategy.Kind();
        return out;
    }

} // namespace hybrid::renderer::raytracing

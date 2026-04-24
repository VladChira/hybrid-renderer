#include "renderer/raytracing/MiddleSplitStrategy.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>

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
        (void)config;

        BvhSplitDecision decision{};
        if (request.primitive_count < 2 || !request.centroid_bounds.valid)
        {
            return decision;
        }

        if (request.inputs == nullptr || request.primitive_indices == nullptr)
        {
            return decision;
        }

        const std::vector<BvhInput> &inputs = *request.inputs;
        const std::vector<uint32_t> &order  = *request.primitive_indices;

        const uint64_t begin = request.first_primitive;
        const uint64_t end   = begin + request.primitive_count;
        if (end > order.size())
        {
            return decision;
        }

        const glm::vec3 extent = request.centroid_bounds.max - request.centroid_bounds.min;
        constexpr float kExtentEpsilon = 1e-6f;

        std::array<uint32_t, 3> axis_order{0u, 1u, 2u};
        std::sort(axis_order.begin(), axis_order.end(), [&](uint32_t lhs, uint32_t rhs) {
            return extent[lhs] > extent[rhs];
        });

        for (const uint32_t axis : axis_order)
        {
            if (!std::isfinite(extent[axis]) || extent[axis] <= kExtentEpsilon)
            {
                continue;
            }

            const float axis_min = request.centroid_bounds.min[axis];
            const float axis_max = request.centroid_bounds.max[axis];
            const float split_position = 0.5f * (axis_min + axis_max);
            if (!std::isfinite(split_position))
            {
                continue;
            }

            uint32_t left_count = 0;
            uint32_t right_count = 0;
            for (uint64_t i = begin; i < end; ++i)
            {
                const uint32_t input_index = order[static_cast<std::size_t>(i)];
                if (input_index >= inputs.size())
                {
                    return {};
                }

                const float c = inputs[input_index].centroid[axis];
                if (c < split_position)
                {
                    ++left_count;
                }
                else
                {
                    ++right_count;
                }
            }

            if (left_count == 0 || right_count == 0)
            {
                continue;
            }

            decision.valid = true;
            decision.axis = axis;
            decision.split_position = split_position;
            return decision;
        }

        return decision;
    }

} // namespace hybrid::renderer::raytracing

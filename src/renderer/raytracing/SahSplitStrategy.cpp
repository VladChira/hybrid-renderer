#include "renderer/raytracing/SahSplitStrategy.h"

#include "core/scene/types/SceneMath.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace hybrid::renderer::raytracing
{
    namespace
    {
        struct Bucket
        {
            core::scene::Aabb bounds = core::scene::EmptyAabb();
            uint32_t          count  = 0;
        };

        float SurfaceArea(const core::scene::Aabb &bounds)
        {
            if (!bounds.valid)
            {
                return 0.0f;
            }

            const glm::vec3 extent = glm::max(bounds.max - bounds.min, glm::vec3(0.0f));
            return 2.0f * (extent.x * extent.y + extent.y * extent.z + extent.z * extent.x);
        }
    } // namespace

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
        BvhSplitDecision best{};
        if (request.inputs == nullptr || request.primitive_indices == nullptr)
        {
            return best;
        }
        if (request.primitive_count < 2 || !request.centroid_bounds.valid || !request.primitive_bounds.valid)
        {
            return best;
        }

        const std::vector<BvhInput> &inputs = *request.inputs;
        const std::vector<uint32_t> &order  = *request.primitive_indices;

        const uint64_t begin = request.first_primitive;
        const uint64_t end   = begin + request.primitive_count;
        if (end > order.size())
        {
            return best;
        }

        const uint32_t bucket_count = std::max(2u, config.sah_bucket_count);
        const glm::vec3 centroid_extent = request.centroid_bounds.max - request.centroid_bounds.min;
        const float node_surface_area = SurfaceArea(request.primitive_bounds);

        std::vector<Bucket> buckets(bucket_count);
        std::vector<core::scene::Aabb> left_bounds(bucket_count);
        std::vector<core::scene::Aabb> right_bounds(bucket_count);
        std::vector<uint32_t> left_count(bucket_count, 0u);
        std::vector<uint32_t> right_count(bucket_count, 0u);

        for (uint32_t axis = 0; axis < 3; ++axis)
        {
            if (!std::isfinite(centroid_extent[axis]) || centroid_extent[axis] <= 0.0f)
            {
                continue;
            }

            std::fill(buckets.begin(), buckets.end(), Bucket{});
            const float inv_extent = static_cast<float>(bucket_count) / centroid_extent[axis];

            for (uint64_t i = begin; i < end; ++i)
            {
                const uint32_t input_index = order[static_cast<std::size_t>(i)];
                if (input_index >= inputs.size())
                {
                    return {};
                }

                const BvhInput &primitive = inputs[input_index];
                const float offset = (primitive.centroid[axis] - request.centroid_bounds.min[axis]) * inv_extent;
                const int32_t bucket_index =
                    std::clamp(static_cast<int32_t>(offset), 0, static_cast<int32_t>(bucket_count) - 1);
                Bucket &bucket = buckets[static_cast<uint32_t>(bucket_index)];
                core::scene::ExpandAabbToInclude(bucket.bounds, primitive.bounds);
                ++bucket.count;
            }

            core::scene::Aabb accum_bounds = core::scene::EmptyAabb();
            uint32_t accum_count = 0;
            for (uint32_t b = 0; b < bucket_count; ++b)
            {
                core::scene::ExpandAabbToInclude(accum_bounds, buckets[b].bounds);
                accum_count += buckets[b].count;
                left_bounds[b] = accum_bounds;
                left_count[b] = accum_count;
            }

            accum_bounds = core::scene::EmptyAabb();
            accum_count = 0;
            for (int32_t b = static_cast<int32_t>(bucket_count) - 1; b >= 0; --b)
            {
                const uint32_t bucket_index = static_cast<uint32_t>(b);
                core::scene::ExpandAabbToInclude(accum_bounds, buckets[bucket_index].bounds);
                accum_count += buckets[bucket_index].count;
                right_bounds[bucket_index] = accum_bounds;
                right_count[bucket_index] = accum_count;
            }

            for (uint32_t b = 0; b + 1 < bucket_count; ++b)
            {
                const uint32_t lc = left_count[b];
                const uint32_t rc = right_count[b + 1];
                if (lc == 0 || rc == 0)
                {
                    continue;
                }

                const float split_cost =
                    1.0f +
                    (SurfaceArea(left_bounds[b]) * static_cast<float>(lc) +
                     SurfaceArea(right_bounds[b + 1]) * static_cast<float>(rc)) /
                    std::max(node_surface_area, 1e-6f);

                if (split_cost < best.cost)
                {
                    best.valid = true;
                    best.axis = axis;
                    best.split_bucket = b;
                    best.bucket_count = bucket_count;
                    best.use_sah_buckets = true;
                    best.cost = split_cost;
                }
            }
        }

        if (!best.valid)
        {
            return best;
        }

        const float extent = centroid_extent[best.axis];
        if (!std::isfinite(extent) || extent <= 0.0f)
        {
            return {};
        }

        const float bucket_boundary = static_cast<float>(best.split_bucket + 1u) / static_cast<float>(best.bucket_count);
        best.split_position = request.centroid_bounds.min[best.axis] + extent * bucket_boundary;
        if (!std::isfinite(best.split_position))
        {
            return {};
        }

        return best;
    }

} // namespace hybrid::renderer::raytracing

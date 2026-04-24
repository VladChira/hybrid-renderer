#include "renderer/raytracing/BvhBuilder.h"
#include "renderer/raytracing/MiddleSplitStrategy.h"
#include "renderer/raytracing/SahSplitStrategy.h"
#include "core/scene/types/SceneMath.h"

#include <core/Log.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <memory>
#include <stack>

namespace hybrid::renderer::raytracing
{

    namespace
    {

        core::scene::Aabb BoundsOfRange(const std::vector<BvhInput> &inputs,
                                         const std::vector<uint32_t> &order,
                                         uint32_t begin,
                                         uint32_t end)
        {
            core::scene::Aabb bounds = core::scene::EmptyAabb();
            for (uint32_t i = begin; i < end; ++i)
            {
                core::scene::ExpandAabbToInclude(bounds, inputs[order[i]].bounds);
            }
            return bounds;
        }

        core::scene::Aabb CentroidBoundsOfRange(const std::vector<BvhInput> &inputs,
                                                 const std::vector<uint32_t> &order,
                                                 uint32_t begin,
                                                 uint32_t end)
        {
            core::scene::Aabb bounds = core::scene::EmptyAabb();
            for (uint32_t i = begin; i < end; ++i)
            {
                core::scene::ExpandAabbToInclude(bounds, inputs[order[i]].centroid);
            }
            return bounds;
        }

        void WriteLeaf(BvhNode &node, const core::scene::Aabb &bounds, uint32_t first, uint32_t count)
        {
            node.bmin = bounds.valid ? bounds.min : glm::vec3(0.0f);
            node.bmax = bounds.valid ? bounds.max : glm::vec3(0.0f);
            node.left_or_first = static_cast<int32_t>(first);
            node.right_or_count = -static_cast<int32_t>(count);
        }

        float ComponentAt(const glm::vec3 &v, uint32_t axis)
        {
            switch (axis)
            {
            case 0:
                return v.x;
            case 1:
                return v.y;
            case 2:
                return v.z;
            default:
                return v.x;
            }
        }

        uint32_t Partition(const std::vector<BvhInput> &inputs,
                           std::vector<uint32_t> &order,
                           uint32_t begin,
                           uint32_t end,
                           const BvhSplitDecision &split)
        {
            if (begin >= end || split.axis > 2)
            {
                return begin;
            }

            auto first = order.begin() + static_cast<std::ptrdiff_t>(begin);
            auto last = order.begin() + static_cast<std::ptrdiff_t>(end);

            const auto mid_it = std::partition(first, last, [&](uint32_t input_index) {
                const float c = ComponentAt(inputs[input_index].centroid, split.axis);
                return c < split.split_position;
            });

            return static_cast<uint32_t>(std::distance(order.begin(), mid_it));
        }

    } // namespace

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

        BvhBuildResult out;
        out.stats.split_strategy = split_strategy.Kind();
        out.stats.primitive_count = static_cast<uint32_t>(inputs.size());

        if (inputs.empty())
        {
            return out;
        }

        const auto start = std::chrono::steady_clock::now();

        std::vector<uint32_t> order(inputs.size());
        for (uint32_t i = 0; i < static_cast<uint32_t>(order.size()); ++i)
        {
            order[i] = i;
        }

        // Pre-allocate an upper-bound number of nodes (2*N - 1 for N leaves;
        // cap leaves at primitive_count since max_leaf_primitives >= 1).
        out.nodes.reserve(std::max<size_t>(1, 2 * inputs.size()));
        out.nodes.push_back(BvhNode{});

        // Start building the BVH, mirroring a recursive approach
        // but using a stack instead.
        // The BuildWorkItem is a unit of work, it represents
        // the rest of the hierarchy under a particular node.
        // the begin,end range is the primitive range this node owns
        // It will of course become smaller as the depth of the node increases

        std::stack<BuildWorkItem> work;
        // Push the root into the stack, which encompasses
        // all primitives, is the first in the BvhNode array
        // and is at depth 0
        work.push({0, static_cast<uint32_t>(inputs.size()), 0, 0});

        while(!work.empty())
        {
            const BuildWorkItem item = work.top();
            work.pop();

            // Define the conditions for a node to become a leaf
            // Max leaves hit, tree depth hit, invalid bounds
            const uint32_t count = item.end - item.begin;
            const core::scene::Aabb node_bounds     = BoundsOfRange(inputs, order, item.begin, item.end);
            const core::scene::Aabb centroid_bounds = CentroidBoundsOfRange(inputs, order, item.begin, item.end);
            const glm::vec3 centroid_extent = centroid_bounds.max - centroid_bounds.min;
            constexpr float kCentroidExtentEpsilon = 1e-6f;
            const bool degenerate_centroid_extent =
                std::abs(centroid_extent.x) <= kCentroidExtentEpsilon &&
                std::abs(centroid_extent.y) <= kCentroidExtentEpsilon &&
                std::abs(centroid_extent.z) <= kCentroidExtentEpsilon;

            const bool forced_leaf =
                count <= config.max_leaf_primitives ||
                item.depth >= config.max_depth ||
                !centroid_bounds.valid ||
                degenerate_centroid_extent;

            BvhSplitDecision split{};
            if (!forced_leaf)
            {
                // Delegate to the split strategy
                BvhSplitRequest request{};
                request.inputs = &inputs;
                request.primitive_indices = &order;
                request.first_primitive = item.begin;
                request.primitive_count = count;
                request.depth = item.depth;
                request.primitive_bounds = node_bounds;
                request.centroid_bounds = centroid_bounds;

                split = split_strategy.ChooseSplit(request, config);
            }

            if (!forced_leaf && !split.valid)
            {
                LOG_WARN("Split strategy '{}' returned an invalid split at depth {} for primitive range [{}, {}).",
                         split_strategy.DebugName(),
                         item.depth,
                         item.begin,
                         item.end);
            }
            const bool commit_split = !forced_leaf && split.valid;

            if (!commit_split)
            {
                // If not splitting, this is a leaf, mark it as such
                const uint32_t first = static_cast<uint32_t>(out.primitive_indices.size());
                for (uint32_t i = item.begin; i < item.end; ++i)
                {
                    out.primitive_indices.push_back(inputs[order[i]].payload_index);
                }
                WriteLeaf(out.nodes[item.node_index], node_bounds, first, count);
                out.stats.leaf_count++;
                out.stats.max_depth = std::max(out.stats.max_depth, item.depth);
                continue;
            }

            // If we do decide to split, partition the node by that split
            // and push the new nodes to the stack

            const uint32_t mid = Partition(inputs, order, item.begin, item.end, split);

            if (mid == item.begin || mid == item.end)
            {
                // Degenerate partition — fall back to a leaf to guarantee progress.
                const uint32_t first = static_cast<uint32_t>(out.primitive_indices.size());
                for (uint32_t i = item.begin; i < item.end; ++i)
                {
                    out.primitive_indices.push_back(inputs[order[i]].payload_index);
                }
                WriteLeaf(out.nodes[item.node_index], node_bounds, first, count);
                out.stats.leaf_count++;
                out.stats.max_depth = std::max(out.stats.max_depth, item.depth);
                continue;
            }

            const uint32_t left_index  = static_cast<uint32_t>(out.nodes.size());
            out.nodes.push_back(BvhNode{});
            const uint32_t right_index = static_cast<uint32_t>(out.nodes.size());
            out.nodes.push_back(BvhNode{});

            BvhNode &node = out.nodes[item.node_index];
            node.bmin = node_bounds.min;
            node.bmax = node_bounds.max;
            node.left_or_first  = static_cast<int32_t>(left_index);
            node.right_or_count = static_cast<int32_t>(right_index);


            // Push the larger half first so it's processed last — keeps the
            // stack depth bounded by log2(N).
            const uint32_t left_count  = mid - item.begin;
            const uint32_t right_count = item.end - mid;
            if (left_count >= right_count)
            {
                work.push({item.begin, mid, left_index, item.depth + 1});
                work.push({mid, item.end, right_index, item.depth + 1});
            }
            else
            {
                work.push({mid, item.end, right_index, item.depth + 1});
                work.push({item.begin, mid, left_index, item.depth + 1});
            }
        }

        out.bounds = out.nodes.empty()
                         ? core::scene::Aabb{}
                         : [&]() {
                               core::scene::Aabb b{};
                               b.min = out.nodes[0].bmin;
                               b.max = out.nodes[0].bmax;
                               b.valid = true;
                               return b;
                           }();

        out.stats.node_count = static_cast<uint32_t>(out.nodes.size());
        const auto end = std::chrono::steady_clock::now();
        out.stats.build_ms = std::chrono::duration<double, std::milli>(end - start).count();
        return out;
    }

    Blas BuildBlas(const core::scene::MeshPrimitive &primitive,
                   const BvhBuildConfig &config,
                   const IBvhSplitStrategy *split_strategy_override)
    {
        Blas blas;
        const IBvhSplitStrategy &split_strategy = split_strategy_override != nullptr
                                                      ? *split_strategy_override
                                                      : GetBvhSplitStrategy(config.split_strategy);
        blas.stats.split_strategy = split_strategy.Kind();

        if (primitive.indices.size() < 3 || primitive.vertices.empty())
        {
            return blas;
        }

        const uint32_t triangle_count = static_cast<uint32_t>(primitive.indices.size() / 3);

        std::vector<BvhInput> inputs;
        inputs.reserve(triangle_count);
        for (uint32_t tri = 0; tri < triangle_count; ++tri)
        {
            const uint32_t i0 = primitive.indices[tri * 3 + 0];
            const uint32_t i1 = primitive.indices[tri * 3 + 1];
            const uint32_t i2 = primitive.indices[tri * 3 + 2];
            if (i0 >= primitive.vertices.size() || i1 >= primitive.vertices.size() || i2 >= primitive.vertices.size())
            {
                continue;
            }
            const glm::vec3 &v0 = primitive.vertices[i0].position;
            const glm::vec3 &v1 = primitive.vertices[i1].position;
            const glm::vec3 &v2 = primitive.vertices[i2].position;

            BvhInput input{};
            input.bounds = core::scene::EmptyAabb();
            core::scene::ExpandAabbToInclude(input.bounds, v0);
            core::scene::ExpandAabbToInclude(input.bounds, v1);
            core::scene::ExpandAabbToInclude(input.bounds, v2);
            input.centroid      = (v0 + v1 + v2) * (1.0f / 3.0f);
            input.payload_index = tri;
            inputs.push_back(input);
        }

        BvhBuildResult result = BuildBvh(inputs, config, split_strategy_override);
        blas.nodes            = std::move(result.nodes);
        blas.triangle_indices = std::move(result.primitive_indices);
        blas.bounds           = result.bounds;
        blas.stats            = result.stats;
        return blas;
    }

    bool IntersectRayAabb(const glm::vec3 &origin,
                          const glm::vec3 &inv_direction,
                          const glm::vec3 &bmin,
                          const glm::vec3 &bmax,
                          float t_min,
                          float t_max,
                          float &out_t_near)
    {
        const glm::vec3 t1 = (bmin - origin) * inv_direction;
        const glm::vec3 t2 = (bmax - origin) * inv_direction;
        const glm::vec3 tmin3 = glm::min(t1, t2);
        const glm::vec3 tmax3 = glm::max(t1, t2);
        const float near_t = std::max(std::max(tmin3.x, tmin3.y), std::max(tmin3.z, t_min));
        const float far_t  = std::min(std::min(tmax3.x, tmax3.y), std::min(tmax3.z, t_max));
        if (near_t > far_t)
        {
            return false;
        }
        out_t_near = near_t;
        return true;
    }

    bool IntersectRayTriangle(const glm::vec3 &origin,
                              const glm::vec3 &direction,
                              const glm::vec3 &v0,
                              const glm::vec3 &v1,
                              const glm::vec3 &v2,
                              float t_min,
                              float t_max,
                              float &out_t,
                              float &out_u,
                              float &out_v)
    {
        constexpr float kEpsilon = 1e-8f;
        const glm::vec3 e1 = v1 - v0;
        const glm::vec3 e2 = v2 - v0;
        const glm::vec3 p  = glm::cross(direction, e2);
        const float det    = glm::dot(e1, p);
        if (std::fabs(det) < kEpsilon)
        {
            return false;
        }
        const float inv_det = 1.0f / det;
        const glm::vec3 s = origin - v0;
        const float u = glm::dot(s, p) * inv_det;
        if (u < 0.0f || u > 1.0f)
        {
            return false;
        }
        const glm::vec3 q = glm::cross(s, e1);
        const float v = glm::dot(direction, q) * inv_det;
        if (v < 0.0f || u + v > 1.0f)
        {
            return false;
        }
        const float t = glm::dot(e2, q) * inv_det;
        if (t < t_min || t > t_max)
        {
            return false;
        }
        out_t = t;
        out_u = u;
        out_v = v;
        return true;
    }

} // namespace hybrid::renderer::raytracing

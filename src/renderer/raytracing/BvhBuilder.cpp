#include "renderer/raytracing/BvhBuilder.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <stack>

namespace hybrid::renderer::raytracing
{

    namespace
    {
        // ---------------------------------------------------------------------
        // AABB utilities
        // ---------------------------------------------------------------------

        core::scene::Aabb MakeEmpty()
        {
            core::scene::Aabb a{};
            a.min = glm::vec3(std::numeric_limits<float>::infinity());
            a.max = glm::vec3(-std::numeric_limits<float>::infinity());
            a.valid = false;
            return a;
        }

        void ExpandToInclude(core::scene::Aabb &a, const core::scene::Aabb &b)
        {
            if (!b.valid)
            {
                return;
            }
            a.min = glm::min(a.min, b.min);
            a.max = glm::max(a.max, b.max);
            a.valid = true;
        }

        void ExpandToInclude(core::scene::Aabb &a, const glm::vec3 &p)
        {
            a.min = glm::min(a.min, p);
            a.max = glm::max(a.max, p);
            a.valid = true;
        }

        float SurfaceArea(const core::scene::Aabb &a)
        {
            if (!a.valid)
            {
                return 0.0f;
            }
            const glm::vec3 d = glm::max(a.max - a.min, glm::vec3(0.0f));
            return 2.0f * (d.x * d.y + d.y * d.z + d.z * d.x);
        }

        // ---------------------------------------------------------------------
        // SAH bucket builder
        // ---------------------------------------------------------------------

        struct BuildWorkItem
        {
            uint32_t begin;
            uint32_t end;        // exclusive
            uint32_t node_index;
            uint32_t depth;
        };

        core::scene::Aabb BoundsOfRange(const std::vector<BvhInput> &inputs,
                                         const std::vector<uint32_t> &order,
                                         uint32_t begin,
                                         uint32_t end)
        {
            core::scene::Aabb bounds = MakeEmpty();
            for (uint32_t i = begin; i < end; ++i)
            {
                ExpandToInclude(bounds, inputs[order[i]].bounds);
            }
            return bounds;
        }

        core::scene::Aabb CentroidBoundsOfRange(const std::vector<BvhInput> &inputs,
                                                 const std::vector<uint32_t> &order,
                                                 uint32_t begin,
                                                 uint32_t end)
        {
            core::scene::Aabb bounds = MakeEmpty();
            for (uint32_t i = begin; i < end; ++i)
            {
                ExpandToInclude(bounds, inputs[order[i]].centroid);
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

        struct SplitResult
        {
            bool     is_valid = false;
            uint32_t axis     = 0;
            uint32_t bucket   = 0;
            float    cost     = std::numeric_limits<float>::infinity();
        };

        struct Bucket
        {
            core::scene::Aabb bounds = MakeEmpty();
            uint32_t          count  = 0;
        };

        SplitResult FindBestSplit(const std::vector<BvhInput> &inputs,
                                   const std::vector<uint32_t> &order,
                                   uint32_t begin,
                                   uint32_t end,
                                   const core::scene::Aabb &node_bounds,
                                   const core::scene::Aabb &centroid_bounds,
                                   uint32_t bucket_count)
        {
            SplitResult best{};

            if (!centroid_bounds.valid)
            {
                return best;
            }

            const glm::vec3 centroid_extent = centroid_bounds.max - centroid_bounds.min;
            const float node_sa = SurfaceArea(node_bounds);
            const uint32_t primitive_count = end - begin;

            std::vector<Bucket> buckets(bucket_count);
            std::vector<core::scene::Aabb> left_bounds(bucket_count);
            std::vector<core::scene::Aabb> right_bounds(bucket_count);
            std::vector<uint32_t> left_count(bucket_count, 0);
            std::vector<uint32_t> right_count(bucket_count, 0);

            for (uint32_t axis = 0; axis < 3; ++axis)
            {
                if (centroid_extent[axis] <= 0.0f)
                {
                    continue;
                }

                std::fill(buckets.begin(), buckets.end(), Bucket{});
                const float inv_extent = static_cast<float>(bucket_count) / centroid_extent[axis];

                for (uint32_t i = begin; i < end; ++i)
                {
                    const BvhInput &p = inputs[order[i]];
                    const float offset = (p.centroid[axis] - centroid_bounds.min[axis]) * inv_extent;
                    uint32_t b = static_cast<uint32_t>(std::clamp(static_cast<int32_t>(offset),
                                                                  0,
                                                                  static_cast<int32_t>(bucket_count) - 1));
                    ExpandToInclude(buckets[b].bounds, p.bounds);
                    buckets[b].count++;
                }

                core::scene::Aabb acc = MakeEmpty();
                uint32_t acc_count = 0;
                for (uint32_t b = 0; b < bucket_count; ++b)
                {
                    ExpandToInclude(acc, buckets[b].bounds);
                    acc_count += buckets[b].count;
                    left_bounds[b] = acc;
                    left_count[b]  = acc_count;
                }

                acc = MakeEmpty();
                acc_count = 0;
                for (int32_t b = static_cast<int32_t>(bucket_count) - 1; b >= 0; --b)
                {
                    ExpandToInclude(acc, buckets[static_cast<uint32_t>(b)].bounds);
                    acc_count += buckets[static_cast<uint32_t>(b)].count;
                    right_bounds[static_cast<uint32_t>(b)] = acc;
                    right_count[static_cast<uint32_t>(b)]  = acc_count;
                }

                for (uint32_t b = 0; b + 1 < bucket_count; ++b)
                {
                    const uint32_t lc = left_count[b];
                    const uint32_t rc = right_count[b + 1];
                    if (lc == 0 || rc == 0)
                    {
                        continue;
                    }
                    const float cost =
                        1.0f +
                        (SurfaceArea(left_bounds[b]) * static_cast<float>(lc) +
                         SurfaceArea(right_bounds[b + 1]) * static_cast<float>(rc)) /
                        std::max(node_sa, 1e-6f);
                    if (cost < best.cost)
                    {
                        best.is_valid = true;
                        best.axis     = axis;
                        best.bucket   = b;
                        best.cost     = cost;
                    }
                }
            }

            // Leaf cost comparison — the caller uses this to decide whether
            // the split is worth taking over a leaf that holds all primitives.
            const float leaf_cost = static_cast<float>(primitive_count);
            if (best.is_valid && best.cost >= leaf_cost && primitive_count <= std::numeric_limits<uint32_t>::max())
            {
                // keep is_valid; the caller still inspects the cost vs. leaf cost
            }
            (void)leaf_cost;
            return best;
        }

        uint32_t Partition(const std::vector<BvhInput> &inputs,
                           std::vector<uint32_t> &order,
                           uint32_t begin,
                           uint32_t end,
                           const core::scene::Aabb &centroid_bounds,
                           uint32_t axis,
                           uint32_t split_bucket,
                           uint32_t bucket_count)
        {
            const float min_v = centroid_bounds.min[axis];
            const float extent = centroid_bounds.max[axis] - centroid_bounds.min[axis];
            const float inv_extent = extent > 0.0f ? static_cast<float>(bucket_count) / extent : 0.0f;
            uint32_t mid = begin;
            for (uint32_t i = begin; i < end; ++i)
            {
                const float offset = (inputs[order[i]].centroid[axis] - min_v) * inv_extent;
                const int32_t b = std::clamp(static_cast<int32_t>(offset),
                                             0,
                                             static_cast<int32_t>(bucket_count) - 1);
                if (static_cast<uint32_t>(b) <= split_bucket)
                {
                    std::swap(order[i], order[mid]);
                    ++mid;
                }
            }
            return mid;
        }
    } // namespace

    // -------------------------------------------------------------------------
    // Generic builder
    // -------------------------------------------------------------------------

    BvhBuildResult BuildBvh(const std::vector<BvhInput> &inputs, const BvhBuildConfig &config)
    {
        BvhBuildResult out;
        out.stats.primitive_count = static_cast<uint32_t>(inputs.size());

        if (inputs.empty())
        {
            return out;
        }

        const auto start = std::chrono::steady_clock::now();

        std::vector<uint32_t> order(inputs.size());
        for (uint32_t i = 0; i < order.size(); ++i)
        {
            order[i] = i;
        }

        // Pre-allocate an upper-bound number of nodes (2*N - 1 for N leaves;
        // cap leaves at primitive_count since max_leaf_primitives >= 1).
        out.nodes.reserve(std::max<size_t>(1, 2 * inputs.size()));
        out.nodes.push_back(BvhNode{});

        std::stack<BuildWorkItem> work;
        work.push({0, static_cast<uint32_t>(inputs.size()), 0, 0});

        while (!work.empty())
        {
            const BuildWorkItem item = work.top();
            work.pop();

            const uint32_t count = item.end - item.begin;
            const core::scene::Aabb node_bounds     = BoundsOfRange(inputs, order, item.begin, item.end);
            const core::scene::Aabb centroid_bounds = CentroidBoundsOfRange(inputs, order, item.begin, item.end);

            const bool forced_leaf =
                count <= config.max_leaf_primitives ||
                item.depth >= config.max_depth ||
                !centroid_bounds.valid ||
                (centroid_bounds.max == centroid_bounds.min);

            SplitResult split{};
            if (!forced_leaf)
            {
                split = FindBestSplit(inputs, order, item.begin, item.end,
                                      node_bounds, centroid_bounds, config.sah_bucket_count);
            }

            const float leaf_cost = static_cast<float>(count);
            const bool take_split = !forced_leaf && split.is_valid &&
                                    (count > config.max_leaf_primitives || split.cost < leaf_cost);

            if (!take_split)
            {
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

            const uint32_t mid = Partition(inputs, order, item.begin, item.end,
                                           centroid_bounds, split.axis, split.bucket,
                                           config.sah_bucket_count);

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

    // -------------------------------------------------------------------------
    // BLAS wrapper
    // -------------------------------------------------------------------------

    Blas BuildBlas(const core::scene::MeshPrimitive &primitive, const BvhBuildConfig &config)
    {
        Blas blas;
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
            input.bounds = MakeEmpty();
            ExpandToInclude(input.bounds, v0);
            ExpandToInclude(input.bounds, v1);
            ExpandToInclude(input.bounds, v2);
            input.centroid      = (v0 + v1 + v2) * (1.0f / 3.0f);
            input.payload_index = tri;
            inputs.push_back(input);
        }

        BvhBuildResult result = BuildBvh(inputs, config);
        blas.nodes            = std::move(result.nodes);
        blas.triangle_indices = std::move(result.primitive_indices);
        blas.bounds           = result.bounds;
        blas.stats            = result.stats;
        return blas;
    }

    // -------------------------------------------------------------------------
    // Ray intersection helpers (shared with tests)
    // -------------------------------------------------------------------------

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

#include "renderer/raytracing/Bvh.h"
#include "renderer/raytracing/BvhBuilder.h"
#include "core/scene/types/SceneAssets.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <random>
#include <stack>
#include <string>
#include <unordered_set>
#include <utility>

namespace rt = hybrid::renderer::raytracing;

namespace
{
    rt::BvhInput MakePointAabb(const glm::vec3 &center, float half_extent, uint32_t payload)
    {
        rt::BvhInput input{};
        input.bounds.min = center - glm::vec3(half_extent);
        input.bounds.max = center + glm::vec3(half_extent);
        input.bounds.valid = true;
        input.centroid = center;
        input.payload_index = payload;
        return input;
    }

    // Recursively walk a built BVH and verify the key invariants:
    // - every input primitive index shows up in exactly one leaf
    // - every internal node's bounds contain its children's bounds
    // - no leaf is empty
    struct BvhInvariantResult
    {
        bool ok = true;
        std::string message;
        uint32_t leaf_primitive_count = 0;
        uint32_t max_depth = 0;
    };

    BvhInvariantResult VerifyBvh(const rt::BvhBuildResult &result, uint32_t expected_primitives)
    {
        BvhInvariantResult out{};
        std::unordered_set<uint32_t> seen;
        std::stack<std::pair<uint32_t, uint32_t>> stack;  // (node index, depth)
        stack.push({0, 0});

        while (!stack.empty())
        {
            auto [node_index, depth] = stack.top();
            stack.pop();
            out.max_depth = std::max(out.max_depth, depth);
            const rt::BvhNode &node = result.nodes[node_index];

            if (node.bmin.x > node.bmax.x || node.bmin.y > node.bmax.y || node.bmin.z > node.bmax.z)
            {
                out.ok = false;
                out.message = "node has inverted bounds";
                return out;
            }

            if (rt::IsLeaf(node))
            {
                const uint32_t first = rt::LeafFirst(node);
                const uint32_t count = rt::LeafCount(node);
                if (count == 0)
                {
                    out.ok = false;
                    out.message = "empty leaf";
                    return out;
                }
                for (uint32_t i = 0; i < count; ++i)
                {
                    const uint32_t payload = result.primitive_indices[first + i];
                    if (!seen.insert(payload).second)
                    {
                        out.ok = false;
                        out.message = "duplicate primitive across leaves";
                        return out;
                    }
                }
                out.leaf_primitive_count += count;
                continue;
            }

            const uint32_t left = static_cast<uint32_t>(node.left_or_first);
            const uint32_t right = static_cast<uint32_t>(node.right_or_count);
            const rt::BvhNode &ln = result.nodes[left];
            const rt::BvhNode &rn = result.nodes[right];
            for (int axis = 0; axis < 3; ++axis)
            {
                if (ln.bmin[axis] < node.bmin[axis] - 1e-4f || ln.bmax[axis] > node.bmax[axis] + 1e-4f ||
                    rn.bmin[axis] < node.bmin[axis] - 1e-4f || rn.bmax[axis] > node.bmax[axis] + 1e-4f)
                {
                    out.ok = false;
                    out.message = "child bounds escape parent";
                    return out;
                }
            }
            stack.push({left, depth + 1});
            stack.push({right, depth + 1});
        }

        if (seen.size() != expected_primitives)
        {
            out.ok = false;
            out.message = "not all primitives accounted for (" +
                          std::to_string(seen.size()) + " / " +
                          std::to_string(expected_primitives) + ")";
        }
        return out;
    }
} // namespace

TEST(BvhBuilder, EmptyInputProducesEmptyTree)
{
    std::vector<rt::BvhInput> inputs;
    rt::BvhBuildResult result = rt::BuildBvh(inputs, rt::BvhBuildConfig{});
    EXPECT_TRUE(result.nodes.empty());
    EXPECT_TRUE(result.primitive_indices.empty());
    EXPECT_EQ(result.stats.primitive_count, 0u);
}

TEST(BvhBuilder, SinglePrimitiveProducesLeafRoot)
{
    std::vector<rt::BvhInput> inputs;
    inputs.push_back(MakePointAabb(glm::vec3(1.0f, 2.0f, 3.0f), 0.5f, 0));

    rt::BvhBuildResult result = rt::BuildBvh(inputs, rt::BvhBuildConfig{});
    ASSERT_EQ(result.nodes.size(), 1u);
    ASSERT_TRUE(rt::IsLeaf(result.nodes[0]));
    EXPECT_EQ(rt::LeafCount(result.nodes[0]), 1u);
    EXPECT_EQ(result.primitive_indices.size(), 1u);
    EXPECT_EQ(result.primitive_indices[0], 0u);
}

TEST(BvhBuilder, GridScenePreservesAllPrimitives)
{
    // 8x8x8 = 512 tiny boxes on a regular grid.
    std::vector<rt::BvhInput> inputs;
    const uint32_t grid = 8;
    for (uint32_t z = 0; z < grid; ++z)
    {
        for (uint32_t y = 0; y < grid; ++y)
        {
            for (uint32_t x = 0; x < grid; ++x)
            {
                const uint32_t payload = x + y * grid + z * grid * grid;
                inputs.push_back(MakePointAabb(glm::vec3(float(x), float(y), float(z)), 0.25f, payload));
            }
        }
    }

    rt::BvhBuildConfig cfg{};
    rt::BvhBuildResult result = rt::BuildBvh(inputs, cfg);

    auto verdict = VerifyBvh(result, static_cast<uint32_t>(inputs.size()));
    EXPECT_TRUE(verdict.ok) << verdict.message;
    EXPECT_EQ(verdict.leaf_primitive_count, inputs.size());
    EXPECT_LE(verdict.max_depth, cfg.max_depth);
    EXPECT_EQ(result.stats.primitive_count, static_cast<uint32_t>(inputs.size()));
    EXPECT_GT(result.stats.node_count, 0u);
    EXPECT_GT(result.stats.leaf_count, 0u);
}

TEST(BvhBuilder, RandomSceneInvariantsHold)
{
    std::mt19937 rng(12345);
    std::uniform_real_distribution<float> pos_dist(-10.0f, 10.0f);
    std::uniform_real_distribution<float> size_dist(0.05f, 0.25f);

    std::vector<rt::BvhInput> inputs;
    for (uint32_t i = 0; i < 200; ++i)
    {
        glm::vec3 center(pos_dist(rng), pos_dist(rng), pos_dist(rng));
        inputs.push_back(MakePointAabb(center, size_dist(rng), i));
    }

    rt::BvhBuildResult result = rt::BuildBvh(inputs, rt::BvhBuildConfig{});
    auto verdict = VerifyBvh(result, static_cast<uint32_t>(inputs.size()));
    EXPECT_TRUE(verdict.ok) << verdict.message;
    EXPECT_EQ(verdict.leaf_primitive_count, inputs.size());
}

TEST(BvhBuilder, BlasFromMeshPrimitiveCoversEveryTriangle)
{
    // Two-triangle quad.
    hybrid::core::scene::MeshPrimitive primitive;
    primitive.vertices.resize(4);
    primitive.vertices[0].position = glm::vec3(0.0f, 0.0f, 0.0f);
    primitive.vertices[1].position = glm::vec3(1.0f, 0.0f, 0.0f);
    primitive.vertices[2].position = glm::vec3(1.0f, 1.0f, 0.0f);
    primitive.vertices[3].position = glm::vec3(0.0f, 1.0f, 0.0f);
    primitive.indices = {0, 1, 2, 0, 2, 3};

    rt::Blas blas = rt::BuildBlas(primitive, rt::BvhBuildConfig{});
    EXPECT_FALSE(blas.nodes.empty());
    EXPECT_EQ(blas.triangle_indices.size(), 2u);
    EXPECT_EQ(blas.stats.primitive_count, 2u);

    std::unordered_set<uint32_t> tris(blas.triangle_indices.begin(), blas.triangle_indices.end());
    EXPECT_EQ(tris.size(), 2u);
    EXPECT_TRUE(tris.count(0) == 1);
    EXPECT_TRUE(tris.count(1) == 1);
}

TEST(BvhBuilder, TraversalAgreesWithBruteForce)
{
    // Build a 5x5 grid of unit quads on the XY plane, send 100 random rays
    // mostly pointing in -Z, and verify the BVH traversal either hits the
    // same "closest quad" as brute force or both miss.
    constexpr int kGrid = 5;
    std::vector<rt::BvhInput> inputs;
    struct QuadInfo { glm::vec3 bmin; glm::vec3 bmax; };
    std::vector<QuadInfo> quads;

    for (int x = 0; x < kGrid; ++x)
    {
        for (int y = 0; y < kGrid; ++y)
        {
            QuadInfo q{};
            q.bmin = glm::vec3(float(x), float(y), 0.0f);
            q.bmax = glm::vec3(float(x) + 1.0f, float(y) + 1.0f, 0.0f);
            quads.push_back(q);

            rt::BvhInput in{};
            in.bounds.min = q.bmin - glm::vec3(0.0f, 0.0f, 0.001f);
            in.bounds.max = q.bmax + glm::vec3(0.0f, 0.0f, 0.001f);
            in.bounds.valid = true;
            in.centroid = 0.5f * (in.bounds.min + in.bounds.max);
            in.payload_index = static_cast<uint32_t>(quads.size() - 1);
            inputs.push_back(in);
        }
    }

    rt::BvhBuildResult result = rt::BuildBvh(inputs, rt::BvhBuildConfig{});
    ASSERT_FALSE(result.nodes.empty());

    std::mt19937 rng(7);
    std::uniform_real_distribution<float> xy(-1.0f, float(kGrid) + 1.0f);

    for (int i = 0; i < 100; ++i)
    {
        glm::vec3 origin(xy(rng), xy(rng), 5.0f);
        glm::vec3 direction = glm::normalize(glm::vec3(xy(rng) * 0.05f, xy(rng) * 0.05f, -1.0f));
        glm::vec3 inv_dir = 1.0f / direction;

        // Brute-force closest-hit AABB against every quad.
        float brute_t = std::numeric_limits<float>::infinity();
        for (const auto &q : quads)
        {
            float t_near = 0.0f;
            if (rt::IntersectRayAabb(origin, inv_dir, q.bmin - glm::vec3(0, 0, 0.001f), q.bmax + glm::vec3(0, 0, 0.001f),
                                     0.0f, std::numeric_limits<float>::infinity(), t_near))
            {
                brute_t = std::min(brute_t, t_near);
            }
        }

        // BVH traversal — closest-hit AABB.
        float bvh_t = std::numeric_limits<float>::infinity();
        std::stack<uint32_t> stack;
        stack.push(0);
        while (!stack.empty())
        {
            const uint32_t ni = stack.top();
            stack.pop();
            const rt::BvhNode &n = result.nodes[ni];
            float t_node = 0.0f;
            if (!rt::IntersectRayAabb(origin, inv_dir, n.bmin, n.bmax, 0.0f, bvh_t, t_node))
            {
                continue;
            }
            if (rt::IsLeaf(n))
            {
                const uint32_t first = rt::LeafFirst(n);
                const uint32_t count = rt::LeafCount(n);
                for (uint32_t k = 0; k < count; ++k)
                {
                    const QuadInfo &q = quads[result.primitive_indices[first + k]];
                    float t_near = 0.0f;
                    if (rt::IntersectRayAabb(origin, inv_dir, q.bmin - glm::vec3(0, 0, 0.001f),
                                             q.bmax + glm::vec3(0, 0, 0.001f),
                                             0.0f, bvh_t, t_near))
                    {
                        bvh_t = std::min(bvh_t, t_near);
                    }
                }
            }
            else
            {
                stack.push(static_cast<uint32_t>(n.left_or_first));
                stack.push(static_cast<uint32_t>(n.right_or_count));
            }
        }

        if (std::isfinite(brute_t))
        {
            EXPECT_TRUE(std::isfinite(bvh_t)) << "BVH missed a hit brute-force found";
            EXPECT_NEAR(brute_t, bvh_t, 1e-3f);
        }
        else
        {
            EXPECT_FALSE(std::isfinite(bvh_t)) << "BVH hit something brute-force missed";
        }
    }
}

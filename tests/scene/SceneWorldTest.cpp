#include <gtest/gtest.h>

#include "core/scene/SceneWorld.h"

namespace
{
    using hybrid::core::scene::SceneWorld;
    const hybrid::core::scene::TransformComponent &GetTransform(const SceneWorld &world, entt::entity entity)
    {
        const auto *transform = world.TryGetTransform(entity);
        EXPECT_NE(transform, nullptr);
        return *transform;
    }
} // namespace

TEST(SceneWorldTest, UpdatesWorldTransformForRoot)
{
    SceneWorld world;
    auto root = world.CreateEntity("root");

    world.SetLocalTranslation(root, {1.0f, 2.0f, 3.0f});

    world.UpdateTransforms();

    const auto &transform = GetTransform(world, root);
    auto position = glm::vec3(transform.world[3]);
    EXPECT_FLOAT_EQ(position.x, 1.0f);
    EXPECT_FLOAT_EQ(position.y, 2.0f);
    EXPECT_FLOAT_EQ(position.z, 3.0f);
    EXPECT_FALSE(transform.dirty);
}

TEST(SceneWorldTest, PropagatesParentTransformToChild)
{
    SceneWorld world;
    auto parent = world.CreateEntity("parent");
    auto child = world.CreateEntity("child");

    world.SetParent(child, parent);

    world.SetLocalTranslation(parent, {2.0f, 0.0f, 0.0f});
    world.SetLocalTranslation(child, {0.0f, 3.0f, 0.0f});

    world.UpdateTransforms();

    const auto &parent_transform = GetTransform(world, parent);
    const auto &child_transform = GetTransform(world, child);
    auto child_pos = glm::vec3(child_transform.world[3]);
    EXPECT_FLOAT_EQ(child_pos.x, 2.0f);
    EXPECT_FLOAT_EQ(child_pos.y, 3.0f);
    EXPECT_FLOAT_EQ(child_pos.z, 0.0f);
    EXPECT_FALSE(parent_transform.dirty);
    EXPECT_FALSE(child_transform.dirty);
}

TEST(SceneWorldTest, MarksChildDirtyWhenParentChanges)
{
    SceneWorld world;
    auto parent = world.CreateEntity("parent");
    auto child = world.CreateEntity("child");

    world.SetParent(child, parent);

    world.SetLocalTranslation(parent, {1.0f, 0.0f, 0.0f});
    world.SetLocalTranslation(child, {0.0f, 1.0f, 0.0f});
    world.UpdateTransforms();

    world.SetLocalTranslation(parent, {5.0f, 0.0f, 0.0f});

    const auto &parent_transform = GetTransform(world, parent);
    const auto &child_transform = GetTransform(world, child);

    EXPECT_TRUE(parent_transform.dirty);
    EXPECT_TRUE(child_transform.dirty);

    world.UpdateTransforms();

    auto child_pos = glm::vec3(child_transform.world[3]);
    EXPECT_FLOAT_EQ(child_pos.x, 5.0f);
    EXPECT_FLOAT_EQ(child_pos.y, 1.0f);
    EXPECT_FLOAT_EQ(child_pos.z, 0.0f);
}

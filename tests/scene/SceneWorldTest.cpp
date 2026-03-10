#include <gtest/gtest.h>

#include "core/scene/SceneWorld.h"

namespace
{
    using hybrid::core::scene::SceneWorld;
    using hybrid::core::scene::TransformComponent;
    using hybrid::core::scene::Transform;

    TransformComponent &GetTransform(SceneWorld &world, entt::entity entity)
    {
        return world.Registry().get<TransformComponent>(entity);
    }
} // namespace

TEST(SceneWorldTest, UpdatesWorldTransformForRoot)
{
    SceneWorld world;
    auto root = world.CreateEntity("root");

    auto &transform = GetTransform(world, root);
    transform.local.translation = {1.0f, 2.0f, 3.0f};
    transform.dirty = true;

    world.UpdateTransforms();

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

    auto &parent_transform = GetTransform(world, parent);
    auto &child_transform = GetTransform(world, child);

    parent_transform.local.translation = {2.0f, 0.0f, 0.0f};
    child_transform.local.translation = {0.0f, 3.0f, 0.0f};
    parent_transform.dirty = true;
    child_transform.dirty = true;

    world.UpdateTransforms();

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

    auto &parent_transform = GetTransform(world, parent);
    auto &child_transform = GetTransform(world, child);

    parent_transform.local.translation = {1.0f, 0.0f, 0.0f};
    child_transform.local.translation = {0.0f, 1.0f, 0.0f};
    parent_transform.dirty = true;
    child_transform.dirty = true;
    world.UpdateTransforms();

    parent_transform.local.translation = {5.0f, 0.0f, 0.0f};
    world.MarkDirty(parent);

    EXPECT_TRUE(parent_transform.dirty);
    EXPECT_TRUE(child_transform.dirty);

    world.UpdateTransforms();

    auto child_pos = glm::vec3(child_transform.world[3]);
    EXPECT_FLOAT_EQ(child_pos.x, 5.0f);
    EXPECT_FLOAT_EQ(child_pos.y, 1.0f);
    EXPECT_FLOAT_EQ(child_pos.z, 0.0f);
}

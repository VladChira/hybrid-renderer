#include "ui/UiStateUtils.h"

#include "core/scene/SceneWorld.h"
#include "core/scene/types/SceneComponents.h"

#include <unordered_set>
#include <utility>

namespace hybrid::ui
{

    void BuildMaterialEntries(const core::scene::SceneWorld *scene_world, std::vector<UiMaterialEntry> &out_entries)
    {
        out_entries.clear();
        if (!scene_world)
        {
            return;
        }

        const auto &registry = scene_world->Registry();
        const auto mesh_view = registry.view<core::scene::MeshRendererComponent>();
        std::unordered_set<uint64_t> seen_material_ids;

        for (const entt::entity entity : mesh_view)
        {
            (void)entity;
            const auto &mesh_renderer = mesh_view.get<core::scene::MeshRendererComponent>(entity);
            const core::scene::MeshAsset *mesh_asset = mesh_renderer.mesh.Get();
            if (!mesh_asset)
            {
                continue;
            }

            for (const core::scene::MeshPrimitive &primitive : mesh_asset->primitives)
            {
                if (!primitive.material.IsValid())
                {
                    continue;
                }

                const assets::AssetId material_id = primitive.material.Id();
                if (!material_id.IsValid())
                {
                    continue;
                }

                if (!seen_material_ids.insert(material_id.value).second)
                {
                    continue;
                }

                const core::scene::MaterialAsset *material_asset = primitive.material.Get();
                if (!material_asset)
                {
                    continue;
                }

                UiMaterialEntry entry{};
                entry.asset_id = material_id.value;
                entry.name = material_asset->name;
                entry.material = material_asset;
                out_entries.push_back(std::move(entry));
            }
        }
    }

} // namespace hybrid::ui

// Vulkan-mode stub for Renderer.
//
// Phase 2B progress (this file):
//   * Brings up VulkanRenderBackend (instance/device/swapchain/VMA/offscreen).
//   * Drives GeometryStore + AccelerationStructureCache from the submitted
//     SceneWorld each frame, *without* their GL upload paths (Init/Sync/
//     Upload are skipped — those are GL-only). The CPU-side BVH/primitive
//     vectors get mirrored into the heatmap pass's host-visible SSBOs.
//   * TraversalHeatmapVulkanPass writes the false-coloured visit count
//     into the offscreen image, blitted to the swapchain.
//
// Limitations carried into later phases:
//   * BVH SSBOs are HOST_VISIBLE; on every change we wait_idle and re-upload.
//     Upgrade to device-local + staging if/when scene complexity hurts.
//   * UBO data also re-uploaded each frame — same persistently-mapped path
//     as Phase 2A; that's fine.
//
// Excluded from the build in opengl mode; the real
// src/renderer/Renderer.cpp is used instead.

#include "renderer/Renderer.h"
#include "renderer/SceneWorldSnapshot.h"
#include "renderer/passes/TraversalHeatmapVulkanPass.h"
#include "renderer/passes/VulkanGBufferPass.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/stores/MaterialStore.h"
#include "renderer/vulkan/VulkanRenderBackend.h"

#include "core/Log.h"
#include "core/Profiling.h"
#include "core/scene/types/SceneAssets.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstring>
#include <unordered_map>
#include <vector>

namespace hybrid::renderer
{

    namespace
    {
        // CPU mirror of MaterialStore on the Vulkan path. MaterialStore
        // itself stays GL-only — its Init touches GL textures and bindless
        // handles which we don't have here. We translate MaterialAssets
        // straight into GpuMaterial records; texture-handle fields stay 0
        // for now (stage C1 has no textures yet, the shader doesn't sample).
        // The default record at index 0 matches MaterialStore::Init's
        // default.
        struct VulkanMaterialMirror
        {
            std::unordered_map<uint64_t, uint32_t> index_by_asset_id;
            std::vector<GpuMaterial>               records;
        };

        GpuMaterial MakeDefaultGpuMaterial()
        {
            GpuMaterial m{};
            m.base_color_factor    = glm::vec4(1.0f);
            m.emissive_and_cutoff  = glm::vec4(0.0f, 0.0f, 0.0f, 0.5f);
            m.scalar_factors       = glm::vec4(0.0f, 1.0f, 1.0f, 1.0f);
            m.flags_texcoords      = glm::uvec4(0u);
            return m;
        }

        uint32_t ResolveOrAppendMaterial(VulkanMaterialMirror &mirror,
                                          const assets::AssetHandle<core::scene::MaterialAsset> &handle)
        {
            const core::scene::MaterialAsset *material = handle.Get();
            const uint64_t asset_id = handle.Id().value;
            if (material == nullptr || asset_id == 0)
            {
                return kDefaultMaterialIndex;
            }
            if (auto it = mirror.index_by_asset_id.find(asset_id);
                it != mirror.index_by_asset_id.end())
            {
                return it->second;
            }

            GpuMaterial r{};
            r.base_color_factor    = material->base_color_factor;
            r.emissive_and_cutoff  = glm::vec4(material->emissive_factor, material->alpha_cutoff);
            r.scalar_factors       = glm::vec4(material->metallic_factor,
                                                material->roughness_factor,
                                                material->normal_scale,
                                                material->occlusion_strength);

            uint32_t alpha_mode = 0u;
            switch (material->alpha_mode)
            {
            case core::scene::AlphaMode::Opaque: alpha_mode = 0u; break;
            case core::scene::AlphaMode::Mask:   alpha_mode = 1u; break;
            case core::scene::AlphaMode::Blend:  alpha_mode = 2u; break;
            }
            r.flags_texcoords = glm::uvec4(alpha_mode, 0u, 0u, 0u);
            // texture-handle fields stay zero — stage C3 wires them up.

            const uint32_t index = static_cast<uint32_t>(mirror.records.size());
            mirror.records.push_back(r);
            mirror.index_by_asset_id.emplace(asset_id, index);
            return index;
        }

        void ImageBarrier(VkCommandBuffer cmd,
                          VkImage image,
                          VkImageLayout from, VkImageLayout to,
                          VkAccessFlags src_access, VkAccessFlags dst_access,
                          VkPipelineStageFlags src_stage,
                          VkPipelineStageFlags dst_stage)
        {
            VkImageMemoryBarrier b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            b.oldLayout = from;
            b.newLayout = to;
            b.srcAccessMask = src_access;
            b.dstAccessMask = dst_access;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = image;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.levelCount = 1;
            b.subresourceRange.layerCount = 1;
            vkCmdPipelineBarrier(cmd, src_stage, dst_stage, 0,
                                 0, nullptr, 0, nullptr, 1, &b);
        }
    } // namespace

    struct Renderer::Impl
    {
        bool initialized = false;
        RendererStats stats{};
        RendererOutputs outputs{};
        raytracing::AccelerationStructureStats as_stats{};

        VulkanRenderBackend backend{};
        bool frame_active = false;
        float current_time = 0.0f;

        Renderer::UiRenderHook ui_render_hook{};

        TraversalHeatmapVulkanPass heatmap_pass{};
        VulkanGBufferPass gbuffer_pass{};
        VulkanMaterialMirror materials{};

        // Cached byte counts for the gbuffer pass's host-visible buffers.
        // Wait_idle + reupload happens whenever any of these changes.
        size_t last_vertices_bytes  = 0;
        size_t last_indices_bytes   = 0;
        size_t last_materials_bytes = 0;
        std::vector<VulkanGBufferPass::DrawCall> draw_scratch{};

        // Scene plumbing — CPU-side only on the Vulkan path. We never call
        // geometry_store.Init/Sync or as_cache.Init/Upload (those touch GL).
        SceneFrameCache scene_frame_cache{};
        GeometryStore geometry_store{};
        raytracing::AccelerationStructureCache as_cache{};

        core::scene::SceneWorld *submitted_scene_world = nullptr;
        RenderView submitted_view{};
        RenderSettings submitted_settings{};
        bool view_submitted = false;

        // Lifetime sizes of the heatmap pass's SSBOs, used to detect when we
        // have to wait_idle before UpdateSsbos reallocates a buffer.
        size_t last_primitives_bytes      = 0;
        size_t last_blas_nodes_bytes      = 0;
        size_t last_tlas_nodes_bytes      = 0;
        size_t last_tlas_instances_bytes  = 0;
    };

    namespace
    {
        // Walks opaque + masked instances and ensures each primitive is in
        // the geometry store (with the correct material_index cached on its
        // GpuPrimitive descriptor) and each material is in the Vulkan-side
        // mirror.
        void PopulateGeometryStore(GeometryStore &store,
                                    VulkanMaterialMirror &materials,
                                    const FrameSceneData &scene)
        {
            auto eat = [&](const std::vector<RenderMeshInstance> &batch)
            {
                for (const RenderMeshInstance &instance : batch)
                {
                    const core::scene::MeshAsset *mesh = instance.mesh.Get();
                    if (mesh == nullptr) continue;

                    for (size_t pi = 0; pi < mesh->primitives.size(); ++pi)
                    {
                        const core::scene::MeshPrimitive &primitive = mesh->primitives[pi];
                        const uint32_t material_index = ResolveOrAppendMaterial(materials, primitive.material);

                        PrimitiveHandle handle{};
                        bool appended = false;
                        store.GetOrAppend(instance.mesh.Id().value,
                                          static_cast<uint32_t>(pi),
                                          primitive,
                                          material_index,
                                          handle,
                                          appended);
                        // GeometryStore only writes material_index when the
                        // primitive is first appended. Patch on later runs
                        // so material reassignment between frames isn't
                        // silently ignored.
                        if (!appended && handle.IsValid())
                        {
                            store.SetPrimitiveMaterial(handle.primitive_id, material_index);
                        }
                    }
                }
            };
            eat(scene.opaque_mesh_instances);
            eat(scene.masked_mesh_instances);
        }

    } // namespace

    Renderer::Renderer() : m_impl(std::make_unique<Impl>()) {}
    Renderer::~Renderer() = default;

    bool Renderer::Init(platform::NativeWindowHandle window)
    {
        auto *glfw_window = static_cast<GLFWwindow *>(window.window);
        if (!glfw_window)
        {
            LOG_ERROR("[renderer/vulkan] Init missing window handle");
            return false;
        }

        int w = 0, h = 0;
        glfwGetFramebufferSize(glfw_window, &w, &h);
        if (w <= 0 || h <= 0)
        {
            LOG_ERROR("[renderer/vulkan] window framebuffer size is zero");
            return false;
        }

        if (!m_impl->backend.Init(glfw_window,
                                   static_cast<uint32_t>(w),
                                   static_cast<uint32_t>(h)))
        {
            LOG_ERROR("[renderer/vulkan] backend Init failed");
            return false;
        }

        if (!m_impl->heatmap_pass.Init(m_impl->backend.Device().Logical(),
                                        m_impl->backend.Allocator()))
        {
            LOG_ERROR("[renderer/vulkan] TraversalHeatmapVulkanPass::Init failed");
            m_impl->heatmap_pass.Shutdown();
            m_impl->backend.Shutdown();
            return false;
        }
        m_impl->heatmap_pass.SetOutputImageView(m_impl->backend.OffscreenImageView());

        const std::vector<VkFormat> gbuffer_color_formats = {
            m_impl->backend.OffscreenFormat(),
            m_impl->backend.Rt1Format(),
        };
        // Seed the material mirror with the neutral default at index 0 so
        // primitives without a valid MaterialAsset still resolve to a
        // safe record (matches MaterialStore::Init's convention).
        m_impl->materials.records.push_back(MakeDefaultGpuMaterial());

        if (!m_impl->gbuffer_pass.Init(m_impl->backend.Device().Logical(),
                                        m_impl->backend.Allocator(),
                                        gbuffer_color_formats,
                                        m_impl->backend.DepthFormat()))
        {
            LOG_ERROR("[renderer/vulkan] VulkanGBufferPass::Init failed");
            m_impl->gbuffer_pass.Shutdown();
            m_impl->heatmap_pass.Shutdown();
            m_impl->backend.Shutdown();
            return false;
        }

        LOG_INFO("[renderer/vulkan] Phase 3-stage-A: gbuffer raster -> ImGui sample -> present");
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized) return;
        m_impl->backend.Device().WaitIdle();
        m_impl->gbuffer_pass.Shutdown();
        m_impl->heatmap_pass.Shutdown();
        m_impl->backend.Shutdown();
        m_impl->initialized = false;
    }

    void Renderer::Resize(const RenderExtent & /*extent*/) {}

    bool Renderer::BeginFrame(const FrameContext &frame)
    {
        if (!m_impl->initialized) return false;
        m_impl->current_time = static_cast<float>(frame.time_seconds);
        m_impl->submitted_scene_world = nullptr;
        m_impl->view_submitted = false;
        m_impl->frame_active = m_impl->backend.BeginFrame();
        return m_impl->frame_active;
    }

    void Renderer::SubmitScene(core::scene::SceneWorld &scene_world,
                                const RenderView &view,
                                const RenderSettings &settings)
    {
        if (!m_impl->initialized) return;
        m_impl->submitted_scene_world = &scene_world;
        m_impl->submitted_view = view;
        m_impl->submitted_settings = settings;
        m_impl->view_submitted = true;
    }

    RendererOutputs Renderer::EndFrame()
    {
        HYBRID_PROFILE_ZONE_N("Renderer::EndFrame");
        if (!m_impl->frame_active)
        {
            return m_impl->outputs;
        }

        // -- Drive the CPU side of the BVH / primitive store ----------------
        FrameSceneData scene_data{};
        {
            HYBRID_PROFILE_ZONE_N("Renderer::BuildFrameSceneData");
            if (m_impl->submitted_scene_world != nullptr)
            {
                m_impl->scene_frame_cache.Sync(*m_impl->submitted_scene_world);
                scene_data = m_impl->scene_frame_cache.GetFrameData();
            }
            else
            {
                m_impl->scene_frame_cache.Reset();
            }
        }

        {
            HYBRID_PROFILE_ZONE_N("Renderer::PopulateGeometryStore");
            PopulateGeometryStore(m_impl->geometry_store, m_impl->materials, scene_data);
        }
        {
            HYBRID_PROFILE_ZONE_N("Renderer::AccelerationStructureSync");
            m_impl->as_cache.SyncBlas(scene_data, m_impl->geometry_store);
            m_impl->as_cache.SyncTlas(scene_data, m_impl->geometry_store);
            m_impl->as_stats = m_impl->as_cache.Stats();
        }

        const auto &vertices   = m_impl->geometry_store.Vertices();
        const auto &indices    = m_impl->geometry_store.Indices();
        const auto &primitives = m_impl->geometry_store.Primitives();

        // -- Mirror geometry + materials into the gbuffer pass's buffers ---
        const auto &material_records = m_impl->materials.records;
        const size_t vbytes = vertices.size()         * sizeof(vertices[0]);
        const size_t ibytes = indices.size()          * sizeof(indices[0]);
        const size_t mbytes = material_records.size() * sizeof(material_records[0]);
        const bool size_changed =
            vbytes != m_impl->last_vertices_bytes ||
            ibytes != m_impl->last_indices_bytes  ||
            mbytes != m_impl->last_materials_bytes;
        if (size_changed)
        {
            m_impl->backend.Device().WaitIdle();
            m_impl->last_vertices_bytes  = vbytes;
            m_impl->last_indices_bytes   = ibytes;
            m_impl->last_materials_bytes = mbytes;
        }
        if (vbytes > 0 && ibytes > 0)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::UpdateGeometry");
            VulkanGBufferPass::GeometryUpload geo{};
            geo.vertices       = vertices.data();
            geo.vertices_bytes = vbytes;
            geo.indices        = indices.data();
            geo.indices_bytes  = ibytes;
            m_impl->gbuffer_pass.UpdateGeometry(geo);
        }
        if (mbytes > 0)
        {
            HYBRID_PROFILE_ZONE_N("Renderer::UpdateMaterials");
            VulkanGBufferPass::MaterialUpload mats{};
            mats.materials       = material_records.data();
            mats.materials_bytes = mbytes;
            m_impl->gbuffer_pass.UpdateMaterials(mats);
        }

        // -- Build per-primitive draw calls from the submitted instances ----
        HYBRID_PROFILE_ZONE_N("Renderer::BuildDrawList");
        m_impl->draw_scratch.clear();
        auto emit_batch = [&](const std::vector<RenderMeshInstance> &batch)
        {
            for (const RenderMeshInstance &instance : batch)
            {
                const core::scene::MeshAsset *mesh = instance.mesh.Get();
                if (mesh == nullptr) continue;
                for (size_t pi = 0; pi < mesh->primitives.size(); ++pi)
                {
                    uint32_t primitive_id = 0;
                    if (!m_impl->geometry_store.FindPrimitiveId(
                            instance.mesh.Id().value,
                            static_cast<uint32_t>(pi),
                            primitive_id))
                    {
                        continue;
                    }
                    const GpuPrimitive &gp = primitives[primitive_id];
                    if (gp.index_count == 0) continue;

                    VulkanGBufferPass::DrawCall draw{};
                    draw.model          = instance.world_from_local;
                    draw.first_index    = gp.index_offset;
                    draw.index_count    = gp.index_count;
                    draw.vertex_offset  = static_cast<int32_t>(gp.vertex_offset);
                    draw.material_index = gp.material_index;
                    m_impl->draw_scratch.push_back(draw);
                }
            }
        };
        emit_batch(scene_data.opaque_mesh_instances);
        emit_batch(scene_data.masked_mesh_instances);

        HYBRID_PROFILE_ZONE_N("Renderer::RecordCommands");

        VkCommandBuffer cmd       = m_impl->backend.CurrentCommandBuffer();
        VkImage         offscreen = m_impl->backend.OffscreenImage();
        VkImage         rt1       = m_impl->backend.Rt1Image();
        VkImage         depth     = m_impl->backend.DepthImage();
        VkImage         swap      = m_impl->backend.CurrentSwapchainImage();
        VkExtent2D      extent    = m_impl->backend.OffscreenExtent();

        // 1) RT0 + RT1 UNDEFINED -> COLOR_ATTACHMENT, depth UNDEFINED ->
        //    DEPTH_ATTACHMENT for the gbuffer raster scope.
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);
        ImageBarrier(cmd, rt1,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        VkImageMemoryBarrier depth_barrier{};
        depth_barrier.sType                       = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        depth_barrier.oldLayout                   = VK_IMAGE_LAYOUT_UNDEFINED;
        depth_barrier.newLayout                   = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_barrier.srcAccessMask               = 0;
        depth_barrier.dstAccessMask               = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                                                     VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
        depth_barrier.srcQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        depth_barrier.dstQueueFamilyIndex         = VK_QUEUE_FAMILY_IGNORED;
        depth_barrier.image                       = depth;
        depth_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        depth_barrier.subresourceRange.levelCount = 1;
        depth_barrier.subresourceRange.layerCount = 1;
        vkCmdPipelineBarrier(cmd,
                             VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
                             0, 0, nullptr, 0, nullptr, 1, &depth_barrier);

        // 2) gbuffer raster — clears + draws every visible primitive.
        const RenderView view_to_use = m_impl->view_submitted
            ? m_impl->submitted_view : RenderView{};
        VulkanGBufferPass::FrameParams params{};
        params.view       = view_to_use.view;
        params.projection = view_to_use.projection;
        const std::vector<VkImageView> color_views = {
            m_impl->backend.OffscreenImageView(),
            m_impl->backend.Rt1ImageView(),
        };
        m_impl->gbuffer_pass.Execute(cmd,
                                      m_impl->backend.FrameIndexInFlight(),
                                      extent,
                                      color_views,
                                      m_impl->backend.DepthImageView(),
                                      params,
                                      m_impl->draw_scratch);

        // 3) RT0 + RT1 COLOR_ATTACHMENT -> SHADER_READ (ImGui samples them
        //    via ViewportPanel) and swapchain UNDEFINED -> COLOR_ATTACHMENT
        //    for the ImGui render scope below.
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        ImageBarrier(cmd, rt1,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
        ImageBarrier(cmd, swap,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                     0, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT);

        // 4) Open a dynamic-rendering scope on the swapchain image. ImGui's
        //    dockspace covers the viewport so loadOp=CLEAR is fine — clear
        //    color is only visible at panel gaps.
        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView   = m_impl->backend.CurrentSwapchainImageView();
        color_attach.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attach.storeOp     = VK_ATTACHMENT_STORE_OP_STORE;
        color_attach.clearValue.color = {{0.05f, 0.05f, 0.05f, 1.0f}};

        VkRenderingInfo rendering{};
        rendering.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        rendering.renderArea.offset    = {0, 0};
        rendering.renderArea.extent    = extent;
        rendering.layerCount           = 1;
        rendering.colorAttachmentCount = 1;
        rendering.pColorAttachments    = &color_attach;

        vkCmdBeginRendering(cmd, &rendering);
        if (m_impl->ui_render_hook)
        {
            m_impl->ui_render_hook(cmd);
        }
        vkCmdEndRendering(cmd);

        // 5) swapchain COLOR_ATTACHMENT -> PRESENT
        ImageBarrier(cmd, swap,
                     VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, 0,
                     VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
                     VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);

        m_impl->backend.EndFrame();
        m_impl->frame_active = false;
        return m_impl->outputs;
    }

    const RendererStats &Renderer::GetStats() const { return m_impl->stats; }

    const raytracing::AccelerationStructureStats *Renderer::GetAccelerationStructureStats() const
    {
        return &m_impl->as_stats;
    }

    void Renderer::SetUiRenderHook(UiRenderHook hook)
    {
        m_impl->ui_render_hook = std::move(hook);
    }

    VulkanRenderBackend *Renderer::GetVulkanRenderBackend()
    {
        return &m_impl->backend;
    }

} // namespace hybrid::renderer

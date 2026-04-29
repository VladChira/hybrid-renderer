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
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/stores/GeometryStore.h"
#include "renderer/vulkan/VulkanRenderBackend.h"

#include "core/Log.h"
#include "core/scene/types/SceneAssets.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_inverse.hpp>

#include <cstring>

namespace hybrid::renderer
{

    namespace
    {
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
        VkExtent2D last_descriptor_extent{0, 0};

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
        // Mirror of GBufferPass's prepare-instance loop (GL path), minus the
        // material upload — we only need geometry_store populated so the AS
        // cache has stable primitive ids to address. Walks opaque + masked
        // buckets only; transparent doesn't participate in the heatmap.
        void PopulateGeometryStore(GeometryStore &store, const FrameSceneData &scene)
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
                        PrimitiveHandle handle{};
                        bool appended = false;
                        // material_index = 0: heatmap doesn't read materials.
                        store.GetOrAppend(instance.mesh.Id().value,
                                          static_cast<uint32_t>(pi),
                                          primitive,
                                          /*material_index=*/0u,
                                          handle,
                                          appended);
                    }
                }
            };
            eat(scene.opaque_mesh_instances);
            eat(scene.masked_mesh_instances);
        }

        TraversalHeatmapVulkanPass::FrameParams ComputeFrameParams(
            const RenderView &view, VkExtent2D extent, uint32_t tlas_node_count)
        {
            TraversalHeatmapVulkanPass::FrameParams p{};
            p.inv_view        = glm::affineInverse(view.view);
            p.inv_projection  = glm::inverse(view.projection);
            p.camera_position = view.position;
            p.output_size     = glm::uvec2(extent.width, extent.height);
            p.heatmap_scale   = 64.0f;
            p.tlas_node_count = tlas_node_count;
            return p;
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
        m_impl->last_descriptor_extent = m_impl->backend.OffscreenExtent();

        LOG_INFO("[renderer/vulkan] Phase 2B: scene-driven BVH heatmap -> blit -> present");
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized) return;
        m_impl->backend.Device().WaitIdle();
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
        if (!m_impl->frame_active)
        {
            return m_impl->outputs;
        }

        // Resize hand-off: re-point the storage-image binding when the
        // backend rebuilt the offscreen image.
        VkExtent2D current_extent = m_impl->backend.OffscreenExtent();
        if (current_extent.width  != m_impl->last_descriptor_extent.width ||
            current_extent.height != m_impl->last_descriptor_extent.height)
        {
            m_impl->heatmap_pass.SetOutputImageView(m_impl->backend.OffscreenImageView());
            m_impl->last_descriptor_extent = current_extent;
        }

        // -- Drive the CPU side of the BVH / primitive store ----------------
        FrameSceneData scene_data{};
        if (m_impl->submitted_scene_world != nullptr)
        {
            m_impl->scene_frame_cache.Sync(*m_impl->submitted_scene_world);
            scene_data = m_impl->scene_frame_cache.GetFrameData();
        }
        else
        {
            m_impl->scene_frame_cache.Reset();
        }

        PopulateGeometryStore(m_impl->geometry_store, scene_data);
        m_impl->as_cache.SyncBlas(scene_data, m_impl->geometry_store);
        m_impl->as_cache.SyncTlas(scene_data, m_impl->geometry_store);
        m_impl->as_stats = m_impl->as_cache.Stats();

        // -- Mirror CPU vectors into the pass's SSBOs -----------------------
        const auto &primitives    = m_impl->geometry_store.Primitives();
        const auto &blas_nodes    = m_impl->as_cache.BlasNodes();
        const auto &tlas_nodes    = m_impl->as_cache.TlasNodes();
        const auto &tlas_inst     = m_impl->as_cache.TlasInstances();

        TraversalHeatmapVulkanPass::SsboData ssbo{};
        ssbo.primitives           = primitives.data();
        ssbo.primitives_bytes     = primitives.size()    * sizeof(primitives[0]);
        ssbo.blas_nodes           = blas_nodes.data();
        ssbo.blas_nodes_bytes     = blas_nodes.size()    * sizeof(blas_nodes[0]);
        ssbo.tlas_nodes           = tlas_nodes.data();
        ssbo.tlas_nodes_bytes     = tlas_nodes.size()    * sizeof(tlas_nodes[0]);
        ssbo.tlas_instances       = tlas_inst.data();
        ssbo.tlas_instances_bytes = tlas_inst.size()     * sizeof(tlas_inst[0]);

        // Wait idle before UpdateSsbos when any size grew or the buffers are
        // about to be reallocated. Cheap fix; the pass would otherwise race
        // an in-flight read of a stale buffer. Track sizes externally so we
        // only stall on actual change.
        const bool size_changed =
            ssbo.primitives_bytes     != m_impl->last_primitives_bytes ||
            ssbo.blas_nodes_bytes     != m_impl->last_blas_nodes_bytes ||
            ssbo.tlas_nodes_bytes     != m_impl->last_tlas_nodes_bytes ||
            ssbo.tlas_instances_bytes != m_impl->last_tlas_instances_bytes;
        if (size_changed)
        {
            m_impl->backend.Device().WaitIdle();
            m_impl->last_primitives_bytes     = ssbo.primitives_bytes;
            m_impl->last_blas_nodes_bytes     = ssbo.blas_nodes_bytes;
            m_impl->last_tlas_nodes_bytes     = ssbo.tlas_nodes_bytes;
            m_impl->last_tlas_instances_bytes = ssbo.tlas_instances_bytes;
        }
        // Skip the upload entirely when there's nothing yet — the pass keeps
        // its placeholder SSBOs and the shader's tlas_node_count==0 path
        // short-circuits.
        if (ssbo.primitives_bytes > 0 &&
            ssbo.blas_nodes_bytes > 0 &&
            ssbo.tlas_nodes_bytes > 0 &&
            ssbo.tlas_instances_bytes > 0)
        {
            m_impl->heatmap_pass.UpdateSsbos(ssbo);
        }

        VkCommandBuffer cmd       = m_impl->backend.CurrentCommandBuffer();
        VkImage         offscreen = m_impl->backend.OffscreenImage();
        VkImage         swap      = m_impl->backend.CurrentSwapchainImage();
        VkExtent2D      extent    = m_impl->backend.SwapchainExtent();

        // 1) offscreen UNDEFINED -> GENERAL for compute write
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     0, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        // 2) heatmap dispatch — uses real view if SubmitScene happened, else
        //    a degenerate identity view that produces an all-cool image.
        const RenderView view_to_use = m_impl->view_submitted
            ? m_impl->submitted_view : RenderView{};
        const auto params = ComputeFrameParams(
            view_to_use, extent, m_impl->as_stats.tlas_nodes);
        m_impl->heatmap_pass.Execute(cmd,
                                      m_impl->backend.FrameIndexInFlight(),
                                      extent,
                                      params);

        // 3) offscreen GENERAL -> SHADER_READ_ONLY (ImGui samples it via the
        //    ViewportPanel) and swapchain UNDEFINED -> COLOR_ATTACHMENT for
        //    the dynamic-rendering scope below.
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
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

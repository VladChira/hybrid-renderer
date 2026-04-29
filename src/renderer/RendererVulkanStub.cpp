// Vulkan-mode stub for Renderer.
//
// Phase 2 progress (this file):
//   * Brings up VulkanRenderBackend (instance/device/swapchain/VMA/offscreen).
//   * Drives TraversalHeatmapVulkanPass each frame with a hand-fabricated
//     1-instance / 1-BLAS / 1-triangle-leaf BVH and an orbit camera, so we
//     get a recognizable heatmap silhouette of the synthetic AABB.
//   * Blits the offscreen image onto the swapchain.
//
// Real scene plumbing (driving GeometryStore + AccelerationStructureCache
// from SceneWorld in SubmitScene) is the next session's work.
//
// Excluded from the build in opengl mode; the real
// src/renderer/Renderer.cpp is used instead.

#include "renderer/Renderer.h"
#include "renderer/passes/TraversalHeatmapVulkanPass.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/vulkan/VulkanRenderBackend.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <array>
#include <cmath>
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

        // ---- Synthetic BVH data --------------------------------------------
        // Phase 2 scaffolding: one instance pointing at one BLAS, whose root
        // is a leaf reporting one triangle's worth of cost. The heatmap
        // shader doesn't actually intersect triangles — it just counts node
        // visits — so we don't need real geometry, only the BVH skeleton.
        // Layouts match shaders/include/common.glsl (BvhNode, GpuPrimitive,
        // GpuTlasInstance).

        struct SynthBvhNode
        {
            glm::vec3 bmin;
            int32_t   left_or_first;
            glm::vec3 bmax;
            int32_t   right_or_count;
        };
        static_assert(sizeof(SynthBvhNode) == 32, "BvhNode std430 = 32 bytes");

        struct SynthGpuPrimitive
        {
            uint32_t vertex_offset;
            uint32_t vertex_count;
            uint32_t index_offset;
            uint32_t index_count;
            uint32_t material_index;
            uint32_t blas_root;
            uint32_t blas_triangle_offset;
            uint32_t _pad;
        };
        static_assert(sizeof(SynthGpuPrimitive) == 32,
                      "GpuPrimitive std430 = 32 bytes");

        struct SynthGpuTlasInstance
        {
            glm::mat4 world_from_local;
            glm::mat4 local_from_world;
            uint32_t  primitive_id;
            uint32_t  entity_id;
            uint32_t  _pad0;
            uint32_t  _pad1;
        };
        static_assert(sizeof(SynthGpuTlasInstance) == 144,
                      "GpuTlasInstance std430 = 144 bytes");

        struct SyntheticBvh
        {
            std::array<SynthGpuPrimitive,    1> primitives;
            std::array<SynthBvhNode,         1> blas_nodes;
            std::array<SynthBvhNode,         1> tlas_nodes;
            std::array<SynthGpuTlasInstance, 1> tlas_instances;
        };

        SyntheticBvh BuildSyntheticBvh()
        {
            SyntheticBvh b{};
            b.primitives[0] = SynthGpuPrimitive{
                /*vertex_offset=*/0, /*vertex_count=*/0,
                /*index_offset=*/0,  /*index_count=*/3,
                /*material_index=*/0,
                /*blas_root=*/0,
                /*blas_triangle_offset=*/0,
                /*_pad=*/0,
            };
            b.blas_nodes[0] = SynthBvhNode{
                /*bmin=*/glm::vec3(-1.0f), /*left_or_first=*/0,
                /*bmax=*/glm::vec3( 1.0f), /*right_or_count=*/-1, // leaf, 1 tri
            };
            b.tlas_nodes[0] = SynthBvhNode{
                /*bmin=*/glm::vec3(-1.0f), /*left_or_first=*/0,
                /*bmax=*/glm::vec3( 1.0f), /*right_or_count=*/-1, // leaf, 1 inst
            };
            b.tlas_instances[0] = SynthGpuTlasInstance{
                /*world_from_local=*/glm::mat4(1.0f),
                /*local_from_world=*/glm::mat4(1.0f),
                /*primitive_id=*/0,
                /*entity_id=*/0,
                /*_pad0=*/0, /*_pad1=*/0,
            };
            return b;
        }

        TraversalHeatmapVulkanPass::FrameParams ComputeFrameParams(
            float time_seconds, VkExtent2D extent, uint32_t tlas_node_count)
        {
            TraversalHeatmapVulkanPass::FrameParams p{};
            const float t = time_seconds * 0.5f;
            const glm::vec3 cam_pos(3.0f * std::cos(t), 1.5f, 3.0f * std::sin(t));
            const glm::mat4 view = glm::lookAt(cam_pos,
                                                glm::vec3(0.0f),
                                                glm::vec3(0.0f, 1.0f, 0.0f));
            const float aspect = extent.height > 0
                ? static_cast<float>(extent.width) / static_cast<float>(extent.height)
                : 1.0f;
            const glm::mat4 proj = glm::perspective(
                glm::radians(60.0f), aspect, 0.1f, 100.0f);

            p.inv_view        = glm::inverse(view);
            p.inv_projection  = glm::inverse(proj);
            p.camera_position = cam_pos;
            p.output_size     = glm::uvec2(extent.width, extent.height);
            p.heatmap_scale   = 4.0f;     // small: tiny BVH, single-digit visits
            p.tlas_node_count = tlas_node_count;
            return p;
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

        TraversalHeatmapVulkanPass heatmap_pass{};
        SyntheticBvh synthetic_bvh{};
        VkExtent2D last_descriptor_extent{0, 0};
    };

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

        m_impl->synthetic_bvh = BuildSyntheticBvh();
        TraversalHeatmapVulkanPass::SsboData ssbo{};
        ssbo.primitives           = m_impl->synthetic_bvh.primitives.data();
        ssbo.primitives_bytes     = sizeof(m_impl->synthetic_bvh.primitives);
        ssbo.blas_nodes           = m_impl->synthetic_bvh.blas_nodes.data();
        ssbo.blas_nodes_bytes     = sizeof(m_impl->synthetic_bvh.blas_nodes);
        ssbo.tlas_nodes           = m_impl->synthetic_bvh.tlas_nodes.data();
        ssbo.tlas_nodes_bytes     = sizeof(m_impl->synthetic_bvh.tlas_nodes);
        ssbo.tlas_instances       = m_impl->synthetic_bvh.tlas_instances.data();
        ssbo.tlas_instances_bytes = sizeof(m_impl->synthetic_bvh.tlas_instances);

        if (!m_impl->heatmap_pass.Init(m_impl->backend.Device().Logical(),
                                        m_impl->backend.Allocator(),
                                        ssbo))
        {
            LOG_ERROR("[renderer/vulkan] TraversalHeatmapVulkanPass::Init failed");
            m_impl->heatmap_pass.Shutdown();
            m_impl->backend.Shutdown();
            return false;
        }
        m_impl->heatmap_pass.SetOutputImageView(m_impl->backend.OffscreenImageView());
        m_impl->last_descriptor_extent = m_impl->backend.OffscreenExtent();

        LOG_INFO("[renderer/vulkan] Phase 2: synthetic-BVH heatmap -> blit -> present");
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
        m_impl->frame_active = m_impl->backend.BeginFrame();
        return m_impl->frame_active;
    }

    void Renderer::SubmitScene(core::scene::SceneWorld & /*scene_world*/,
                                const RenderView & /*view*/,
                                const RenderSettings & /*settings*/) {}

    RendererOutputs Renderer::EndFrame()
    {
        if (!m_impl->frame_active)
        {
            return m_impl->outputs;
        }

        // The backend recreates the offscreen image on resize. When the
        // extent changes, re-point the pass's descriptor sets at the new
        // view (the resize path already waits idle, so it's safe to write
        // descriptors that other frames might be referencing).
        VkExtent2D current_extent = m_impl->backend.OffscreenExtent();
        if (current_extent.width  != m_impl->last_descriptor_extent.width ||
            current_extent.height != m_impl->last_descriptor_extent.height)
        {
            m_impl->heatmap_pass.SetOutputImageView(m_impl->backend.OffscreenImageView());
            m_impl->last_descriptor_extent = current_extent;
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

        // 2) heatmap dispatch
        const auto params = ComputeFrameParams(
            m_impl->current_time, extent,
            static_cast<uint32_t>(m_impl->synthetic_bvh.tlas_nodes.size()));
        m_impl->heatmap_pass.Execute(cmd,
                                      m_impl->backend.FrameIndexInFlight(),
                                      extent,
                                      params);

        // 3) offscreen GENERAL -> TRANSFER_SRC and swapchain UNDEFINED -> TRANSFER_DST
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_GENERAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_ACCESS_SHADER_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);
        ImageBarrier(cmd, swap,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     0, VK_ACCESS_TRANSFER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_TRANSFER_BIT);

        // 4) blit offscreen -> swapchain (handles SRGB encoding via blit)
        VkImageBlit blit{};
        blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.srcSubresource.layerCount = 1;
        blit.srcOffsets[1] = {static_cast<int32_t>(extent.width),
                               static_cast<int32_t>(extent.height), 1};
        blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        blit.dstSubresource.layerCount = 1;
        blit.dstOffsets[1] = blit.srcOffsets[1];
        vkCmdBlitImage(cmd,
                       offscreen, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       swap,      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1, &blit,
                       VK_FILTER_NEAREST);

        // 5) swapchain TRANSFER_DST -> PRESENT
        ImageBarrier(cmd, swap,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                     VK_ACCESS_TRANSFER_WRITE_BIT, 0,
                     VK_PIPELINE_STAGE_TRANSFER_BIT,
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

} // namespace hybrid::renderer

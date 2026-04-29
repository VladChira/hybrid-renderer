#pragma once

#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCommon.h"

#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <vector>

#include <glm/glm.hpp>

namespace hybrid::renderer
{

    // Phase 3-stage-A GBuffer raster pass — Vulkan-side. One color
    // attachment (the renderer's existing offscreen image) + depth, no
    // materials. Output is normal-as-color; the helmet's silhouette and
    // shading should be visible end-to-end after this pass runs.
    //
    // Caller responsibilities:
    //   * Transition color UNDEFINED -> COLOR_ATTACHMENT_OPTIMAL and depth
    //     UNDEFINED -> DEPTH_ATTACHMENT_OPTIMAL before Execute.
    //   * Transition color -> SHADER_READ_ONLY_OPTIMAL after Execute (for
    //     ImGui sampling in the ViewportPanel).
    //   * Provide draw calls — pass doesn't walk the scene itself.
    class VulkanGBufferPass
    {
    public:
        static constexpr uint32_t kMaxFramesInFlight = 2;

        // Per-frame view + projection (Vulkan-flavoured: depth [0, 1],
        // glm::perspective with GLM_FORCE_DEPTH_ZERO_TO_ONE). Y is flipped
        // by a negative-height viewport in the pipeline.
        struct FrameParams
        {
            glm::mat4 view{1.0f};
            glm::mat4 projection{1.0f};
        };

        struct DrawCall
        {
            glm::mat4 model{1.0f};
            uint32_t  first_index   = 0;
            uint32_t  index_count   = 0;
            int32_t   vertex_offset = 0;
        };

        struct GeometryUpload
        {
            const void  *vertices       = nullptr;
            VkDeviceSize vertices_bytes = 0;
            const void  *indices        = nullptr;
            VkDeviceSize indices_bytes  = 0;
        };

        bool Init(VkDevice device,
                  VmaAllocator allocator,
                  VkFormat color_format,
                  VkFormat depth_format);
        void Shutdown();

        // Mirror the renderer's CPU-side vertex / index vectors into the
        // pass's host-visible buffers. Reallocates on size change; caller
        // must wait idle when shape changes (same pattern as the heatmap
        // pass's UpdateSsbos).
        void UpdateGeometry(const GeometryUpload &geo);

        // Records the dynamic-rendering scope, viewport/scissor, vertex/
        // index bindings, and one vkCmdDrawIndexed per DrawCall.
        void Execute(VkCommandBuffer cmd,
                     uint32_t frame_index,
                     VkExtent2D extent,
                     VkImageView color_view,
                     VkImageView depth_view,
                     const FrameParams &params,
                     const std::vector<DrawCall> &draws);

    private:
        bool CreatePipeline(VkFormat color_format, VkFormat depth_format);
        bool CreatePerFrameUniforms();
        bool CreateDescriptorPool();
        bool AllocateDescriptorSets();
        void WriteUboDescriptors();

        VkDevice     m_device    = VK_NULL_HANDLE;
        VmaAllocator m_allocator = VK_NULL_HANDLE;

        VkShaderModule        m_vert_module     = VK_NULL_HANDLE;
        VkShaderModule        m_frag_module     = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_set_layout      = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipeline_layout = VK_NULL_HANDLE;
        VkPipeline            m_pipeline        = VK_NULL_HANDLE;
        VkDescriptorPool      m_descriptor_pool = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kMaxFramesInFlight> m_descriptor_sets{};

        vulkan::Buffer m_vertices{};
        vulkan::Buffer m_indices{};
        std::array<vulkan::Buffer, kMaxFramesInFlight> m_uniforms{};
    };

} // namespace hybrid::renderer

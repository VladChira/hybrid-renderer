#pragma once

#include "renderer/vulkan/VulkanBuffer.h"
#include "renderer/vulkan/VulkanCommon.h"

#include <vk_mem_alloc.h>

#include <array>
#include <cstdint>
#include <glm/glm.hpp>

namespace hybrid::renderer
{

    // Vulkan port of TraversalHeatmapPass. Owns its pipeline, descriptor
    // sets, SSBOs, and per-frame UBOs. Output is a storage image supplied
    // externally (the renderer's offscreen target) — pass writes into the
    // bound image with layout = GENERAL.
    //
    // Phase 2: SSBO contents are written once at Init from CPU data the
    // caller supplies. Frame-to-frame the only update is the per-frame UBO
    // (camera + render extent + tlas_node_count).
    class TraversalHeatmapVulkanPass
    {
    public:
        static constexpr uint32_t kMaxFramesInFlight = 2;

        // Static SSBO contents handed in at Init time. Layout must match
        // shaders/include/common.glsl (BvhNode, GpuPrimitive, GpuTlasInstance)
        // and shaders/compute/traversal_heatmap.comp bindings (set=0
        // bindings 2/7/9/10).
        struct SsboData
        {
            const void  *primitives        = nullptr;
            VkDeviceSize primitives_bytes  = 0;
            const void  *blas_nodes        = nullptr;
            VkDeviceSize blas_nodes_bytes  = 0;
            const void  *tlas_nodes        = nullptr;
            VkDeviceSize tlas_nodes_bytes  = 0;
            const void  *tlas_instances        = nullptr;
            VkDeviceSize tlas_instances_bytes  = 0;
        };

        // Per-dispatch params. Mirrors the Vulkan-side HeatmapParams UBO
        // declared in traversal_heatmap.comp.
        struct FrameParams
        {
            glm::mat4   inv_view{1.0f};
            glm::mat4   inv_projection{1.0f};
            glm::vec3   camera_position{0.0f};
            glm::uvec2  output_size{0};
            float       heatmap_scale = 64.0f;
            uint32_t    tlas_node_count = 0;
        };

        bool Init(VkDevice device, VmaAllocator allocator);
        void Shutdown();

        // Re-point all per-frame descriptor sets' storage-image binding at
        // `view`. Must be called once after Init and again any time the
        // caller recreates the offscreen image (resize). Caller waits idle
        // first when racing with in-flight reads.
        void SetOutputImageView(VkImageView view);

        // Replace the SSBO contents. Reallocates a buffer when its incoming
        // size differs from the existing buffer; in that case rewrites the
        // affected per-frame descriptor bindings. Caller MUST have waited
        // idle when reallocation is possible (i.e. whenever scene data
        // changed shape between frames).
        void UpdateSsbos(const SsboData &data);

        // Records the dispatch into `cmd`. Caller is responsible for
        // transitioning the output image to GENERAL beforehand and out of
        // GENERAL after.
        void Execute(VkCommandBuffer cmd,
                     uint32_t frame_index,
                     VkExtent2D output_extent,
                     const FrameParams &params);

    private:
        bool CreatePipeline();
        bool CreatePerFrameUniforms();
        bool CreateDescriptorPool();
        bool AllocateDescriptorSets();
        void WriteUboDescriptors();
        void WriteSsboDescriptors();
        void WriteImageDescriptors(VkImageView view);

        VkDevice     m_device    = VK_NULL_HANDLE;
        VmaAllocator m_allocator = VK_NULL_HANDLE;

        VkShaderModule        m_shader_module    = VK_NULL_HANDLE;
        VkDescriptorSetLayout m_set_layout       = VK_NULL_HANDLE;
        VkPipelineLayout      m_pipeline_layout  = VK_NULL_HANDLE;
        VkPipeline            m_pipeline         = VK_NULL_HANDLE;
        VkDescriptorPool      m_descriptor_pool  = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, kMaxFramesInFlight> m_descriptor_sets{};

        vulkan::Buffer m_primitives{};
        vulkan::Buffer m_blas_nodes{};
        vulkan::Buffer m_tlas_nodes{};
        vulkan::Buffer m_tlas_instances{};
        std::array<vulkan::Buffer, kMaxFramesInFlight> m_uniforms{};
    };

} // namespace hybrid::renderer

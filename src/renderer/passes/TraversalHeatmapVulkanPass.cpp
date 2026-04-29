#include "renderer/passes/TraversalHeatmapVulkanPass.h"

#include "renderer/vulkan/VulkanShader.h"

#include <array>
#include <cstring>

namespace hybrid::renderer
{

    namespace
    {
        constexpr uint32_t kWorkgroupSize = 8;

        // Mirror of the Vulkan-side HeatmapParams UBO declared in
        // shaders/compute/traversal_heatmap.comp. std140-padded:
        //   mat4 + mat4 + vec4 + uvec2 + float + uint = 64+64+16+8+4+4 = 160.
        struct HeatmapParamsUbo
        {
            glm::mat4  inv_view;
            glm::mat4  inv_projection;
            glm::vec4  camera_position;     // xyz, w unused
            glm::uvec2 output_size;
            float      heatmap_scale;
            uint32_t   tlas_node_count;
        };
        static_assert(sizeof(HeatmapParamsUbo) == 160,
                      "HeatmapParamsUbo must match std140 layout");

        uint32_t CeilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }
    } // namespace

    bool TraversalHeatmapVulkanPass::Init(VkDevice device,
                                           VmaAllocator allocator,
                                           const SsboData &ssbo_data)
    {
        m_device    = device;
        m_allocator = allocator;

        if (!CreatePipeline())          { Shutdown(); return false; }
        if (!CreateSsbos(ssbo_data))    { Shutdown(); return false; }
        if (!CreatePerFrameUniforms())  { Shutdown(); return false; }
        if (!CreateDescriptorPool())    { Shutdown(); return false; }
        if (!AllocateDescriptorSets())  { Shutdown(); return false; }
        return true;
    }

    void TraversalHeatmapVulkanPass::Shutdown()
    {
        if (m_device == VK_NULL_HANDLE) return;

        if (m_descriptor_pool)  { vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);    m_descriptor_pool = VK_NULL_HANDLE; }
        if (m_pipeline)         { vkDestroyPipeline(m_device, m_pipeline, nullptr);                  m_pipeline = VK_NULL_HANDLE; }
        if (m_pipeline_layout)  { vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);     m_pipeline_layout = VK_NULL_HANDLE; }
        if (m_set_layout)       { vkDestroyDescriptorSetLayout(m_device, m_set_layout, nullptr);     m_set_layout = VK_NULL_HANDLE; }
        if (m_shader_module)    { vkDestroyShaderModule(m_device, m_shader_module, nullptr);         m_shader_module = VK_NULL_HANDLE; }

        for (auto &set : m_descriptor_sets) set = VK_NULL_HANDLE;

        if (m_allocator != VK_NULL_HANDLE)
        {
            vulkan::DestroyBuffer(m_allocator, m_primitives);
            vulkan::DestroyBuffer(m_allocator, m_blas_nodes);
            vulkan::DestroyBuffer(m_allocator, m_tlas_nodes);
            vulkan::DestroyBuffer(m_allocator, m_tlas_instances);
            for (auto &ub : m_uniforms) vulkan::DestroyBuffer(m_allocator, ub);
        }

        m_device    = VK_NULL_HANDLE;
        m_allocator = VK_NULL_HANDLE;
    }

    // -----------------------------------------------------------------------
    // Pipeline / shader / descriptor layout
    // -----------------------------------------------------------------------

    bool TraversalHeatmapVulkanPass::CreatePipeline()
    {
        // Bindings match traversal_heatmap.comp + heatmap_bvh_traversal.glsl.
        // The shader uses sparse binding indices (0, 1, 2, 7, 9, 10); declare
        // exactly those, leave the gaps unbound.
        const std::array<VkDescriptorSetLayoutBinding, 6> bindings{{
            { 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // o_heatmap
            { 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // HeatmapParams
            { 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // primitives
            { 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // blas_nodes
            { 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // tlas_nodes
            {10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr }, // tlas_instances
        }};

        VkDescriptorSetLayoutCreateInfo dsl_info{};
        dsl_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl_info.bindingCount = static_cast<uint32_t>(bindings.size());
        dsl_info.pBindings    = bindings.data();
        if (!HYBRID_VK_CHECK(vkCreateDescriptorSetLayout(m_device, &dsl_info, nullptr, &m_set_layout)))
        {
            return false;
        }

        VkPipelineLayoutCreateInfo pl_info{};
        pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_info.setLayoutCount = 1;
        pl_info.pSetLayouts    = &m_set_layout;
        if (!HYBRID_VK_CHECK(vkCreatePipelineLayout(m_device, &pl_info, nullptr, &m_pipeline_layout)))
        {
            return false;
        }

        auto spirv = vulkan::LoadSpirv("compute/traversal_heatmap.comp.spv");
        if (spirv.empty()) return false;
        m_shader_module = vulkan::CreateShaderModule(m_device, spirv);
        if (m_shader_module == VK_NULL_HANDLE) return false;

        VkPipelineShaderStageCreateInfo stage{};
        stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
        stage.module = m_shader_module;
        stage.pName  = "main";

        VkComputePipelineCreateInfo cp{};
        cp.sType  = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cp.stage  = stage;
        cp.layout = m_pipeline_layout;
        return HYBRID_VK_CHECK(vkCreateComputePipelines(
            m_device, VK_NULL_HANDLE, 1, &cp, nullptr, &m_pipeline));
    }

    // -----------------------------------------------------------------------
    // Resources
    // -----------------------------------------------------------------------

    namespace
    {
        bool UploadHostVisible(vulkan::Buffer &buf,
                               VmaAllocator allocator,
                               VkDeviceSize size,
                               const void *src,
                               VkBufferUsageFlags usage)
        {
            buf = vulkan::CreateHostVisibleBuffer(allocator, size, usage);
            if (buf.handle == VK_NULL_HANDLE) return false;
            std::memcpy(buf.mapped, src, static_cast<size_t>(size));
            return true;
        }
    } // namespace

    bool TraversalHeatmapVulkanPass::CreateSsbos(const SsboData &d)
    {
        if (!d.primitives || d.primitives_bytes == 0 ||
            !d.blas_nodes || d.blas_nodes_bytes == 0 ||
            !d.tlas_nodes || d.tlas_nodes_bytes == 0 ||
            !d.tlas_instances || d.tlas_instances_bytes == 0)
        {
            LOG_ERROR("[heatmap-vk] CreateSsbos: empty input data");
            return false;
        }
        constexpr VkBufferUsageFlags kSsboUsage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        return UploadHostVisible(m_primitives,     m_allocator, d.primitives_bytes,     d.primitives,     kSsboUsage)
            && UploadHostVisible(m_blas_nodes,     m_allocator, d.blas_nodes_bytes,     d.blas_nodes,     kSsboUsage)
            && UploadHostVisible(m_tlas_nodes,     m_allocator, d.tlas_nodes_bytes,     d.tlas_nodes,     kSsboUsage)
            && UploadHostVisible(m_tlas_instances, m_allocator, d.tlas_instances_bytes, d.tlas_instances, kSsboUsage);
    }

    bool TraversalHeatmapVulkanPass::CreatePerFrameUniforms()
    {
        for (auto &ub : m_uniforms)
        {
            ub = vulkan::CreateHostVisibleBuffer(
                m_allocator, sizeof(HeatmapParamsUbo),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            if (ub.handle == VK_NULL_HANDLE) return false;
        }
        return true;
    }

    bool TraversalHeatmapVulkanPass::CreateDescriptorPool()
    {
        const std::array<VkDescriptorPoolSize, 3> sizes{{
            { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  kMaxFramesInFlight },
            { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, kMaxFramesInFlight },
            { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, kMaxFramesInFlight * 4 },
        }};
        VkDescriptorPoolCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets       = kMaxFramesInFlight;
        info.poolSizeCount = static_cast<uint32_t>(sizes.size());
        info.pPoolSizes    = sizes.data();
        return HYBRID_VK_CHECK(vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptor_pool));
    }

    bool TraversalHeatmapVulkanPass::AllocateDescriptorSets()
    {
        std::array<VkDescriptorSetLayout, kMaxFramesInFlight> layouts;
        layouts.fill(m_set_layout);

        VkDescriptorSetAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        ai.descriptorPool     = m_descriptor_pool;
        ai.descriptorSetCount = kMaxFramesInFlight;
        ai.pSetLayouts        = layouts.data();
        return HYBRID_VK_CHECK(vkAllocateDescriptorSets(m_device, &ai, m_descriptor_sets.data()));
    }

    void TraversalHeatmapVulkanPass::SetOutputImageView(VkImageView view)
    {
        if (m_device == VK_NULL_HANDLE || view == VK_NULL_HANDLE) return;

        // Re-write all kMaxFramesInFlight sets. Each set gets:
        //   binding 0  -> storage image (the one shared output view)
        //   binding 1  -> this frame's UBO
        //   binding 2  -> primitives SSBO
        //   binding 7  -> blas_nodes SSBO
        //   binding 9  -> tlas_nodes SSBO
        //   binding 10 -> tlas_instances SSBO
        std::array<VkDescriptorImageInfo,  kMaxFramesInFlight>     img_infos{};
        std::array<VkDescriptorBufferInfo, kMaxFramesInFlight>     ub_infos{};
        std::array<VkDescriptorBufferInfo, 4>                      ssbo_infos{};
        ssbo_infos[0] = { m_primitives.handle,     0, m_primitives.size };
        ssbo_infos[1] = { m_blas_nodes.handle,     0, m_blas_nodes.size };
        ssbo_infos[2] = { m_tlas_nodes.handle,     0, m_tlas_nodes.size };
        ssbo_infos[3] = { m_tlas_instances.handle, 0, m_tlas_instances.size };

        std::array<VkWriteDescriptorSet, kMaxFramesInFlight * 6> writes{};
        size_t w = 0;

        for (uint32_t f = 0; f < kMaxFramesInFlight; ++f)
        {
            img_infos[f].imageView   = view;
            img_infos[f].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

            ub_infos[f].buffer = m_uniforms[f].handle;
            ub_infos[f].offset = 0;
            ub_infos[f].range  = VK_WHOLE_SIZE;

            auto WriteEntry = [&](uint32_t binding, VkDescriptorType type,
                                   const VkDescriptorImageInfo *img,
                                   const VkDescriptorBufferInfo *buf) {
                VkWriteDescriptorSet &out = writes[w++];
                out = {};
                out.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                out.dstSet          = m_descriptor_sets[f];
                out.dstBinding      = binding;
                out.descriptorType  = type;
                out.descriptorCount = 1;
                out.pImageInfo      = img;
                out.pBufferInfo     = buf;
            };

            WriteEntry( 0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,  &img_infos[f], nullptr);
            WriteEntry( 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, nullptr, &ub_infos[f]);
            WriteEntry( 2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssbo_infos[0]);
            WriteEntry( 7, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssbo_infos[1]);
            WriteEntry( 9, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssbo_infos[2]);
            WriteEntry(10, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, nullptr, &ssbo_infos[3]);
        }

        vkUpdateDescriptorSets(m_device,
                               static_cast<uint32_t>(writes.size()),
                               writes.data(),
                               0, nullptr);
    }

    // -----------------------------------------------------------------------
    // Execute
    // -----------------------------------------------------------------------

    void TraversalHeatmapVulkanPass::Execute(VkCommandBuffer cmd,
                                              uint32_t frame_index,
                                              VkExtent2D output_extent,
                                              const FrameParams &params)
    {
        if (cmd == VK_NULL_HANDLE) return;
        const uint32_t fi = frame_index % kMaxFramesInFlight;

        HeatmapParamsUbo ubo{};
        ubo.inv_view        = params.inv_view;
        ubo.inv_projection  = params.inv_projection;
        ubo.camera_position = glm::vec4(params.camera_position, 0.0f);
        ubo.output_size     = params.output_size;
        ubo.heatmap_scale   = params.heatmap_scale;
        ubo.tlas_node_count = params.tlas_node_count;
        std::memcpy(m_uniforms[fi].mapped, &ubo, sizeof(ubo));

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_pipeline_layout, 0, 1,
                                &m_descriptor_sets[fi], 0, nullptr);
        vkCmdDispatch(cmd,
                      CeilDiv(output_extent.width,  kWorkgroupSize),
                      CeilDiv(output_extent.height, kWorkgroupSize),
                      1);
    }

} // namespace hybrid::renderer

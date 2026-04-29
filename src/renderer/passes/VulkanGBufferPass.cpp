#include "renderer/passes/VulkanGBufferPass.h"

#include "renderer/vulkan/VulkanShader.h"

#include <array>
#include <cstring>

namespace hybrid::renderer
{

    namespace
    {
        // CPU mirror of shaders/gbuffer_vk.vert's GBufferFrame UBO. std140:
        //   mat4 view, mat4 projection -> 128 bytes, multiple of 16. ✓
        struct FrameUbo
        {
            glm::mat4 view;
            glm::mat4 projection;
        };
        static_assert(sizeof(FrameUbo) == 128, "FrameUbo std140 = 128 bytes");

        // GpuVertex layout (renderer/stores/GeometryStore.h). Stride 80,
        // attribute offsets pinned by the existing std430-aligned struct.
        constexpr uint32_t kVertexStride = 80;
        constexpr uint32_t kAttribPositionOffset = 0;
        constexpr uint32_t kAttribNormalOffset   = 16;
        constexpr uint32_t kAttribTangentOffset  = 32;
        constexpr uint32_t kAttribUv0Offset      = 48;
        constexpr uint32_t kAttribUv1Offset      = 56;
    } // namespace

    bool VulkanGBufferPass::Init(VkDevice device,
                                  VmaAllocator allocator,
                                  VkFormat color_format,
                                  VkFormat depth_format)
    {
        m_device    = device;
        m_allocator = allocator;

        if (!CreatePipeline(color_format, depth_format)) { Shutdown(); return false; }
        if (!CreatePerFrameUniforms())                   { Shutdown(); return false; }
        if (!CreateDescriptorPool())                     { Shutdown(); return false; }
        if (!AllocateDescriptorSets())                   { Shutdown(); return false; }
        WriteUboDescriptors();

        // 1-byte placeholders so descriptor set / vkCmdBindVertexBuffers
        // calls don't see VK_NULL_HANDLE before the first scene arrives.
        // Replaced on first UpdateGeometry call.
        uint32_t placeholder = 0;
        GeometryUpload empty{};
        empty.vertices       = &placeholder;
        empty.vertices_bytes = sizeof(placeholder);
        empty.indices        = &placeholder;
        empty.indices_bytes  = sizeof(placeholder);
        UpdateGeometry(empty);
        return m_vertices.handle != VK_NULL_HANDLE && m_indices.handle != VK_NULL_HANDLE;
    }

    void VulkanGBufferPass::Shutdown()
    {
        if (m_device == VK_NULL_HANDLE) return;

        if (m_descriptor_pool)  { vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr); m_descriptor_pool = VK_NULL_HANDLE; }
        if (m_pipeline)         { vkDestroyPipeline(m_device, m_pipeline, nullptr);             m_pipeline = VK_NULL_HANDLE; }
        if (m_pipeline_layout)  { vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr); m_pipeline_layout = VK_NULL_HANDLE; }
        if (m_set_layout)       { vkDestroyDescriptorSetLayout(m_device, m_set_layout, nullptr); m_set_layout = VK_NULL_HANDLE; }
        if (m_vert_module)      { vkDestroyShaderModule(m_device, m_vert_module, nullptr);      m_vert_module = VK_NULL_HANDLE; }
        if (m_frag_module)      { vkDestroyShaderModule(m_device, m_frag_module, nullptr);      m_frag_module = VK_NULL_HANDLE; }

        for (auto &set : m_descriptor_sets) set = VK_NULL_HANDLE;

        if (m_allocator != VK_NULL_HANDLE)
        {
            vulkan::DestroyBuffer(m_allocator, m_vertices);
            vulkan::DestroyBuffer(m_allocator, m_indices);
            for (auto &ub : m_uniforms) vulkan::DestroyBuffer(m_allocator, ub);
        }

        m_device    = VK_NULL_HANDLE;
        m_allocator = VK_NULL_HANDLE;
    }

    // -----------------------------------------------------------------------
    // Pipeline
    // -----------------------------------------------------------------------

    bool VulkanGBufferPass::CreatePipeline(VkFormat color_format, VkFormat depth_format)
    {
        // ---- shader modules -----------------------------------------------
        auto vert_spv = vulkan::LoadSpirv("gbuffer_vk.vert.spv");
        auto frag_spv = vulkan::LoadSpirv("gbuffer_vk.frag.spv");
        if (vert_spv.empty() || frag_spv.empty()) return false;
        m_vert_module = vulkan::CreateShaderModule(m_device, vert_spv);
        m_frag_module = vulkan::CreateShaderModule(m_device, frag_spv);
        if (m_vert_module == VK_NULL_HANDLE || m_frag_module == VK_NULL_HANDLE) return false;

        // ---- descriptor set layout: 1 UBO at binding 0 --------------------
        VkDescriptorSetLayoutBinding binding{};
        binding.binding         = 0;
        binding.descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        binding.descriptorCount = 1;
        binding.stageFlags      = VK_SHADER_STAGE_VERTEX_BIT;

        VkDescriptorSetLayoutCreateInfo dsl_info{};
        dsl_info.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dsl_info.bindingCount = 1;
        dsl_info.pBindings    = &binding;
        if (!HYBRID_VK_CHECK(vkCreateDescriptorSetLayout(m_device, &dsl_info, nullptr, &m_set_layout)))
        {
            return false;
        }

        // ---- pipeline layout: set + push constant (mat4 model) ------------
        VkPushConstantRange pc{};
        pc.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
        pc.offset     = 0;
        pc.size       = sizeof(glm::mat4);

        VkPipelineLayoutCreateInfo pl_info{};
        pl_info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        pl_info.setLayoutCount         = 1;
        pl_info.pSetLayouts            = &m_set_layout;
        pl_info.pushConstantRangeCount = 1;
        pl_info.pPushConstantRanges    = &pc;
        if (!HYBRID_VK_CHECK(vkCreatePipelineLayout(m_device, &pl_info, nullptr, &m_pipeline_layout)))
        {
            return false;
        }

        // ---- shader stages -------------------------------------------------
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        stages[0].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage  = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = m_vert_module;
        stages[0].pName  = "main";
        stages[1].sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[1].stage  = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = m_frag_module;
        stages[1].pName  = "main";

        // ---- vertex input: GpuVertex (stride 80) --------------------------
        VkVertexInputBindingDescription vib{};
        vib.binding   = 0;
        vib.stride    = kVertexStride;
        vib.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;

        const std::array<VkVertexInputAttributeDescription, 5> attrs{{
            { /*loc*/0, /*bind*/0, VK_FORMAT_R32G32B32_SFLOAT,    kAttribPositionOffset },
            { /*loc*/1, /*bind*/0, VK_FORMAT_R32G32B32_SFLOAT,    kAttribNormalOffset   },
            { /*loc*/2, /*bind*/0, VK_FORMAT_R32G32_SFLOAT,       kAttribUv0Offset      },
            { /*loc*/3, /*bind*/0, VK_FORMAT_R32G32_SFLOAT,       kAttribUv1Offset      },
            { /*loc*/4, /*bind*/0, VK_FORMAT_R32G32B32A32_SFLOAT, kAttribTangentOffset  },
        }};

        VkPipelineVertexInputStateCreateInfo vi{};
        vi.sType                           = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        vi.vertexBindingDescriptionCount   = 1;
        vi.pVertexBindingDescriptions      = &vib;
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vi.pVertexAttributeDescriptions    = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{};
        ia.sType    = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

        // Viewport + scissor are set dynamically (vkCmdSetViewport / scissor).
        VkPipelineViewportStateCreateInfo vp{};
        vp.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        vp.viewportCount = 1;
        vp.scissorCount  = 1;

        VkPipelineRasterizationStateCreateInfo rs{};
        rs.sType       = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode    = VK_CULL_MODE_BACK_BIT;
        // glTF default: CCW front faces. With a negative-height viewport
        // the Y flip is applied symmetrically to triangle vertices — NDC
        // winding is preserved in framebuffer space, so CCW stays correct.
        rs.frontFace   = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        rs.lineWidth   = 1.0f;

        VkPipelineMultisampleStateCreateInfo ms{};
        ms.sType                = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

        VkPipelineDepthStencilStateCreateInfo ds{};
        ds.sType                 = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        ds.depthTestEnable       = VK_TRUE;
        ds.depthWriteEnable      = VK_TRUE;
        ds.depthCompareOp        = VK_COMPARE_OP_LESS;
        ds.depthBoundsTestEnable = VK_FALSE;
        ds.stencilTestEnable     = VK_FALSE;

        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                             VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        ba.blendEnable    = VK_FALSE;

        VkPipelineColorBlendStateCreateInfo cb{};
        cb.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        cb.attachmentCount = 1;
        cb.pAttachments    = &ba;

        const std::array<VkDynamicState, 2> dynamic_states{{
            VK_DYNAMIC_STATE_VIEWPORT,
            VK_DYNAMIC_STATE_SCISSOR,
        }};
        VkPipelineDynamicStateCreateInfo dyn{};
        dyn.sType             = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dyn.dynamicStateCount = static_cast<uint32_t>(dynamic_states.size());
        dyn.pDynamicStates    = dynamic_states.data();

        // Dynamic rendering — pipeline must declare the attachment formats it
        // will be used with. Chained via pNext on the graphics pipeline info.
        VkPipelineRenderingCreateInfo render_info{};
        render_info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        render_info.colorAttachmentCount    = 1;
        render_info.pColorAttachmentFormats = &color_format;
        render_info.depthAttachmentFormat   = depth_format;

        VkGraphicsPipelineCreateInfo gp{};
        gp.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        gp.pNext               = &render_info;
        gp.stageCount          = static_cast<uint32_t>(stages.size());
        gp.pStages             = stages.data();
        gp.pVertexInputState   = &vi;
        gp.pInputAssemblyState = &ia;
        gp.pViewportState      = &vp;
        gp.pRasterizationState = &rs;
        gp.pMultisampleState   = &ms;
        gp.pDepthStencilState  = &ds;
        gp.pColorBlendState    = &cb;
        gp.pDynamicState       = &dyn;
        gp.layout              = m_pipeline_layout;

        return HYBRID_VK_CHECK(vkCreateGraphicsPipelines(
            m_device, VK_NULL_HANDLE, 1, &gp, nullptr, &m_pipeline));
    }

    // -----------------------------------------------------------------------
    // Resources
    // -----------------------------------------------------------------------

    bool VulkanGBufferPass::CreatePerFrameUniforms()
    {
        for (auto &ub : m_uniforms)
        {
            ub = vulkan::CreateHostVisibleBuffer(
                m_allocator, sizeof(FrameUbo),
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
            if (ub.handle == VK_NULL_HANDLE) return false;
        }
        return true;
    }

    bool VulkanGBufferPass::CreateDescriptorPool()
    {
        VkDescriptorPoolSize size{};
        size.type            = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        size.descriptorCount = kMaxFramesInFlight;

        VkDescriptorPoolCreateInfo info{};
        info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        info.maxSets       = kMaxFramesInFlight;
        info.poolSizeCount = 1;
        info.pPoolSizes    = &size;
        return HYBRID_VK_CHECK(vkCreateDescriptorPool(m_device, &info, nullptr, &m_descriptor_pool));
    }

    bool VulkanGBufferPass::AllocateDescriptorSets()
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

    void VulkanGBufferPass::WriteUboDescriptors()
    {
        std::array<VkDescriptorBufferInfo, kMaxFramesInFlight> infos{};
        std::array<VkWriteDescriptorSet,   kMaxFramesInFlight> writes{};
        for (uint32_t f = 0; f < kMaxFramesInFlight; ++f)
        {
            infos[f].buffer = m_uniforms[f].handle;
            infos[f].offset = 0;
            infos[f].range  = VK_WHOLE_SIZE;

            writes[f].sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[f].dstSet          = m_descriptor_sets[f];
            writes[f].dstBinding      = 0;
            writes[f].descriptorType  = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
            writes[f].descriptorCount = 1;
            writes[f].pBufferInfo     = &infos[f];
        }
        vkUpdateDescriptorSets(m_device,
                               static_cast<uint32_t>(writes.size()), writes.data(),
                               0, nullptr);
    }

    void VulkanGBufferPass::UpdateGeometry(const GeometryUpload &geo)
    {
        auto sync = [&](vulkan::Buffer &buf, const void *src, VkDeviceSize size,
                         VkBufferUsageFlags usage)
        {
            if (src == nullptr || size == 0) return;
            if (buf.size != size)
            {
                vulkan::DestroyBuffer(m_allocator, buf);
                buf = vulkan::CreateHostVisibleBuffer(m_allocator, size, usage);
            }
            std::memcpy(buf.mapped, src, static_cast<size_t>(size));
        };
        sync(m_vertices, geo.vertices, geo.vertices_bytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        sync(m_indices,  geo.indices,  geo.indices_bytes,  VK_BUFFER_USAGE_INDEX_BUFFER_BIT);
    }

    // -----------------------------------------------------------------------
    // Execute
    // -----------------------------------------------------------------------

    void VulkanGBufferPass::Execute(VkCommandBuffer cmd,
                                     uint32_t frame_index,
                                     VkExtent2D extent,
                                     VkImageView color_view,
                                     VkImageView depth_view,
                                     const FrameParams &params,
                                     const std::vector<DrawCall> &draws)
    {
        if (cmd == VK_NULL_HANDLE) return;
        const uint32_t fi = frame_index % kMaxFramesInFlight;

        // Update per-frame UBO.
        FrameUbo ubo{};
        ubo.view       = params.view;
        ubo.projection = params.projection;
        std::memcpy(m_uniforms[fi].mapped, &ubo, sizeof(ubo));

        // ---- dynamic-rendering scope --------------------------------------
        VkRenderingAttachmentInfo color_attach{};
        color_attach.sType                = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        color_attach.imageView            = color_view;
        color_attach.imageLayout          = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        color_attach.loadOp               = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color_attach.storeOp              = VK_ATTACHMENT_STORE_OP_STORE;
        color_attach.clearValue.color     = {{0.05f, 0.05f, 0.05f, 1.0f}};

        VkRenderingAttachmentInfo depth_attach{};
        depth_attach.sType                       = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
        depth_attach.imageView                   = depth_view;
        // Legacy combined depth-stencil layout — works without enabling the
        // separateDepthStencilLayouts feature (which we don't).
        depth_attach.imageLayout                 = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        depth_attach.loadOp                      = VK_ATTACHMENT_LOAD_OP_CLEAR;
        depth_attach.storeOp                     = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        depth_attach.clearValue.depthStencil     = {1.0f, 0};

        VkRenderingInfo ri{};
        ri.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO;
        ri.renderArea.offset    = {0, 0};
        ri.renderArea.extent    = extent;
        ri.layerCount           = 1;
        ri.colorAttachmentCount = 1;
        ri.pColorAttachments    = &color_attach;
        ri.pDepthAttachment     = &depth_attach;

        vkCmdBeginRendering(cmd, &ri);

        // Standard positive-height viewport. Y flip is baked into the
        // projection matrix in App.cpp's view resolution (so ImGuizmo sees
        // the same screen-space mapping we render with).
        VkViewport vp{};
        vp.x        = 0.0f;
        vp.y        = 0.0f;
        vp.width    = static_cast<float>(extent.width);
        vp.height   = static_cast<float>(extent.height);
        vp.minDepth = 0.0f;
        vp.maxDepth = 1.0f;
        vkCmdSetViewport(cmd, 0, 1, &vp);

        VkRect2D scissor{};
        scissor.offset = {0, 0};
        scissor.extent = extent;
        vkCmdSetScissor(cmd, 0, 1, &scissor);

        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                                m_pipeline_layout, 0, 1,
                                &m_descriptor_sets[fi], 0, nullptr);

        VkDeviceSize zero_offset = 0;
        vkCmdBindVertexBuffers(cmd, 0, 1, &m_vertices.handle, &zero_offset);
        vkCmdBindIndexBuffer(cmd, m_indices.handle, 0, VK_INDEX_TYPE_UINT32);

        for (const DrawCall &draw : draws)
        {
            vkCmdPushConstants(cmd, m_pipeline_layout,
                               VK_SHADER_STAGE_VERTEX_BIT,
                               0, sizeof(glm::mat4),
                               &draw.model);
            vkCmdDrawIndexed(cmd,
                             draw.index_count,
                             /*instanceCount*/1,
                             draw.first_index,
                             draw.vertex_offset,
                             /*firstInstance*/0);
        }

        vkCmdEndRendering(cmd);
    }

} // namespace hybrid::renderer

// Vulkan-mode stub for Renderer.
//
// Phase 1 progress (this file):
//   * Brings up VulkanRenderBackend (instance/device/swapchain/VMA/offscreen).
//   * Creates a tiny compute pipeline that writes a time-varying gradient
//     into the offscreen image, then blits the offscreen onto the
//     swapchain.
//
// This is the smallest "real" Vulkan workload — a compute shader writing
// to a storage image — and it exercises the full toolchain we need for
// every other compute pass: SPIR-V load, descriptor sets, pipeline
// layouts, push constants, image layout transitions, dispatch, blit. As
// real passes get ported, the pipeline-creation and per-frame logic here
// becomes the template for them.
//
// Excluded from the build in opengl mode; the real
// src/renderer/Renderer.cpp is used instead.

#include "renderer/Renderer.h"
#include "renderer/raytracing/AccelerationStructureCache.h"
#include "renderer/vulkan/VulkanRenderBackend.h"
#include "renderer/vulkan/VulkanShader.h"

#include "core/Log.h"

#include <GLFW/glfw3.h>

#include <array>
#include <cstring>

namespace hybrid::renderer
{

    namespace
    {
        // Push constants supplied to swapchain_clear.comp. Keep in sync.
        struct ClearPushConstants
        {
            uint32_t size_x;
            uint32_t size_y;
            float    time_seconds;
            float    _pad;
        };
        static_assert(sizeof(ClearPushConstants) == 16,
                      "Push constant block layout drift vs swapchain_clear.comp");

        constexpr uint32_t kComputeWorkgroupSize = 8;
        uint32_t CeilDiv(uint32_t a, uint32_t b) { return (a + b - 1) / b; }

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

        // Compute pipeline state for swapchain_clear.comp.
        VkShaderModule        clear_shader      = VK_NULL_HANDLE;
        VkDescriptorSetLayout clear_set_layout  = VK_NULL_HANDLE;
        VkPipelineLayout      clear_pipe_layout = VK_NULL_HANDLE;
        VkPipeline            clear_pipeline    = VK_NULL_HANDLE;
        VkDescriptorPool      descriptor_pool   = VK_NULL_HANDLE;
        std::array<VkDescriptorSet, VulkanRenderBackend::kMaxFramesInFlight> clear_sets{};
        VkExtent2D last_descriptor_extent{0, 0};
    };

    namespace
    {
        bool CreateClearPipeline(Renderer::Impl &impl)
        {
            VkDevice device = impl.backend.Device().Logical();

            // ---- shader module ------------------------------------------
            auto spirv = vulkan::LoadSpirv("compute/swapchain_clear.comp.spv");
            if (spirv.empty()) return false;
            impl.clear_shader = vulkan::CreateShaderModule(device, spirv);
            if (impl.clear_shader == VK_NULL_HANDLE) return false;

            // ---- descriptor set layout: 1 storage image at (0,0) --------
            VkDescriptorSetLayoutBinding binding{};
            binding.binding = 0;
            binding.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            binding.descriptorCount = 1;
            binding.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

            VkDescriptorSetLayoutCreateInfo set_layout_info{};
            set_layout_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            set_layout_info.bindingCount = 1;
            set_layout_info.pBindings = &binding;
            if (!HYBRID_VK_CHECK(vkCreateDescriptorSetLayout(device, &set_layout_info, nullptr, &impl.clear_set_layout)))
            {
                return false;
            }

            // ---- pipeline layout (set + push constants) -----------------
            VkPushConstantRange pc{};
            pc.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            pc.offset = 0;
            pc.size = sizeof(ClearPushConstants);

            VkPipelineLayoutCreateInfo pl_info{};
            pl_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            pl_info.setLayoutCount = 1;
            pl_info.pSetLayouts = &impl.clear_set_layout;
            pl_info.pushConstantRangeCount = 1;
            pl_info.pPushConstantRanges = &pc;
            if (!HYBRID_VK_CHECK(vkCreatePipelineLayout(device, &pl_info, nullptr, &impl.clear_pipe_layout)))
            {
                return false;
            }

            // ---- compute pipeline ---------------------------------------
            VkPipelineShaderStageCreateInfo stage{};
            stage.sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.stage  = VK_SHADER_STAGE_COMPUTE_BIT;
            stage.module = impl.clear_shader;
            stage.pName  = "main";

            VkComputePipelineCreateInfo cp{};
            cp.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cp.stage = stage;
            cp.layout = impl.clear_pipe_layout;
            if (!HYBRID_VK_CHECK(vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &cp, nullptr, &impl.clear_pipeline)))
            {
                return false;
            }

            // ---- descriptor pool ----------------------------------------
            VkDescriptorPoolSize pool_size{};
            pool_size.type = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
            pool_size.descriptorCount = VulkanRenderBackend::kMaxFramesInFlight;

            VkDescriptorPoolCreateInfo pool_info{};
            pool_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pool_info.maxSets = VulkanRenderBackend::kMaxFramesInFlight;
            pool_info.poolSizeCount = 1;
            pool_info.pPoolSizes = &pool_size;
            if (!HYBRID_VK_CHECK(vkCreateDescriptorPool(device, &pool_info, nullptr, &impl.descriptor_pool)))
            {
                return false;
            }

            // ---- allocate one set per frame in flight -------------------
            std::array<VkDescriptorSetLayout, VulkanRenderBackend::kMaxFramesInFlight> layouts;
            layouts.fill(impl.clear_set_layout);

            VkDescriptorSetAllocateInfo alloc_info{};
            alloc_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            alloc_info.descriptorPool = impl.descriptor_pool;
            alloc_info.descriptorSetCount = static_cast<uint32_t>(layouts.size());
            alloc_info.pSetLayouts = layouts.data();
            if (!HYBRID_VK_CHECK(vkAllocateDescriptorSets(device, &alloc_info, impl.clear_sets.data())))
            {
                return false;
            }
            return true;
        }

        void DestroyClearPipeline(Renderer::Impl &impl)
        {
            VkDevice device = impl.backend.Device().Logical();
            if (impl.descriptor_pool)   { vkDestroyDescriptorPool(device, impl.descriptor_pool, nullptr);   impl.descriptor_pool = VK_NULL_HANDLE; }
            if (impl.clear_pipeline)    { vkDestroyPipeline(device, impl.clear_pipeline, nullptr);          impl.clear_pipeline = VK_NULL_HANDLE; }
            if (impl.clear_pipe_layout) { vkDestroyPipelineLayout(device, impl.clear_pipe_layout, nullptr); impl.clear_pipe_layout = VK_NULL_HANDLE; }
            if (impl.clear_set_layout)  { vkDestroyDescriptorSetLayout(device, impl.clear_set_layout, nullptr); impl.clear_set_layout = VK_NULL_HANDLE; }
            if (impl.clear_shader)      { vkDestroyShaderModule(device, impl.clear_shader, nullptr);        impl.clear_shader = VK_NULL_HANDLE; }
            for (auto &set : impl.clear_sets) set = VK_NULL_HANDLE;
        }

        // Re-write all per-frame descriptor sets to point at the current
        // offscreen image view. Called whenever the offscreen extent
        // changes (initial creation + resize). Safe because the backend's
        // resize path waits idle before recreating.
        void RewriteClearDescriptors(Renderer::Impl &impl)
        {
            VkDevice device = impl.backend.Device().Logical();

            std::array<VkDescriptorImageInfo, VulkanRenderBackend::kMaxFramesInFlight> image_infos{};
            std::array<VkWriteDescriptorSet, VulkanRenderBackend::kMaxFramesInFlight>  writes{};
            for (uint32_t i = 0; i < VulkanRenderBackend::kMaxFramesInFlight; ++i)
            {
                image_infos[i].imageView = impl.backend.OffscreenImageView();
                image_infos[i].imageLayout = VK_IMAGE_LAYOUT_GENERAL;

                writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
                writes[i].dstSet = impl.clear_sets[i];
                writes[i].dstBinding = 0;
                writes[i].descriptorCount = 1;
                writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[i].pImageInfo = &image_infos[i];
            }
            vkUpdateDescriptorSets(device,
                                   static_cast<uint32_t>(writes.size()),
                                   writes.data(),
                                   0, nullptr);
            impl.last_descriptor_extent = impl.backend.OffscreenExtent();
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

        if (!CreateClearPipeline(*m_impl))
        {
            LOG_ERROR("[renderer/vulkan] clear pipeline creation failed");
            DestroyClearPipeline(*m_impl);
            m_impl->backend.Shutdown();
            return false;
        }
        RewriteClearDescriptors(*m_impl);

        LOG_INFO("[renderer/vulkan] Phase 1 stub: gradient compute -> blit -> present");
        m_impl->initialized = true;
        return true;
    }

    void Renderer::Shutdown()
    {
        if (!m_impl->initialized) return;
        m_impl->backend.Device().WaitIdle();
        DestroyClearPipeline(*m_impl);
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

        // If the backend recreated the offscreen image (initial + resize),
        // re-point the descriptors at the new view.
        VkExtent2D current_extent = m_impl->backend.OffscreenExtent();
        if (current_extent.width  != m_impl->last_descriptor_extent.width ||
            current_extent.height != m_impl->last_descriptor_extent.height)
        {
            RewriteClearDescriptors(*m_impl);
        }

        VkCommandBuffer cmd       = m_impl->backend.CurrentCommandBuffer();
        VkImage         offscreen = m_impl->backend.OffscreenImage();
        VkImage         swap      = m_impl->backend.CurrentSwapchainImage();
        VkExtent2D      extent    = m_impl->backend.SwapchainExtent();
        VkDescriptorSet set       = m_impl->clear_sets[m_impl->backend.FrameIndexInFlight()];

        // 1) offscreen UNDEFINED -> GENERAL for compute write
        ImageBarrier(cmd, offscreen,
                     VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_GENERAL,
                     0, VK_ACCESS_SHADER_WRITE_BIT,
                     VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                     VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

        // 2) bind compute pipeline + set, push constants, dispatch
        vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, m_impl->clear_pipeline);
        vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                                m_impl->clear_pipe_layout, 0, 1, &set, 0, nullptr);

        ClearPushConstants pc{};
        pc.size_x = extent.width;
        pc.size_y = extent.height;
        pc.time_seconds = m_impl->current_time;
        pc._pad = 0.0f;
        vkCmdPushConstants(cmd, m_impl->clear_pipe_layout,
                           VK_SHADER_STAGE_COMPUTE_BIT,
                           0, sizeof(pc), &pc);
        vkCmdDispatch(cmd,
                      CeilDiv(extent.width,  kComputeWorkgroupSize),
                      CeilDiv(extent.height, kComputeWorkgroupSize),
                      1);

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

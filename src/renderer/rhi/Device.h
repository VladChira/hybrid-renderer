#pragma once

#include "renderer/rhi/RhiTypes.h"

#include <cstddef>
#include <cstdint>
#include <span>

// rhi::Device is the main RHI surface. It owns the underlying GPU device,
// allocates resources, and produces CommandLists. Pass code receives a
// reference to a Device + a CommandList and never touches the backend
// directly.
//
// Both backends (OpenGL and Vulkan) implement this interface. The OpenGL
// backend exists primarily to make the migration incremental — see
// VULKAN_PLAN.md.

namespace hybrid::renderer::rhi
{

    class CommandList; // forward

    class Device
    {
    public:
        virtual ~Device() = default;

        // ---- Lifetime -----------------------------------------------------
        // Called once at startup. May fail (e.g. no Vulkan device).
        virtual bool Initialize() = 0;
        virtual void Shutdown() = 0;

        // Wait until all outstanding GPU work is done. Used at shutdown and
        // when destroying resources synchronously.
        virtual void WaitIdle() = 0;

        // ---- Frame loop ---------------------------------------------------
        // Begin a new frame. Returns a CommandList valid until EndFrame.
        // Frame-scoped resources (uniforms, descriptor sets) live until the
        // matching EndFrame.
        virtual CommandList *BeginFrame() = 0;
        virtual void EndFrame() = 0;

        // Acquire the next swapchain image. Returns false if the swapchain
        // needs to be recreated (e.g. resize, out-of-date). Caller should
        // RecreateSwapchain in that case.
        virtual bool AcquireNextSwapchainImage(uint32_t &out_index) = 0;
        virtual void Present() = 0;
        virtual void RecreateSwapchain(uint32_t width, uint32_t height) = 0;

        // ---- Resources ----------------------------------------------------
        virtual BufferHandle CreateBuffer(const BufferDesc &desc) = 0;
        virtual void DestroyBuffer(BufferHandle handle) = 0;
        // Upload via mapped pointer for HostVisible / Staging buffers.
        virtual void *MapBuffer(BufferHandle handle) = 0;
        virtual void UnmapBuffer(BufferHandle handle) = 0;
        // Convenience for one-shot upload to device-local buffers.
        virtual void UploadBuffer(BufferHandle handle,
                                  const void *data,
                                  size_t size,
                                  size_t offset = 0) = 0;

        virtual TextureHandle CreateTexture(const TextureDesc &desc) = 0;
        virtual void DestroyTexture(TextureHandle handle) = 0;

        virtual SamplerHandle CreateSampler(const SamplerDesc &desc) = 0;
        virtual void DestroySampler(SamplerHandle handle) = 0;

        virtual ShaderModuleHandle CreateShaderModule(std::span<const uint32_t> spirv,
                                                      ShaderStage stage,
                                                      const char *entry_point = "main") = 0;
        virtual void DestroyShaderModule(ShaderModuleHandle handle) = 0;

        virtual DescriptorSetLayoutHandle CreateDescriptorSetLayout(
            std::span<const DescriptorBinding> bindings) = 0;
        virtual void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle) = 0;

        // Descriptor sets are allocated per-frame by default (auto-reset on
        // EndFrame). For long-lived bindless arrays use a "persistent" set.
        virtual DescriptorSetHandle AllocateDescriptorSet(DescriptorSetLayoutHandle layout,
                                                          bool persistent = false) = 0;

        // Bind resources to a descriptor set. Multiple writes can be batched
        // via repeated calls before the set is used.
        virtual void WriteBufferDescriptor(DescriptorSetHandle set,
                                            uint32_t binding,
                                            BufferHandle buffer,
                                            uint64_t offset = 0,
                                            uint64_t range = ~0ull) = 0;
        virtual void WriteTextureDescriptor(DescriptorSetHandle set,
                                             uint32_t binding,
                                             TextureHandle texture,
                                             SamplerHandle sampler = {},
                                             uint32_t array_index = 0) = 0;
        virtual void WriteStorageImageDescriptor(DescriptorSetHandle set,
                                                  uint32_t binding,
                                                  TextureHandle texture,
                                                  uint32_t array_index = 0,
                                                  uint32_t mip = 0) = 0;

        virtual PipelineHandle CreateComputePipeline(const ComputePipelineDesc &desc) = 0;
        virtual void DestroyPipeline(PipelineHandle handle) = 0;
    };

    class CommandList
    {
    public:
        virtual ~CommandList() = default;

        // ---- Compute ------------------------------------------------------
        virtual void BindComputePipeline(PipelineHandle pipeline) = 0;
        virtual void BindDescriptorSet(PipelineHandle pipeline,
                                        uint32_t set_index,
                                        DescriptorSetHandle set) = 0;
        virtual void PushConstants(PipelineHandle pipeline,
                                    uint32_t offset,
                                    uint32_t size,
                                    const void *data) = 0;
        virtual void Dispatch(uint32_t x, uint32_t y, uint32_t z) = 0;

        // ---- Barriers / sync ---------------------------------------------
        virtual void BarrierImage(const ImageBarrier &barrier) = 0;
        virtual void BarrierImages(std::span<const ImageBarrier> barriers) = 0;
        virtual void BarrierBuffer(const BufferBarrier &barrier) = 0;

        // ---- Copies ------------------------------------------------------
        virtual void CopyTexture(TextureHandle src, TextureHandle dst,
                                  uint32_t width, uint32_t height,
                                  uint32_t base_layer = 0, uint32_t layer_count = 1,
                                  uint32_t mip = 0) = 0;
        virtual void ClearTexture(TextureHandle texture,
                                   const float color[4],
                                   uint32_t base_mip = 0,
                                   uint32_t mip_count = 1,
                                   uint32_t base_layer = 0,
                                   uint32_t layer_count = ~0u) = 0;

        // ---- Debug -------------------------------------------------------
        virtual void BeginDebugLabel(const char *name) = 0;
        virtual void EndDebugLabel() = 0;
    };

} // namespace hybrid::renderer::rhi

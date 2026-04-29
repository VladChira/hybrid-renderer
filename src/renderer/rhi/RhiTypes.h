#pragma once

#include <cstdint>

// Backend-agnostic types shared between the OpenGL and Vulkan RHI surfaces.
// This header contains zero implementation. The opaque handles are
// strongly-typed wrappers around a 64-bit id; backends interpret them.
//
// The vocabulary is deliberately Vulkan-shaped (image layouts, explicit
// barriers, descriptor sets). The OpenGL backend simulates the Vulkan
// semantics where needed.

namespace hybrid::renderer::rhi
{

    // ---- Opaque handles ----------------------------------------------------

    template <typename Tag>
    struct Handle
    {
        uint64_t id = 0;
        bool IsValid() const { return id != 0; }
        bool operator==(const Handle &other) const { return id == other.id; }
        bool operator!=(const Handle &other) const { return id != other.id; }
    };

    struct BufferTag {};
    struct TextureTag {};
    struct SamplerTag {};
    struct ShaderModuleTag {};
    struct PipelineTag {};
    struct DescriptorSetLayoutTag {};
    struct DescriptorSetTag {};

    using BufferHandle              = Handle<BufferTag>;
    using TextureHandle             = Handle<TextureTag>;
    using SamplerHandle             = Handle<SamplerTag>;
    using ShaderModuleHandle        = Handle<ShaderModuleTag>;
    using PipelineHandle            = Handle<PipelineTag>;
    using DescriptorSetLayoutHandle = Handle<DescriptorSetLayoutTag>;
    using DescriptorSetHandle       = Handle<DescriptorSetTag>;

    // ---- Enums -------------------------------------------------------------

    enum class Format : uint16_t
    {
        Unknown = 0,
        // Color
        R8_UNorm,
        R16_SFloat,
        R32_UInt,
        RG16_SFloat,
        RGBA8_UNorm,
        RGBA8_SRGB,
        RGBA16_SFloat,
        RGBA32_SFloat,
        // Depth
        D24_UNorm_S8_UInt,
        D32_SFloat,
    };

    enum class TextureType : uint8_t
    {
        Tex2D,
        Tex2DArray,
        TexCube,
        Tex3D,
    };

    // Bitfield. Backends translate to GL_TEXTURE_USAGE_*/VkImageUsageFlags.
    enum class TextureUsage : uint32_t
    {
        None        = 0,
        Sampled     = 1u << 0,  // sample as a texture in a shader
        Storage     = 1u << 1,  // image2D etc — bound as image, not texture
        ColorAttach = 1u << 2,  // render-target color
        DepthAttach = 1u << 3,  // render-target depth
        TransferSrc = 1u << 4,  // copyable from
        TransferDst = 1u << 5,  // copyable to
    };
    inline TextureUsage operator|(TextureUsage a, TextureUsage b)
    {
        return static_cast<TextureUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool operator&(TextureUsage a, TextureUsage b)
    {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0u;
    }

    enum class BufferUsage : uint32_t
    {
        None     = 0,
        Vertex   = 1u << 0,
        Index    = 1u << 1,
        Uniform  = 1u << 2,
        Storage  = 1u << 3,  // SSBO
        Indirect = 1u << 4,
        TransferSrc = 1u << 5,
        TransferDst = 1u << 6,
    };
    inline BufferUsage operator|(BufferUsage a, BufferUsage b)
    {
        return static_cast<BufferUsage>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
    }
    inline bool operator&(BufferUsage a, BufferUsage b)
    {
        return (static_cast<uint32_t>(a) & static_cast<uint32_t>(b)) != 0u;
    }

    enum class MemoryUsage : uint8_t
    {
        DeviceLocal,    // GPU only
        HostVisible,    // CPU writeable, GPU read (uniforms)
        Staging,        // CPU upload buffer, transferred to DeviceLocal
        Readback,       // GPU writes, CPU reads
    };

    // Image layout tracked by the RHI on each TextureHandle. The backend
    // inserts the necessary transitions when the layout changes.
    enum class ImageLayout : uint8_t
    {
        Undefined,
        General,            // storage image read+write
        ShaderRead,         // sampled / read-only
        ColorAttachment,
        DepthAttachment,
        TransferSrc,
        TransferDst,
        Present,
    };

    enum class SamplerFilter : uint8_t  { Nearest, Linear };
    enum class SamplerAddress : uint8_t { Repeat, ClampToEdge, MirroredRepeat };

    struct SamplerDesc
    {
        SamplerFilter min_filter = SamplerFilter::Linear;
        SamplerFilter mag_filter = SamplerFilter::Linear;
        SamplerFilter mip_filter = SamplerFilter::Linear;
        SamplerAddress address_u = SamplerAddress::ClampToEdge;
        SamplerAddress address_v = SamplerAddress::ClampToEdge;
        SamplerAddress address_w = SamplerAddress::ClampToEdge;
        float max_anisotropy = 0.0f;
    };

    enum class ShaderStage : uint8_t
    {
        Vertex,
        Fragment,
        Compute,
    };

    // ---- Descriptor models -------------------------------------------------

    enum class DescriptorType : uint8_t
    {
        UniformBuffer,
        StorageBuffer,
        SampledTexture,
        StorageTexture,
        Sampler,
        CombinedImageSampler,
    };

    struct DescriptorBinding
    {
        uint32_t binding = 0;
        DescriptorType type = DescriptorType::UniformBuffer;
        uint32_t count = 1;            // > 1 for descriptor arrays (bindless)
        uint32_t stage_mask = 0;       // OR of (1 << ShaderStage)
    };

    // ---- Pipeline descriptors ---------------------------------------------

    struct ComputePipelineDesc
    {
        ShaderModuleHandle module;
        const char *entry_point = "main";
        const DescriptorSetLayoutHandle *set_layouts = nullptr;
        uint32_t set_layout_count = 0;
        uint32_t push_constant_size = 0;
    };

    // (Graphics pipeline desc will land in a later phase — Phase 3.)

    // ---- Resource descriptors ----------------------------------------------

    struct BufferDesc
    {
        uint64_t size = 0;
        BufferUsage usage = BufferUsage::None;
        MemoryUsage memory = MemoryUsage::DeviceLocal;
        const char *debug_name = nullptr;
    };

    struct TextureDesc
    {
        TextureType type = TextureType::Tex2D;
        Format format = Format::Unknown;
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t depth_or_layers = 1;     // depth for 3D, layers for 2DArray/Cube
        uint32_t mip_levels = 1;
        TextureUsage usage = TextureUsage::None;
        const char *debug_name = nullptr;
    };

    // ---- Barriers ----------------------------------------------------------

    struct ImageBarrier
    {
        TextureHandle texture;
        ImageLayout from = ImageLayout::Undefined;
        ImageLayout to   = ImageLayout::Undefined;
        uint32_t base_mip = 0;
        uint32_t mip_count = 1;
        uint32_t base_layer = 0;
        uint32_t layer_count = 1;
    };

    struct BufferBarrier
    {
        BufferHandle buffer;
        // Coarse access transition. For a renderer this size we don't need
        // fine-grained read/write masks — read-after-write across stages is
        // the only case we model.
        bool was_written = true;
    };

} // namespace hybrid::renderer::rhi

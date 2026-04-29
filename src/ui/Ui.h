#pragma once

#include "platform/PlatformEvents.h"
#include "ui/Dockspace.h"
#include "ui/panels/Panel.h"
#include "themes/Themes.h"
#include "ui/UiCommands.h"
#include "ui/UiState.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_impl_glfw.h"
#ifdef HYBRID_RHI_OPENGL
#include "imgui_impl_opengl3.h"
#endif

#ifdef HYBRID_RHI_VULKAN
#include <vulkan/vulkan.h>
// VmaAllocator / VmaAllocation are opaque-pointer typedefs (VK_DEFINE_HANDLE
// inside vk_mem_alloc.h). Forward-declare them here to keep the VMA header
// out of the public Ui interface — the implementation pulls it in via Ui.cpp.
struct VmaAllocator_T;
typedef struct VmaAllocator_T *VmaAllocator;
struct VmaAllocation_T;
typedef struct VmaAllocation_T *VmaAllocation;
#endif

#include <memory>
#include <string>
#include <vector>

namespace hybrid::ui
{

    struct UiConfig
    {
        std::string glsl_version = "#version 330";
        ThemeKind theme = ThemeKind::Darkness;
    };

#ifdef HYBRID_RHI_VULKAN
    // Vulkan handles required to bring up ImGui's Vulkan backend. App.cpp
    // pulls these out of the Renderer's VulkanRenderBackend at startup and
    // passes them to InitVulkan.
    struct VulkanUiContext
    {
        VkInstance       instance         = VK_NULL_HANDLE;
        VkPhysicalDevice physical_device  = VK_NULL_HANDLE;
        VkDevice         device           = VK_NULL_HANDLE;
        uint32_t         queue_family     = 0;
        VkQueue          queue            = VK_NULL_HANDLE;
        VkFormat         color_format     = VK_FORMAT_UNDEFINED;
        uint32_t         min_image_count  = 2;
        uint32_t         image_count      = 2;
        // For UploadIconTexture: staging + device-local images go through
        // VMA, the registered ImGui descriptors reuse the renderer's
        // offscreen sampler.
        VmaAllocator     allocator        = nullptr;
        VkSampler        default_sampler  = VK_NULL_HANDLE;
    };
#endif

    class Ui
    {
    public:
        bool Init(const UiConfig &config, const platform::NativeWindowHandle &window_handle);
#ifdef HYBRID_RHI_VULKAN
        bool InitVulkan(const UiConfig &config,
                        const platform::NativeWindowHandle &window_handle,
                        const VulkanUiContext &vulkan);
        // Records ImGui draws into `cmd`. Called by Renderer's UiRenderHook
        // inside a vkCmdBeginRendering scope.
        void RenderImGuiInto(VkCommandBuffer cmd);

        // Registers (sampler, view) with ImGui's Vulkan backend so the
        // texture can be sampled inside ImGui::Image. Returns the handle
        // cast to uint64_t (fits ImTextureID and UiState's
        // viewport_color_texture). Caller must Unregister on resize/
        // shutdown — view handles change when the underlying image is
        // recreated. Returns 0 on failure or if Ui isn't initialised.
        uint64_t RegisterVulkanTexture(VkSampler sampler, VkImageView view);
        void UnregisterVulkanTexture(uint64_t handle);

        // Uploads an RGBA8 pixel buffer into a device-local VkImage and
        // returns a registered ImTextureID (cast to uint64_t). Owns the
        // image / view / VMA allocation / descriptor set internally;
        // everything is freed at Shutdown. Used by panels (e.g.
        // ToolbarPanel) that load icons from disk. Synchronous — does a
        // one-shot vkQueueSubmit + vkQueueWaitIdle. Returns 0 on failure.
        uint64_t UploadIconTexture(const uint8_t *rgba, int width, int height);
#endif
        void Shutdown();

        void RegisterPanel(std::unique_ptr<Panel> panel, DockTarget target = DockTarget::Main);
        void ClearPanels();
        void SetDockspaceLayout(const DockspaceLayout &layout);
        void ResetDockspaceLayout();

        CommandBuffer Frame(float delta_seconds, const UiState &state);

    private:
        bool InitCommon(const UiConfig &config);
        void RegisterDefaultPanels();

        void *m_window = nullptr;
        bool m_initialized = false;
#ifdef HYBRID_RHI_VULKAN
        struct UploadedIcon
        {
            VkImage         image;
            VkImageView     view;
            VmaAllocation   allocation;
            VkDescriptorSet descriptor_set;
        };
        bool m_using_vulkan = false;
        VkDevice         m_vk_device          = VK_NULL_HANDLE;
        VkDescriptorPool m_vk_imgui_pool      = VK_NULL_HANDLE;
        VmaAllocator     m_vk_allocator       = nullptr;
        VkSampler        m_vk_default_sampler = VK_NULL_HANDLE;
        VkQueue          m_vk_queue           = VK_NULL_HANDLE;
        uint32_t         m_vk_queue_family    = 0;
        std::vector<UploadedIcon> m_vk_icons;
#endif
        UiConfig m_config{};
        DockspaceLayout m_layout{};
        Dockspace m_dockspace{};
        PanelRegistry m_panels{};
        ThemeKind m_theme = ThemeKind::Darkness;
        ThemePalette m_theme_palette{};
        UiSelection m_selection{};
        UiViewportVisualization m_viewport_visualization = UiViewportVisualization::FinalColor;
        TransformTool m_transform_tool = TransformTool::Translate;
        std::vector<UiMaterialEntry> m_material_entries{};

        ImGuiIO *io = nullptr;
    };

} // namespace hybrid::ui

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
        bool m_using_vulkan = false;
        VkDevice         m_vk_device       = VK_NULL_HANDLE;
        VkDescriptorPool m_vk_imgui_pool   = VK_NULL_HANDLE;
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

#include "ui/Ui.h"

#include "core/Log.h"
#include "core/ResourceMonitor.h"
#include "core/Profiling.h"
#include "graphics/GraphicsRuntime.h"
#include "ui/UiStateUtils.h"
#include "themes/Themes.h"
#include "panels/ContentBrowserPanel.h"
#include "panels/ConsolePanel.h"
#include "panels/MaterialsPanel.h"
#include "panels/PropertiesPanel.h"
#include "panels/PerformancePanel.h"
#include "panels/RenderTargetsPanel.h"
#include "panels/SceneHierarchyPanel.h"
#include "panels/SettingsPanel.h"
#include "panels/ToolbarPanel.h"
#include "panels/ViewportPanel.h"

#ifdef HYBRID_RHI_OPENGL
#include <glad.h>
#endif

#include <utility>

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <implot.h>
#include <backends/imgui_impl_glfw.h>
#ifdef HYBRID_RHI_OPENGL
#include <backends/imgui_impl_opengl3.h>
#endif
#ifdef HYBRID_RHI_VULKAN
#include <backends/imgui_impl_vulkan.h>
#endif

#include <ImGuizmo.h>

namespace hybrid::ui
{

    bool Ui::InitCommon(const UiConfig &config)
    {
        LOG_INFO("Initializing ImGui...");

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImPlot::CreateContext();

        io = &ImGui::GetIO();
        io->ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        const std::string font_path = std::string(HYBRID_PROJECT_ROOT) + "/assets/fonts/DMSans-Regular.ttf";
        io->Fonts->AddFontFromFileTTF(font_path.c_str(), 18.0);

        LOG_INFO("Default theme is " + ThemeKindToString(config.theme));
        ApplyTheme(config.theme);

        m_config = config;
        m_layout = DockspaceLayout::Default();
        m_theme = config.theme;
        m_theme_palette = BuildThemePalette(m_theme);
        return true;
    }

    void Ui::RegisterDefaultPanels()
    {
        if (m_panels.Empty())
        {
#if defined(HYBRID_RHI_OPENGL)
            // ToolbarPanel loads GL textures for its icons. Phase 7-stage-2
            // will port it to ImGui_ImplVulkan_AddTexture; for now skip it
            // on the Vulkan path so we don't crash on null gl* function
            // pointers.
            RegisterPanel(std::make_unique<ToolbarPanel>(), DockTarget::Top);
#endif
            RegisterPanel(std::make_unique<SceneHierarchyPanel>(), DockTarget::RightTop);
            RegisterPanel(std::make_unique<PropertiesPanel>(), DockTarget::RightBottom);
            RegisterPanel(std::make_unique<MaterialsPanel>(), DockTarget::BottomLeft);
            RegisterPanel(std::make_unique<ContentBrowserPanel>(), DockTarget::RightTop);
            RegisterPanel(std::make_unique<ConsolePanel>(), DockTarget::BottomRight);
            RegisterPanel(std::make_unique<SettingsPanel>(), DockTarget::LeftTop);
            RegisterPanel(std::make_unique<RenderTargetsPanel>(), DockTarget::LeftTop);
            RegisterPanel(std::make_unique<ViewportPanel>(), DockTarget::Main);
            RegisterPanel(std::make_unique<PerformancePanel>(), DockTarget::LeftBottom);
        }
    }

    bool Ui::Init(const UiConfig &config, const platform::NativeWindowHandle &window_handle)
    {
        if (m_initialized)
        {
            return true;
        }

        auto *window = static_cast<GLFWwindow *>(window_handle.window);
        if (!window)
        {
            return false;
        }

#ifdef HYBRID_RHI_OPENGL
        if (glfwGetCurrentContext() == nullptr)
        {
            LOG_ERROR("UI init failed: no current OpenGL context");
            return false;
        }

        if (!graphics::EnsureOpenGLInitialized(reinterpret_cast<GLADloadproc>(glfwGetProcAddress)))
        {
            LOG_ERROR("UI init failed: OpenGL runtime initialization failed");
            return false;
        }

        if (!InitCommon(config)) return false;

        if (!ImGui_ImplGlfw_InitForOpenGL(window, true))
        {
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        if (!ImGui_ImplOpenGL3_Init(config.glsl_version.c_str()))
        {
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        LOG_INFO("ImGui initialized (OpenGL backend)");

        m_window = window;
        core::ResourceMonitor::Init();
        RegisterDefaultPanels();
        m_initialized = true;
        return true;
#else
        (void)window;
        (void)config;
        LOG_ERROR("Ui::Init called in non-OpenGL build; use InitVulkan");
        return false;
#endif
    }

#ifdef HYBRID_RHI_VULKAN
    bool Ui::InitVulkan(const UiConfig &config,
                        const platform::NativeWindowHandle &window_handle,
                        const VulkanUiContext &vulkan)
    {
        if (m_initialized)
        {
            return true;
        }

        auto *window = static_cast<GLFWwindow *>(window_handle.window);
        if (!window)
        {
            LOG_ERROR("Ui::InitVulkan missing window handle");
            return false;
        }

        if (!InitCommon(config)) return false;

        if (!ImGui_ImplGlfw_InitForVulkan(window, true))
        {
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        // Descriptor pool for ImGui's font texture and any user textures
        // registered via ImGui_ImplVulkan_AddTexture (Phase 7-stage-2 will
        // use these for the ViewportPanel). 1000 entries is what ImGui's
        // example uses.
        VkDescriptorPoolSize pool_size{};
        pool_size.type            = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        pool_size.descriptorCount = 1000;
        VkDescriptorPoolCreateInfo pool_info{};
        pool_info.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        pool_info.flags         = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
        pool_info.maxSets       = 1000;
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes    = &pool_size;
        if (vkCreateDescriptorPool(vulkan.device, &pool_info, nullptr, &m_vk_imgui_pool) != VK_SUCCESS)
        {
            LOG_ERROR("Ui::InitVulkan: vkCreateDescriptorPool failed");
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        ImGui_ImplVulkan_InitInfo init_info{};
        init_info.ApiVersion          = VK_API_VERSION_1_3;
        init_info.Instance            = vulkan.instance;
        init_info.PhysicalDevice      = vulkan.physical_device;
        init_info.Device              = vulkan.device;
        init_info.QueueFamily         = vulkan.queue_family;
        init_info.Queue               = vulkan.queue;
        init_info.DescriptorPool      = m_vk_imgui_pool;
        init_info.MinImageCount       = vulkan.min_image_count;
        init_info.ImageCount          = vulkan.image_count;
        init_info.UseDynamicRendering = true;

        VkPipelineRenderingCreateInfoKHR pipeline_rendering{};
        pipeline_rendering.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO;
        pipeline_rendering.colorAttachmentCount    = 1;
        pipeline_rendering.pColorAttachmentFormats = &vulkan.color_format;
        init_info.PipelineInfoMain.PipelineRenderingCreateInfo = pipeline_rendering;

        if (!ImGui_ImplVulkan_Init(&init_info))
        {
            LOG_ERROR("Ui::InitVulkan: ImGui_ImplVulkan_Init failed");
            vkDestroyDescriptorPool(vulkan.device, m_vk_imgui_pool, nullptr);
            m_vk_imgui_pool = VK_NULL_HANDLE;
            ImGui_ImplGlfw_Shutdown();
            ImPlot::DestroyContext();
            ImGui::DestroyContext();
            return false;
        }

        LOG_INFO("ImGui initialized (Vulkan backend)");

        m_window         = window;
        m_using_vulkan   = true;
        m_vk_device      = vulkan.device;
        core::ResourceMonitor::Init();
        RegisterDefaultPanels();
        m_initialized = true;
        return true;
    }

    void Ui::RenderImGuiInto(VkCommandBuffer cmd)
    {
        if (!m_initialized || !m_using_vulkan) return;
        ImDrawData *draw_data = ImGui::GetDrawData();
        if (draw_data != nullptr)
        {
            ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
        }
    }
#endif

    void Ui::Shutdown()
    {
        if (!m_initialized)
        {
            return;
        }

        LOG_WARN("UI module shutting down...");

#if defined(HYBRID_RHI_VULKAN)
        ImGui_ImplVulkan_Shutdown();
        if (m_vk_imgui_pool != VK_NULL_HANDLE)
        {
            vkDestroyDescriptorPool(m_vk_device, m_vk_imgui_pool, nullptr);
            m_vk_imgui_pool = VK_NULL_HANDLE;
        }
        m_vk_device    = VK_NULL_HANDLE;
        m_using_vulkan = false;
#elif defined(HYBRID_RHI_OPENGL)
        ImGui_ImplOpenGL3_Shutdown();
#endif
        ImGui_ImplGlfw_Shutdown();
        core::ResourceMonitor::Shutdown();
        ImPlot::DestroyContext();
        ImGui::DestroyContext();

        m_window = nullptr;
        m_initialized = false;
    }

    CommandBuffer Ui::Frame(float delta_seconds, const UiState &state)
    {
        HYBRID_PROFILE_ZONE_N("Ui::Frame");
        CommandBuffer commands;
        if (!m_initialized || !m_window)
        {
            return commands;
        }

#if defined(HYBRID_RHI_VULKAN)
        ImGui_ImplVulkan_NewFrame();
#elif defined(HYBRID_RHI_OPENGL)
        ImGui_ImplOpenGL3_NewFrame();
#endif
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        ImGuizmo::BeginFrame();

        {
            HYBRID_PROFILE_ZONE_N("Ui::Frame::Dockspace");
            m_dockspace.BeginFrame();
            m_dockspace.BuildLayout(m_layout);
        }

        UiState frame_state = state;
        BuildMaterialEntries(frame_state.scene_world, m_material_entries);
        frame_state.materials = &m_material_entries;
        frame_state.viewport_visualization = m_viewport_visualization;

        PanelContext context{};
        context.delta_seconds = delta_seconds;
        context.commands = &commands;
        context.theme = &m_theme_palette;
        context.state = &frame_state;
        context.selection = &m_selection;
        context.viewport_visualization = &m_viewport_visualization;
        context.transform_tool = &m_transform_tool;
        {
            HYBRID_PROFILE_ZONE_N("Ui::Frame::Panels");
            m_panels.DrawAll(context);
        }

        {
            HYBRID_PROFILE_ZONE_N("Ui::Frame::ImGuiRender");
            ImGui::Render();
        }

        // GL backend submits draws inline; Vulkan backend defers actual
        // command-buffer recording to RenderImGuiInto, called by the
        // renderer's UiRenderHook inside its dynamic-rendering scope.
#if defined(HYBRID_RHI_OPENGL)
        {
            int fb_width = 0;
            int fb_height = 0;
            glfwGetFramebufferSize(static_cast<GLFWwindow *>(m_window), &fb_width, &fb_height);
            HYBRID_PROFILE_ZONE_N("Ui::Frame::BackendSubmit");
            glViewport(0, 0, fb_width, fb_height);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        }
#endif

        return commands;
    }

    void Ui::RegisterPanel(std::unique_ptr<Panel> panel, DockTarget target)
    {
        const std::string title = panel->Title();
        LOG_INFO("Registering panel " + title);
        m_panels.Register(std::move(panel));
        for (auto it = m_layout.assignments.begin(); it != m_layout.assignments.end(); ++it)
        {
            if (it->panel_title == title)
            {
                m_layout.assignments.erase(it);
                break;
            }
        }
        m_layout.assignments.push_back({title, target});
        m_dockspace.ResetLayout();
    }

    void Ui::ClearPanels()
    {
        m_panels.Clear();
        m_layout.assignments.clear();
        m_dockspace.ResetLayout();
    }

    void Ui::SetDockspaceLayout(const DockspaceLayout &layout)
    {
        m_layout = layout;
        m_dockspace.ResetLayout();
    }

    void Ui::ResetDockspaceLayout()
    {
        m_dockspace.ResetLayout();
    }

} // namespace hybrid::ui

#include "renderer/vulkan/VulkanInstance.h"

#include <GLFW/glfw3.h>

#include <cstring>

namespace hybrid::renderer::vulkan
{

    namespace
    {
        constexpr const char *kValidationLayer = "VK_LAYER_KHRONOS_validation";

        VKAPI_ATTR VkBool32 VKAPI_CALL DebugCallback(
            VkDebugUtilsMessageSeverityFlagBitsEXT severity,
            VkDebugUtilsMessageTypeFlagsEXT /*type*/,
            const VkDebugUtilsMessengerCallbackDataEXT *data,
            void * /*user_data*/)
        {
            if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
            {
                LOG_ERROR("[vulkan/validation] {}", data->pMessage);
            }
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
            {
                LOG_WARN("[vulkan/validation] {}", data->pMessage);
            }
            else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT)
            {
                LOG_INFO("[vulkan/validation] {}", data->pMessage);
            }
            return VK_FALSE;
        }

        bool HasLayer(const std::vector<VkLayerProperties> &available, const char *name)
        {
            for (const auto &lp : available)
            {
                if (std::strcmp(lp.layerName, name) == 0) return true;
            }
            return false;
        }

        bool HasExtension(const std::vector<VkExtensionProperties> &available, const char *name)
        {
            for (const auto &ep : available)
            {
                if (std::strcmp(ep.extensionName, name) == 0) return true;
            }
            return false;
        }
    } // namespace

    bool Instance::Create(const InstanceConfig &config)
    {
        // ---- Probe layers -----------------------------------------------
        uint32_t layer_count = 0;
        vkEnumerateInstanceLayerProperties(&layer_count, nullptr);
        std::vector<VkLayerProperties> available_layers(layer_count);
        vkEnumerateInstanceLayerProperties(&layer_count, available_layers.data());

        if (config.enable_validation && HasLayer(available_layers, kValidationLayer))
        {
            m_enabled_layers.push_back(kValidationLayer);
            m_validation_enabled = true;
        }

        // ---- Probe extensions ------------------------------------------
        uint32_t inst_ext_count = 0;
        vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, nullptr);
        std::vector<VkExtensionProperties> available_exts(inst_ext_count);
        vkEnumerateInstanceExtensionProperties(nullptr, &inst_ext_count, available_exts.data());

        // GLFW required extensions for surface creation.
        uint32_t glfw_ext_count = 0;
        const char **glfw_exts = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
        for (uint32_t i = 0; i < glfw_ext_count; ++i)
        {
            m_enabled_extensions.push_back(glfw_exts[i]);
        }

        if (m_validation_enabled &&
            HasExtension(available_exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME))
        {
            m_enabled_extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }

        // MoltenVK / portability subset support.
        if (config.require_macos_portability &&
            HasExtension(available_exts, VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            m_enabled_extensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
            m_portability_enabled = true;
        }

        VkApplicationInfo app_info{};
        app_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
        app_info.pApplicationName = config.app_name;
        app_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.pEngineName = "HybridRenderer";
        app_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
        app_info.apiVersion = VK_API_VERSION_1_3;

        VkInstanceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
        create_info.pApplicationInfo = &app_info;
        create_info.enabledLayerCount = static_cast<uint32_t>(m_enabled_layers.size());
        create_info.ppEnabledLayerNames = m_enabled_layers.data();
        create_info.enabledExtensionCount = static_cast<uint32_t>(m_enabled_extensions.size());
        create_info.ppEnabledExtensionNames = m_enabled_extensions.data();
        if (m_portability_enabled)
        {
            create_info.flags |= VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
        }

        // Dump what we're asking for — useful for surface-creation
        // EXTENSION_NOT_PRESENT failures, where the instance creates fine
        // but a downstream call wants a platform-surface extension we
        // forgot to enable.
        for (uint32_t i = 0; i < glfw_ext_count; ++i)
        {
            LOG_INFO("[vulkan] GLFW required ext: {}", glfw_exts[i]);
        }
        for (const char *name : m_enabled_extensions)
        {
            LOG_INFO("[vulkan] enabling instance ext: {}", name);
        }

        if (!HYBRID_VK_CHECK(vkCreateInstance(&create_info, nullptr, &m_instance)))
        {
            return false;
        }

        if (m_validation_enabled)
        {
            CreateDebugMessenger();
        }

        LOG_INFO("[vulkan] instance created (validation={}, portability={})",
                 m_validation_enabled, m_portability_enabled);
        return true;
    }

    void Instance::Destroy()
    {
        DestroyDebugMessenger();
        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
    }

    bool Instance::CreateDebugMessenger()
    {
        auto fn = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (!fn) return false;

        VkDebugUtilsMessengerCreateInfoEXT info{};
        info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        info.messageSeverity =
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        info.messageType =
            VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
            VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        info.pfnUserCallback = DebugCallback;

        return HYBRID_VK_CHECK(fn(m_instance, &info, nullptr, &m_debug_messenger));
    }

    void Instance::DestroyDebugMessenger()
    {
        if (m_debug_messenger == VK_NULL_HANDLE) return;
        auto fn = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (fn) fn(m_instance, m_debug_messenger, nullptr);
        m_debug_messenger = VK_NULL_HANDLE;
    }

    VkSurfaceKHR Instance::CreateSurface(GLFWwindow *window) const
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        VkResult result = glfwCreateWindowSurface(m_instance, window, nullptr, &surface);
        if (!VkCheck(result, "glfwCreateWindowSurface"))
        {
            return VK_NULL_HANDLE;
        }
        return surface;
    }

    void Instance::DestroySurface(VkSurfaceKHR surface) const
    {
        if (surface != VK_NULL_HANDLE)
        {
            vkDestroySurfaceKHR(m_instance, surface, nullptr);
        }
    }

} // namespace hybrid::renderer::vulkan

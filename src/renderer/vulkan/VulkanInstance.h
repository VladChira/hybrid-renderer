#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include <vector>

struct GLFWwindow;

namespace hybrid::renderer::vulkan
{

    struct InstanceConfig
    {
        const char *app_name = "HybridRenderer";
        bool enable_validation = true;   // turn off in shipping builds
        bool require_macos_portability = true; // MoltenVK requires VK_KHR_portability_enumeration
    };

    class Instance
    {
    public:
        bool Create(const InstanceConfig &config);
        void Destroy();

        VkInstance Handle() const { return m_instance; }
        bool ValidationEnabled() const { return m_validation_enabled; }

        VkSurfaceKHR CreateSurface(GLFWwindow *window) const;
        void DestroySurface(VkSurfaceKHR surface) const;

    private:
        bool CreateDebugMessenger();
        void DestroyDebugMessenger();

        VkInstance m_instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT m_debug_messenger = VK_NULL_HANDLE;
        bool m_validation_enabled = false;
        bool m_portability_enabled = false;
        std::vector<const char *> m_enabled_layers;
        std::vector<const char *> m_enabled_extensions;
    };

} // namespace hybrid::renderer::vulkan

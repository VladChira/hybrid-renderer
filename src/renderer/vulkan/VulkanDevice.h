#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include <optional>
#include <vector>

namespace hybrid::renderer::vulkan
{

    class Instance;

    struct QueueFamilyIndices
    {
        std::optional<uint32_t> graphics;
        std::optional<uint32_t> present;
        bool IsComplete() const { return graphics.has_value() && present.has_value(); }
    };

    class Device
    {
    public:
        bool Create(const Instance &instance, VkSurfaceKHR surface);
        void Destroy();
        void WaitIdle();

        VkPhysicalDevice Physical() const { return m_physical; }
        VkDevice Logical() const { return m_device; }

        VkQueue GraphicsQueue() const { return m_graphics_queue; }
        VkQueue PresentQueue() const { return m_present_queue; }
        const QueueFamilyIndices &Queues() const { return m_queues; }

        // Cached limits / features for callers that need them.
        const VkPhysicalDeviceProperties &Properties() const { return m_properties; }
        const VkPhysicalDeviceFeatures &Features() const { return m_features; }
        bool DescriptorIndexingEnabled() const { return m_descriptor_indexing_enabled; }

    private:
        struct Candidate
        {
            VkPhysicalDevice device = VK_NULL_HANDLE;
            QueueFamilyIndices queues{};
            VkPhysicalDeviceProperties props{};
            int score = 0;
        };

        std::vector<Candidate> EnumerateCandidates(const Instance &instance,
                                                    VkSurfaceKHR surface) const;
        QueueFamilyIndices FindQueueFamilies(VkPhysicalDevice device,
                                              VkSurfaceKHR surface) const;
        bool DeviceSupportsRequiredExtensions(VkPhysicalDevice device,
                                               std::vector<const char *> &out_enabled) const;

        VkPhysicalDevice m_physical = VK_NULL_HANDLE;
        VkDevice m_device = VK_NULL_HANDLE;
        QueueFamilyIndices m_queues{};
        VkQueue m_graphics_queue = VK_NULL_HANDLE;
        VkQueue m_present_queue = VK_NULL_HANDLE;

        VkPhysicalDeviceProperties m_properties{};
        VkPhysicalDeviceFeatures m_features{};
        bool m_descriptor_indexing_enabled = false;
    };

} // namespace hybrid::renderer::vulkan

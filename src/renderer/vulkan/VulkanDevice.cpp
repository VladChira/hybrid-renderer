#include "renderer/vulkan/VulkanDevice.h"

#include "renderer/vulkan/VulkanInstance.h"

#include <cstring>
#include <set>

namespace hybrid::renderer::vulkan
{

    namespace
    {
        // Required device extensions. VK_KHR_portability_subset is mandatory
        // when present (MoltenVK exposes it). VK_KHR_swapchain is the rest of
        // what we actually use.
        constexpr const char *kRequiredExtensions[] = {
            VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        };

        bool HasExt(const std::vector<VkExtensionProperties> &available, const char *name)
        {
            for (const auto &e : available)
            {
                if (std::strcmp(e.extensionName, name) == 0) return true;
            }
            return false;
        }

        int ScoreDevice(const VkPhysicalDeviceProperties &props)
        {
            switch (props.deviceType)
            {
            case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:   return 1000;
            case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return 500;
            case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:    return 100;
            case VK_PHYSICAL_DEVICE_TYPE_CPU:            return 10;
            default:                                     return 1;
            }
        }
    } // namespace

    QueueFamilyIndices Device::FindQueueFamilies(VkPhysicalDevice device,
                                                  VkSurfaceKHR surface) const
    {
        QueueFamilyIndices result{};

        uint32_t count = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
        std::vector<VkQueueFamilyProperties> families(count);
        vkGetPhysicalDeviceQueueFamilyProperties(device, &count, families.data());

        for (uint32_t i = 0; i < count; ++i)
        {
            if (families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            {
                result.graphics = i;
            }
            VkBool32 supports_present = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &supports_present);
            if (supports_present)
            {
                result.present = i;
            }
            if (result.IsComplete()) break;
        }
        return result;
    }

    bool Device::DeviceSupportsRequiredExtensions(VkPhysicalDevice device,
                                                   std::vector<const char *> &out_enabled) const
    {
        uint32_t count = 0;
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr);
        std::vector<VkExtensionProperties> available(count);
        vkEnumerateDeviceExtensionProperties(device, nullptr, &count, available.data());

        for (const char *req : kRequiredExtensions)
        {
            if (!HasExt(available, req)) return false;
            out_enabled.push_back(req);
        }
        // VK_KHR_portability_subset must be enabled when present (MoltenVK).
        if (HasExt(available, "VK_KHR_portability_subset"))
        {
            out_enabled.push_back("VK_KHR_portability_subset");
        }
        return true;
    }

    std::vector<Device::Candidate> Device::EnumerateCandidates(const Instance &instance,
                                                                VkSurfaceKHR surface) const
    {
        uint32_t count = 0;
        vkEnumeratePhysicalDevices(instance.Handle(), &count, nullptr);
        if (count == 0) return {};

        std::vector<VkPhysicalDevice> physicals(count);
        vkEnumeratePhysicalDevices(instance.Handle(), &count, physicals.data());

        std::vector<Candidate> out;
        out.reserve(count);
        for (VkPhysicalDevice p : physicals)
        {
            Candidate c{};
            c.device = p;
            c.queues = FindQueueFamilies(p, surface);
            vkGetPhysicalDeviceProperties(p, &c.props);
            c.score = ScoreDevice(c.props);
            if (!c.queues.IsComplete()) continue;

            std::vector<const char *> ignored;
            if (!DeviceSupportsRequiredExtensions(p, ignored)) continue;

            out.push_back(c);
        }
        return out;
    }

    bool Device::Create(const Instance &instance, VkSurfaceKHR surface)
    {
        auto candidates = EnumerateCandidates(instance, surface);
        if (candidates.empty())
        {
            LOG_ERROR("[vulkan] no suitable physical device found");
            return false;
        }

        // Pick highest score.
        const Candidate *best = &candidates[0];
        for (const auto &c : candidates)
        {
            if (c.score > best->score) best = &c;
        }
        m_physical = best->device;
        m_queues = best->queues;
        m_properties = best->props;
        vkGetPhysicalDeviceFeatures(m_physical, &m_features);

        LOG_INFO("[vulkan] picked GPU: {} (api {}.{}.{})",
                 m_properties.deviceName,
                 VK_API_VERSION_MAJOR(m_properties.apiVersion),
                 VK_API_VERSION_MINOR(m_properties.apiVersion),
                 VK_API_VERSION_PATCH(m_properties.apiVersion));

        std::vector<const char *> enabled_extensions;
        if (!DeviceSupportsRequiredExtensions(m_physical, enabled_extensions))
        {
            LOG_ERROR("[vulkan] picked device unexpectedly missing required extensions");
            return false;
        }

        // Probe descriptor indexing (1.2) and dynamic rendering (1.3).
        VkPhysicalDeviceVulkan13Features features13{};
        features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        VkPhysicalDeviceVulkan12Features features12{};
        features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        features12.pNext = &features13;
        VkPhysicalDeviceFeatures2 features2{};
        features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        features2.pNext = &features12;
        vkGetPhysicalDeviceFeatures2(m_physical, &features2);

        m_descriptor_indexing_enabled =
            features12.descriptorIndexing &&
            features12.shaderSampledImageArrayNonUniformIndexing &&
            features12.runtimeDescriptorArray;
        if (!m_descriptor_indexing_enabled)
        {
            LOG_WARN("[vulkan] descriptor indexing not fully supported; "
                     "bindless materials path will need a fallback");
        }
        if (!features13.dynamicRendering)
        {
            LOG_ERROR("[vulkan] dynamicRendering not supported by physical device; "
                      "ImGui Vulkan backend integration requires it");
            return false;
        }

        std::set<uint32_t> unique_families = {*m_queues.graphics, *m_queues.present};
        std::vector<VkDeviceQueueCreateInfo> queue_infos;
        const float priority = 1.0f;
        for (uint32_t family : unique_families)
        {
            VkDeviceQueueCreateInfo qi{};
            qi.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
            qi.queueFamilyIndex = family;
            qi.queueCount = 1;
            qi.pQueuePriorities = &priority;
            queue_infos.push_back(qi);
        }

        VkPhysicalDeviceVulkan13Features enabled13{};
        enabled13.sType            = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        enabled13.dynamicRendering = VK_TRUE;

        VkPhysicalDeviceVulkan12Features enabled12{};
        enabled12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        enabled12.pNext = &enabled13;
        enabled12.descriptorIndexing                            = features12.descriptorIndexing;
        enabled12.shaderSampledImageArrayNonUniformIndexing     = features12.shaderSampledImageArrayNonUniformIndexing;
        enabled12.runtimeDescriptorArray                        = features12.runtimeDescriptorArray;
        enabled12.descriptorBindingSampledImageUpdateAfterBind  = features12.descriptorBindingSampledImageUpdateAfterBind;
        enabled12.descriptorBindingPartiallyBound               = features12.descriptorBindingPartiallyBound;
        enabled12.descriptorBindingVariableDescriptorCount      = features12.descriptorBindingVariableDescriptorCount;
        enabled12.timelineSemaphore                             = features12.timelineSemaphore;

        VkPhysicalDeviceFeatures enabled_core{};
        enabled_core.shaderInt64 = m_features.shaderInt64;
        enabled_core.samplerAnisotropy = m_features.samplerAnisotropy;
        enabled_core.fillModeNonSolid = m_features.fillModeNonSolid;

        VkDeviceCreateInfo create_info{};
        create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        create_info.pNext = &enabled12;
        create_info.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
        create_info.pQueueCreateInfos = queue_infos.data();
        create_info.pEnabledFeatures = &enabled_core;
        create_info.enabledExtensionCount = static_cast<uint32_t>(enabled_extensions.size());
        create_info.ppEnabledExtensionNames = enabled_extensions.data();

        if (!HYBRID_VK_CHECK(vkCreateDevice(m_physical, &create_info, nullptr, &m_device)))
        {
            return false;
        }

        vkGetDeviceQueue(m_device, *m_queues.graphics, 0, &m_graphics_queue);
        vkGetDeviceQueue(m_device, *m_queues.present, 0, &m_present_queue);

        return true;
    }

    void Device::Destroy()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }
        m_physical = VK_NULL_HANDLE;
        m_graphics_queue = VK_NULL_HANDLE;
        m_present_queue = VK_NULL_HANDLE;
    }

    void Device::WaitIdle()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_device);
        }
    }

} // namespace hybrid::renderer::vulkan

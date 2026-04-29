#include "renderer/vulkan/VulkanSwapchain.h"

#include "renderer/vulkan/VulkanDevice.h"

#include <algorithm>

namespace hybrid::renderer::vulkan
{

    namespace
    {
        VkSurfaceFormatKHR PickFormat(const std::vector<VkSurfaceFormatKHR> &available)
        {
            for (const auto &f : available)
            {
                // Prefer SRGB to make the swapchain present-encoded; the
                // renderer composites into linear targets and tonemaps once.
                if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
                    f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
                {
                    return f;
                }
            }
            return available[0];
        }

        VkPresentModeKHR PickPresentMode(const std::vector<VkPresentModeKHR> &available,
                                          bool prefer_mailbox)
        {
            if (prefer_mailbox)
            {
                for (auto m : available)
                {
                    if (m == VK_PRESENT_MODE_MAILBOX_KHR) return m;
                }
            }
            return VK_PRESENT_MODE_FIFO_KHR; // always available
        }

        VkExtent2D PickExtent(const VkSurfaceCapabilitiesKHR &caps,
                              uint32_t width, uint32_t height)
        {
            if (caps.currentExtent.width != UINT32_MAX)
            {
                return caps.currentExtent;
            }
            VkExtent2D actual = {width, height};
            actual.width  = std::clamp(actual.width,
                                       caps.minImageExtent.width,
                                       caps.maxImageExtent.width);
            actual.height = std::clamp(actual.height,
                                       caps.minImageExtent.height,
                                       caps.maxImageExtent.height);
            return actual;
        }
    } // namespace

    bool Swapchain::Create(const Device &device, const SwapchainConfig &config)
    {
        m_surface = config.surface;
        m_prefer_mailbox = config.prefer_mailbox;

        VkSurfaceCapabilitiesKHR caps{};
        vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device.Physical(), m_surface, &caps);

        uint32_t fmt_count = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.Physical(), m_surface, &fmt_count, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(fmt_count);
        vkGetPhysicalDeviceSurfaceFormatsKHR(device.Physical(), m_surface, &fmt_count, formats.data());

        uint32_t pm_count = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(device.Physical(), m_surface, &pm_count, nullptr);
        std::vector<VkPresentModeKHR> present_modes(pm_count);
        vkGetPhysicalDeviceSurfacePresentModesKHR(device.Physical(), m_surface, &pm_count, present_modes.data());

        if (formats.empty() || present_modes.empty())
        {
            LOG_ERROR("[vulkan] surface has no formats or no present modes");
            return false;
        }

        m_format       = PickFormat(formats);
        m_present_mode = PickPresentMode(present_modes, m_prefer_mailbox);
        m_extent       = PickExtent(caps, config.width, config.height);

        // Triple-buffer when allowed.
        uint32_t image_count = caps.minImageCount + 1;
        if (caps.maxImageCount > 0 && image_count > caps.maxImageCount)
        {
            image_count = caps.maxImageCount;
        }

        VkSwapchainCreateInfoKHR ci{};
        ci.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
        ci.surface = m_surface;
        ci.minImageCount = image_count;
        ci.imageFormat = m_format.format;
        ci.imageColorSpace = m_format.colorSpace;
        ci.imageExtent = m_extent;
        ci.imageArrayLayers = 1;
        ci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT |
                        VK_IMAGE_USAGE_TRANSFER_DST_BIT;

        const auto &queues = device.Queues();
        uint32_t shared_indices[2] = {*queues.graphics, *queues.present};
        if (queues.graphics != queues.present)
        {
            ci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
            ci.queueFamilyIndexCount = 2;
            ci.pQueueFamilyIndices = shared_indices;
        }
        else
        {
            ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
        }

        ci.preTransform = caps.currentTransform;
        ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        ci.presentMode = m_present_mode;
        ci.clipped = VK_TRUE;
        ci.oldSwapchain = VK_NULL_HANDLE;

        if (!HYBRID_VK_CHECK(vkCreateSwapchainKHR(device.Logical(), &ci, nullptr, &m_swapchain)))
        {
            return false;
        }

        uint32_t actual_image_count = 0;
        vkGetSwapchainImagesKHR(device.Logical(), m_swapchain, &actual_image_count, nullptr);
        m_images.resize(actual_image_count);
        vkGetSwapchainImagesKHR(device.Logical(), m_swapchain, &actual_image_count, m_images.data());

        m_image_views.resize(actual_image_count);
        for (uint32_t i = 0; i < actual_image_count; ++i)
        {
            VkImageViewCreateInfo vi{};
            vi.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            vi.image = m_images[i];
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = m_format.format;
            vi.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            vi.subresourceRange.baseMipLevel = 0;
            vi.subresourceRange.levelCount = 1;
            vi.subresourceRange.baseArrayLayer = 0;
            vi.subresourceRange.layerCount = 1;
            if (!HYBRID_VK_CHECK(vkCreateImageView(device.Logical(), &vi, nullptr, &m_image_views[i])))
            {
                return false;
            }
        }

        LOG_INFO("[vulkan] swapchain {}x{} {} images, present={}",
                 m_extent.width, m_extent.height, actual_image_count,
                 m_present_mode == VK_PRESENT_MODE_MAILBOX_KHR ? "mailbox" : "fifo");
        return true;
    }

    void Swapchain::Destroy(const Device &device)
    {
        DestroyImageViews(device);
        if (m_swapchain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(device.Logical(), m_swapchain, nullptr);
            m_swapchain = VK_NULL_HANDLE;
        }
        m_images.clear();
        m_surface = VK_NULL_HANDLE;
    }

    void Swapchain::DestroyImageViews(const Device &device)
    {
        for (VkImageView v : m_image_views)
        {
            if (v != VK_NULL_HANDLE) vkDestroyImageView(device.Logical(), v, nullptr);
        }
        m_image_views.clear();
    }

    bool Swapchain::Recreate(const Device &device, uint32_t width, uint32_t height)
    {
        VkSurfaceKHR surface = m_surface;
        const bool prefer_mailbox = m_prefer_mailbox;
        Destroy(device);
        SwapchainConfig cfg{surface, width, height, prefer_mailbox};
        return Create(device, cfg);
    }

} // namespace hybrid::renderer::vulkan

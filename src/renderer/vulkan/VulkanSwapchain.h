#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include <vector>

namespace hybrid::renderer::vulkan
{

    class Device;

    struct SwapchainConfig
    {
        VkSurfaceKHR surface = VK_NULL_HANDLE;
        uint32_t width = 0;
        uint32_t height = 0;
        bool prefer_mailbox = true;
    };

    class Swapchain
    {
    public:
        bool Create(const Device &device, const SwapchainConfig &config);
        void Destroy(const Device &device);

        // Recreate after a resize / out-of-date result. Old resources are
        // destroyed; caller must vkDeviceWaitIdle first if necessary.
        bool Recreate(const Device &device, uint32_t width, uint32_t height);

        VkSwapchainKHR Handle() const { return m_swapchain; }
        VkFormat ColorFormat() const { return m_format.format; }
        VkExtent2D Extent() const { return m_extent; }
        uint32_t ImageCount() const { return static_cast<uint32_t>(m_images.size()); }
        VkImage Image(uint32_t i) const { return m_images[i]; }
        VkImageView ImageView(uint32_t i) const { return m_image_views[i]; }

    private:
        void DestroyImageViews(const Device &device);

        VkSurfaceKHR m_surface = VK_NULL_HANDLE;
        VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
        VkSurfaceFormatKHR m_format{};
        VkPresentModeKHR m_present_mode = VK_PRESENT_MODE_FIFO_KHR;
        VkExtent2D m_extent{};
        std::vector<VkImage> m_images;
        std::vector<VkImageView> m_image_views;
        bool m_prefer_mailbox = true;
    };

} // namespace hybrid::renderer::vulkan

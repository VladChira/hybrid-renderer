#pragma once

// All Vulkan TUs in this directory go through this header for the include
// + a few small utilities. Keeps the rest of the code free of platform
// boilerplate.

#include <vulkan/vulkan.h>

#include "core/Log.h"

#include <string>

namespace hybrid::renderer::vulkan
{

    const char *VkResultToString(VkResult result);

    // Logs the failure and returns false if result is not VK_SUCCESS.
    inline bool VkCheck(VkResult result, const char *what)
    {
        if (result == VK_SUCCESS)
        {
            return true;
        }
        LOG_ERROR("[vulkan] {} failed: {}", what, VkResultToString(result));
        return false;
    }

#define HYBRID_VK_CHECK(expr) ::hybrid::renderer::vulkan::VkCheck((expr), #expr)

} // namespace hybrid::renderer::vulkan

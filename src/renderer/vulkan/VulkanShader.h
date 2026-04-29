#pragma once

#include "renderer/vulkan/VulkanCommon.h"

#include <string>
#include <vector>

namespace hybrid::renderer::vulkan
{

    // Reads a SPIR-V binary from disk. Path is resolved relative to
    // HYBRID_SHADER_BIN_DIR (set by CMake when the Vulkan path is built).
    // Returns an empty vector on failure (logs the error).
    std::vector<uint32_t> LoadSpirv(const std::string &relative_path);

    // Wraps SPIR-V bytecode into a VkShaderModule. Returns VK_NULL_HANDLE on
    // failure.
    VkShaderModule CreateShaderModule(VkDevice device,
                                       const std::vector<uint32_t> &spirv);

} // namespace hybrid::renderer::vulkan

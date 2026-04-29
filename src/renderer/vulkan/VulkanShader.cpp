#include "renderer/vulkan/VulkanShader.h"

#include <cstdio>
#include <filesystem>

namespace hybrid::renderer::vulkan
{

    namespace
    {
        std::string ResolveShaderPath(const std::string &rel)
        {
#ifdef HYBRID_SHADER_BIN_DIR
            return std::string(HYBRID_SHADER_BIN_DIR) + "/" + rel;
#else
            return rel;
#endif
        }
    } // namespace

    std::vector<uint32_t> LoadSpirv(const std::string &relative_path)
    {
        const std::string full_path = ResolveShaderPath(relative_path);

        std::error_code ec;
        const uintmax_t size = std::filesystem::file_size(full_path, ec);
        if (ec)
        {
            LOG_ERROR("[vulkan/shader] file_size({}) failed: {}",
                      full_path, ec.message());
            return {};
        }
        if (size == 0 || (size % 4) != 0)
        {
            LOG_ERROR("[vulkan/shader] '{}' is not a valid SPIR-V file (size={})",
                      full_path, size);
            return {};
        }

        std::vector<uint32_t> bytecode(size / 4);
        FILE *f = std::fopen(full_path.c_str(), "rb");
        if (!f)
        {
            LOG_ERROR("[vulkan/shader] fopen('{}') failed", full_path);
            return {};
        }
        const size_t read = std::fread(bytecode.data(), 1, size, f);
        std::fclose(f);
        if (read != size)
        {
            LOG_ERROR("[vulkan/shader] fread('{}') short: got {} of {}",
                      full_path, read, size);
            return {};
        }
        return bytecode;
    }

    VkShaderModule CreateShaderModule(VkDevice device,
                                       const std::vector<uint32_t> &spirv)
    {
        if (spirv.empty()) return VK_NULL_HANDLE;

        VkShaderModuleCreateInfo info{};
        info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        info.codeSize = spirv.size() * sizeof(uint32_t);
        info.pCode = spirv.data();

        VkShaderModule module = VK_NULL_HANDLE;
        if (!HYBRID_VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module)))
        {
            return VK_NULL_HANDLE;
        }
        return module;
    }

} // namespace hybrid::renderer::vulkan

#include "assets/StbImageLoader.h"

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <limits>

namespace hybrid::assets
{

    namespace
    {
        bool EqualsIgnoreCase(std::string_view lhs, std::string_view rhs)
        {
            if (lhs.size() != rhs.size())
            {
                return false;
            }
            for (size_t i = 0; i < lhs.size(); ++i)
            {
                const char left = lhs[i];
                const char right = rhs[i];
                if (left == right)
                {
                    continue;
                }
                if (left >= 'A' && left <= 'Z')
                {
                    if (static_cast<char>(left - 'A' + 'a') != right)
                    {
                        return false;
                    }
                }
                else if (right >= 'A' && right <= 'Z')
                {
                    if (static_cast<char>(right - 'A' + 'a') != left)
                    {
                        return false;
                    }
                }
                else
                {
                    return false;
                }
            }
            return true;
        }
    } // namespace

    std::type_index StbImageLoader::Type() const
    {
        return std::type_index(typeid(ImageAsset));
    }

    bool StbImageLoader::SupportsExtension(std::string_view extension) const
    {
        for (const auto ext : kExtensions)
        {
            if (EqualsIgnoreCase(extension, ext))
            {
                return true;
            }
        }
        return false;
    }

    std::shared_ptr<void> StbImageLoader::Load(const AssetLoadRequest &request, IAssetDataSource *data_source)
    {
        if (!data_source)
        {
            return {};
        }

        std::vector<std::byte> bytes;
        if (!data_source->ReadAllBytes(request.path, bytes))
        {
            return {};
        }
        if (bytes.empty())
        {
            return {};
        }
        if (bytes.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
        {
            return {};
        }

        const auto *data = reinterpret_cast<const stbi_uc *>(bytes.data());
        const int length = static_cast<int>(bytes.size());

        int width = 0;
        int height = 0;
        int channels = 0;

        auto image = std::make_shared<ImageAsset>();
        if (stbi_is_hdr_from_memory(data, length))
        {
            image->is_hdr = true;
            float *pixels = stbi_loadf_from_memory(data, length, &width, &height, &channels, 0);
            if (!pixels)
            {
                return {};
            }

            image->width = width;
            image->height = height;
            image->channels = channels;

            const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
            image->pixels_f32.assign(pixels, pixels + count);
            stbi_image_free(pixels);
        }
        else
        {
            stbi_uc *pixels = stbi_load_from_memory(data, length, &width, &height, &channels, 0);
            if (!pixels)
            {
                return {};
            }

            image->width = width;
            image->height = height;
            image->channels = channels;

            const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * static_cast<size_t>(channels);
            image->pixels.assign(pixels, pixels + count);
            stbi_image_free(pixels);
        }

        if (!image->IsValid())
        {
            return {};
        }

        return image;
    }

} // namespace hybrid::assets

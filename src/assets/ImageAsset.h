#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace hybrid::assets
{

    struct ImageAsset
    {
        int width = 0;
        int height = 0;
        int channels = 0;
        bool is_hdr = false;

        std::vector<uint8_t> pixels;
        std::vector<float> pixels_f32;

        bool IsValid() const
        {
            if (width <= 0 || height <= 0 || channels <= 0)
            {
                return false;
            }
            if (is_hdr)
            {
                return !pixels_f32.empty();
            }
            return !pixels.empty();
        }

        size_t SizeInBytes() const
        {
            return is_hdr ? pixels_f32.size() * sizeof(float) : pixels.size() * sizeof(uint8_t);
        }
    };

} // namespace hybrid::assets

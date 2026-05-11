#include "core/FrameCaptureExporter.h"

#include "core/Log.h"

#include <glad.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <random>
#include <string>
#include <vector>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

namespace hybrid::core
{
    namespace
    {
        constexpr uint32_t kNoEntityId = std::numeric_limits<uint32_t>::max();

        enum class ExportReadbackKind
        {
            ColorRgba,
            SingleChannelRed,
            Depth,
            EntityId
        };

        struct ExportEntry
        {
            const char *base_name = "";
            renderer::GlTextureId texture = 0;
            ExportReadbackKind kind = ExportReadbackKind::ColorRgba;
            bool split_rgb_and_a = false;
        };

        std::string GenerateRandomId()
        {
            std::random_device random_device{};
            std::mt19937_64 generator(random_device());
            std::uniform_int_distribution<uint64_t> distribution(
                0u,
                std::numeric_limits<uint64_t>::max());

            constexpr char kHexDigits[] = "0123456789abcdef";
            const uint64_t value = distribution(generator);

            std::string id(16, '0');
            for (size_t index = 0; index < id.size(); ++index)
            {
                const uint32_t nibble = static_cast<uint32_t>((value >> ((id.size() - 1 - index) * 4)) & 0xfu);
                id[index] = kHexDigits[nibble];
            }
            return id;
        }

        std::filesystem::path CreateExportDirectory()
        {
            const std::filesystem::path export_root = std::filesystem::path(HYBRID_PROJECT_ROOT) / "export";
            std::error_code error{};
            std::filesystem::create_directories(export_root, error);
            if (error)
            {
                LOG_ERROR("[FrameCaptureExporter] Failed to create export root '{}': {}",
                          export_root.string(),
                          error.message());
                return {};
            }

            for (int attempt = 0; attempt < 8; ++attempt)
            {
                const std::filesystem::path candidate = export_root / GenerateRandomId();
                if (std::filesystem::create_directory(candidate, error))
                {
                    return candidate;
                }

                if (error)
                {
                    LOG_ERROR("[FrameCaptureExporter] Failed to create export directory '{}': {}",
                              candidate.string(),
                              error.message());
                    return {};
                }
            }

            LOG_ERROR("[FrameCaptureExporter] Failed to allocate a unique export directory under '{}'",
                      export_root.string());
            return {};
        }

        bool PrepareReadbackFramebuffer(GLuint texture,
                                        GLenum attachment,
                                        GLenum textarget,
                                        GLuint &out_framebuffer,
                                        GLint &out_previous_read_framebuffer,
                                        GLint &out_previous_draw_framebuffer,
                                        GLint &out_previous_pack_alignment)
        {
            out_framebuffer = 0;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &out_previous_read_framebuffer);
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &out_previous_draw_framebuffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &out_previous_pack_alignment);

            glGenFramebuffers(1, &out_framebuffer);
            if (out_framebuffer == 0)
            {
                return false;
            }

            glBindFramebuffer(GL_FRAMEBUFFER, out_framebuffer);
            glFramebufferTexture2D(GL_FRAMEBUFFER, attachment, textarget, texture, 0);
            if (attachment == GL_DEPTH_ATTACHMENT)
            {
                glDrawBuffer(GL_NONE);
                glReadBuffer(GL_NONE);
            }
            else
            {
                glDrawBuffer(GL_COLOR_ATTACHMENT0);
                glReadBuffer(GL_COLOR_ATTACHMENT0);
            }

            if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
            {
                glDeleteFramebuffers(1, &out_framebuffer);
                out_framebuffer = 0;
                glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(out_previous_read_framebuffer));
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(out_previous_draw_framebuffer));
                return false;
            }

            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            return true;
        }

        void RestoreReadbackFramebuffer(GLuint framebuffer,
                                        GLint previous_read_framebuffer,
                                        GLint previous_draw_framebuffer,
                                        GLint previous_pack_alignment)
        {
            if (framebuffer != 0)
            {
                glDeleteFramebuffers(1, &framebuffer);
            }
            glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previous_read_framebuffer));
            glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previous_draw_framebuffer));
            glPixelStorei(GL_PACK_ALIGNMENT, previous_pack_alignment);
        }

        bool ReadColorTextureRgba(GLuint texture,
                                  const renderer::RenderExtent &extent,
                                  std::vector<uint8_t> &out_pixels)
        {
            if (texture == 0 || !extent.IsValid())
            {
                return false;
            }

            GLuint framebuffer = 0;
            GLint previous_read_framebuffer = 0;
            GLint previous_draw_framebuffer = 0;
            GLint previous_pack_alignment = 4;
            if (!PrepareReadbackFramebuffer(texture,
                                            GL_COLOR_ATTACHMENT0,
                                            GL_TEXTURE_2D,
                                            framebuffer,
                                            previous_read_framebuffer,
                                            previous_draw_framebuffer,
                                            previous_pack_alignment))
            {
                return false;
            }

            out_pixels.resize(static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height) * 4u);
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(extent.width),
                         static_cast<GLsizei>(extent.height),
                         GL_RGBA,
                         GL_UNSIGNED_BYTE,
                         out_pixels.data());

            const bool ok = glGetError() == GL_NO_ERROR;
            RestoreReadbackFramebuffer(framebuffer,
                                       previous_read_framebuffer,
                                       previous_draw_framebuffer,
                                       previous_pack_alignment);
            return ok;
        }

        bool ReadSingleChannelTexture(GLuint texture,
                                      const renderer::RenderExtent &extent,
                                      std::vector<uint8_t> &out_pixels)
        {
            if (texture == 0 || !extent.IsValid())
            {
                return false;
            }

            GLuint framebuffer = 0;
            GLint previous_read_framebuffer = 0;
            GLint previous_draw_framebuffer = 0;
            GLint previous_pack_alignment = 4;
            if (!PrepareReadbackFramebuffer(texture,
                                            GL_COLOR_ATTACHMENT0,
                                            GL_TEXTURE_2D,
                                            framebuffer,
                                            previous_read_framebuffer,
                                            previous_draw_framebuffer,
                                            previous_pack_alignment))
            {
                return false;
            }

            out_pixels.resize(static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height));
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(extent.width),
                         static_cast<GLsizei>(extent.height),
                         GL_RED,
                         GL_UNSIGNED_BYTE,
                         out_pixels.data());

            const bool ok = glGetError() == GL_NO_ERROR;
            RestoreReadbackFramebuffer(framebuffer,
                                       previous_read_framebuffer,
                                       previous_draw_framebuffer,
                                       previous_pack_alignment);
            return ok;
        }

        bool ReadDepthTexture(GLuint texture,
                              const renderer::RenderExtent &extent,
                              std::vector<float> &out_pixels)
        {
            if (texture == 0 || !extent.IsValid())
            {
                return false;
            }

            GLuint framebuffer = 0;
            GLint previous_read_framebuffer = 0;
            GLint previous_draw_framebuffer = 0;
            GLint previous_pack_alignment = 4;
            if (!PrepareReadbackFramebuffer(texture,
                                            GL_DEPTH_ATTACHMENT,
                                            GL_TEXTURE_2D,
                                            framebuffer,
                                            previous_read_framebuffer,
                                            previous_draw_framebuffer,
                                            previous_pack_alignment))
            {
                return false;
            }

            out_pixels.resize(static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height));
            glReadBuffer(GL_NONE);
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(extent.width),
                         static_cast<GLsizei>(extent.height),
                         GL_DEPTH_COMPONENT,
                         GL_FLOAT,
                         out_pixels.data());

            const bool ok = glGetError() == GL_NO_ERROR;
            RestoreReadbackFramebuffer(framebuffer,
                                       previous_read_framebuffer,
                                       previous_draw_framebuffer,
                                       previous_pack_alignment);
            return ok;
        }

        bool ReadEntityIdTexture(GLuint texture,
                                 const renderer::RenderExtent &extent,
                                 std::vector<uint32_t> &out_pixels)
        {
            if (texture == 0 || !extent.IsValid())
            {
                return false;
            }

            GLuint framebuffer = 0;
            GLint previous_read_framebuffer = 0;
            GLint previous_draw_framebuffer = 0;
            GLint previous_pack_alignment = 4;
            if (!PrepareReadbackFramebuffer(texture,
                                            GL_COLOR_ATTACHMENT0,
                                            GL_TEXTURE_2D,
                                            framebuffer,
                                            previous_read_framebuffer,
                                            previous_draw_framebuffer,
                                            previous_pack_alignment))
            {
                return false;
            }

            out_pixels.resize(static_cast<size_t>(extent.width) * static_cast<size_t>(extent.height));
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(0,
                         0,
                         static_cast<GLsizei>(extent.width),
                         static_cast<GLsizei>(extent.height),
                         GL_RED_INTEGER,
                         GL_UNSIGNED_INT,
                         out_pixels.data());

            const bool ok = glGetError() == GL_NO_ERROR;
            RestoreReadbackFramebuffer(framebuffer,
                                       previous_read_framebuffer,
                                       previous_draw_framebuffer,
                                       previous_pack_alignment);
            return ok;
        }

        void FlipImageRows(std::vector<uint8_t> &pixels, uint32_t width, uint32_t height, uint32_t channels)
        {
            if (pixels.empty() || width == 0 || height <= 1 || channels == 0)
            {
                return;
            }

            const size_t stride = static_cast<size_t>(width) * channels;
            std::vector<uint8_t> row_buffer(stride);
            for (uint32_t row = 0; row < height / 2; ++row)
            {
                uint8_t *top = pixels.data() + static_cast<size_t>(row) * stride;
                uint8_t *bottom = pixels.data() + static_cast<size_t>(height - 1 - row) * stride;
                std::copy(top, top + stride, row_buffer.begin());
                std::copy(bottom, bottom + stride, top);
                std::copy(row_buffer.begin(), row_buffer.end(), bottom);
            }
        }

        std::vector<uint8_t> ConvertRgbaToRgb(const std::vector<uint8_t> &rgba_pixels)
        {
            std::vector<uint8_t> rgb_pixels(rgba_pixels.size() / 4u * 3u);
            for (size_t source_index = 0, destination_index = 0;
                 source_index + 3 < rgba_pixels.size();
                 source_index += 4, destination_index += 3)
            {
                rgb_pixels[destination_index + 0] = rgba_pixels[source_index + 0];
                rgb_pixels[destination_index + 1] = rgba_pixels[source_index + 1];
                rgb_pixels[destination_index + 2] = rgba_pixels[source_index + 2];
            }
            return rgb_pixels;
        }

        std::vector<uint8_t> ConvertAlphaToRgb(const std::vector<uint8_t> &rgba_pixels)
        {
            std::vector<uint8_t> grayscale_pixels(rgba_pixels.size() / 4u * 3u);
            for (size_t source_index = 0, destination_index = 0;
                 source_index + 3 < rgba_pixels.size();
                 source_index += 4, destination_index += 3)
            {
                const uint8_t value = rgba_pixels[source_index + 3];
                grayscale_pixels[destination_index + 0] = value;
                grayscale_pixels[destination_index + 1] = value;
                grayscale_pixels[destination_index + 2] = value;
            }
            return grayscale_pixels;
        }

        std::vector<uint8_t> ConvertSingleChannelToRgb(const std::vector<uint8_t> &channel_pixels)
        {
            std::vector<uint8_t> rgb_pixels(channel_pixels.size() * 3u);
            for (size_t source_index = 0, destination_index = 0;
                 source_index < channel_pixels.size();
                 ++source_index, destination_index += 3)
            {
                const uint8_t value = channel_pixels[source_index];
                rgb_pixels[destination_index + 0] = value;
                rgb_pixels[destination_index + 1] = value;
                rgb_pixels[destination_index + 2] = value;
            }
            return rgb_pixels;
        }

        std::vector<uint8_t> ConvertDepthToRgb(const std::vector<float> &depth_pixels,
                                               float near_plane,
                                               float far_plane)
        {
            std::vector<uint8_t> rgb_pixels(depth_pixels.size() * 3u);
            for (size_t source_index = 0, destination_index = 0;
                 source_index < depth_pixels.size();
                 ++source_index, destination_index += 3)
            {
                const float depth_sample = std::clamp(depth_pixels[source_index], 0.0f, 1.0f);
                const float z_ndc = depth_sample * 2.0f - 1.0f;
                const float denominator =
                    far_plane + near_plane - z_ndc * (far_plane - near_plane);
                const float linear_depth =
                    (2.0f * near_plane * far_plane) / std::max(denominator, 1e-5f);
                const float normalized_depth =
                    std::clamp((linear_depth - near_plane) / std::max(far_plane - near_plane, 1e-5f),
                               0.0f,
                               1.0f);
                const uint8_t value = static_cast<uint8_t>(normalized_depth * 255.0f);
                rgb_pixels[destination_index + 0] = value;
                rgb_pixels[destination_index + 1] = value;
                rgb_pixels[destination_index + 2] = value;
            }
            return rgb_pixels;
        }

        std::vector<uint8_t> ConvertEntityIdsToRgb(const std::vector<uint32_t> &entity_ids)
        {
            uint32_t max_entity_id = 0;
            for (const uint32_t entity_id : entity_ids)
            {
                if (entity_id != kNoEntityId)
                {
                    max_entity_id = std::max(max_entity_id, entity_id);
                }
            }

            std::vector<uint8_t> rgb_pixels(entity_ids.size() * 3u, 0u);
            for (size_t source_index = 0, destination_index = 0;
                 source_index < entity_ids.size();
                 ++source_index, destination_index += 3)
            {
                uint8_t value = 0;
                if (const uint32_t entity_id = entity_ids[source_index];
                    entity_id != kNoEntityId && max_entity_id > 0)
                {
                    value = static_cast<uint8_t>((static_cast<uint64_t>(entity_id) * 255u) / max_entity_id);
                }

                rgb_pixels[destination_index + 0] = value;
                rgb_pixels[destination_index + 1] = value;
                rgb_pixels[destination_index + 2] = value;
            }
            return rgb_pixels;
        }

        bool WritePng(const std::filesystem::path &path,
                      uint32_t width,
                      uint32_t height,
                      const std::vector<uint8_t> &rgb_pixels)
        {
            if (rgb_pixels.empty())
            {
                return false;
            }

            const int result = stbi_write_png(path.string().c_str(),
                                              static_cast<int>(width),
                                              static_cast<int>(height),
                                              3,
                                              rgb_pixels.data(),
                                              static_cast<int>(width * 3u));
            return result != 0;
        }

        bool ExportRgbAndAlpha(const ExportEntry &entry,
                               const renderer::RenderExtent &extent,
                               const std::filesystem::path &directory,
                               uint32_t &files_written)
        {
            std::vector<uint8_t> rgba_pixels;
            if (!ReadColorTextureRgba(entry.texture, extent, rgba_pixels))
            {
                LOG_WARN("[FrameCaptureExporter] Failed to read texture '{}'", entry.base_name);
                return false;
            }

            std::vector<uint8_t> rgb_pixels = ConvertRgbaToRgb(rgba_pixels);
            std::vector<uint8_t> alpha_pixels = ConvertAlphaToRgb(rgba_pixels);
            FlipImageRows(rgb_pixels, extent.width, extent.height, 3);
            FlipImageRows(alpha_pixels, extent.width, extent.height, 3);

            const std::filesystem::path rgb_path = directory / (std::string(entry.base_name) + "_rgb.png");
            const std::filesystem::path alpha_path = directory / (std::string(entry.base_name) + "_a.png");

            if (!WritePng(rgb_path, extent.width, extent.height, rgb_pixels))
            {
                LOG_ERROR("[FrameCaptureExporter] Failed to write '{}'", rgb_path.string());
                return false;
            }
            ++files_written;

            if (!WritePng(alpha_path, extent.width, extent.height, alpha_pixels))
            {
                LOG_ERROR("[FrameCaptureExporter] Failed to write '{}'", alpha_path.string());
                return false;
            }
            ++files_written;
            return true;
        }

        bool ExportSingleTexture(const ExportEntry &entry,
                                 const renderer::RenderExtent &extent,
                                 const std::filesystem::path &directory,
                                 const renderer::RenderView &view,
                                 uint32_t &files_written)
        {
            std::vector<uint8_t> rgb_pixels;

            switch (entry.kind)
            {
            case ExportReadbackKind::ColorRgba:
            {
                std::vector<uint8_t> rgba_pixels;
                if (!ReadColorTextureRgba(entry.texture, extent, rgba_pixels))
                {
                    LOG_WARN("[FrameCaptureExporter] Failed to read texture '{}'", entry.base_name);
                    return false;
                }
                rgb_pixels = ConvertRgbaToRgb(rgba_pixels);
                break;
            }
            case ExportReadbackKind::SingleChannelRed:
            {
                std::vector<uint8_t> channel_pixels;
                if (!ReadSingleChannelTexture(entry.texture, extent, channel_pixels))
                {
                    LOG_WARN("[FrameCaptureExporter] Failed to read texture '{}'", entry.base_name);
                    return false;
                }
                rgb_pixels = ConvertSingleChannelToRgb(channel_pixels);
                break;
            }
            case ExportReadbackKind::Depth:
            {
                std::vector<float> depth_pixels;
                if (!ReadDepthTexture(entry.texture, extent, depth_pixels))
                {
                    LOG_WARN("[FrameCaptureExporter] Failed to read depth texture '{}'", entry.base_name);
                    return false;
                }
                rgb_pixels = ConvertDepthToRgb(depth_pixels, view.near_plane, view.far_plane);
                break;
            }
            case ExportReadbackKind::EntityId:
            {
                std::vector<uint32_t> entity_ids;
                if (!ReadEntityIdTexture(entry.texture, extent, entity_ids))
                {
                    LOG_WARN("[FrameCaptureExporter] Failed to read entity-id texture '{}'", entry.base_name);
                    return false;
                }
                rgb_pixels = ConvertEntityIdsToRgb(entity_ids);
                break;
            }
            }

            FlipImageRows(rgb_pixels, extent.width, extent.height, 3);
            const std::filesystem::path output_path = directory / (std::string(entry.base_name) + ".png");
            if (!WritePng(output_path, extent.width, extent.height, rgb_pixels))
            {
                LOG_ERROR("[FrameCaptureExporter] Failed to write '{}'", output_path.string());
                return false;
            }

            ++files_written;
            return true;
        }
    } // namespace

    FrameCaptureExportResult ExportFrameCapture(const renderer::RendererOutputs &outputs,
                                                const renderer::RenderExtent &extent,
                                                const renderer::RenderView &view)
    {
        FrameCaptureExportResult result{};
        if (!extent.IsValid())
        {
            LOG_WARN("[FrameCaptureExporter] Skipping frame export because the render extent is invalid");
            return result;
        }

        const std::filesystem::path directory = CreateExportDirectory();
        if (directory.empty())
        {
            return result;
        }

        result.directory = directory.string();

        const std::array<ExportEntry, 8> entries{{
            {"beauty", outputs.color, ExportReadbackKind::ColorRgba, false},
            {"final_color", outputs.color, ExportReadbackKind::ColorRgba, true},
            {"gbuffer_rt0", outputs.gbuffer_rt0, ExportReadbackKind::ColorRgba, true},
            {"gbuffer_rt1", outputs.gbuffer_rt1, ExportReadbackKind::ColorRgba, true},
            {"gbuffer_depth", outputs.depth, ExportReadbackKind::Depth, false},
            {"gbuffer_entity_id", outputs.gbuffer_entity_id, ExportReadbackKind::EntityId, false},
            {"bvh_traversal_heatmap", outputs.raytrace_heatmap, ExportReadbackKind::ColorRgba, true},
            {"rt_reflection_radiance", outputs.reflection_radiance, ExportReadbackKind::ColorRgba, true},
        }};

        for (const ExportEntry &entry : entries)
        {
            if (entry.texture == 0)
            {
                LOG_WARN("[FrameCaptureExporter] Skipping '{}' because the texture is unavailable this frame",
                         entry.base_name);
                continue;
            }

            if (entry.split_rgb_and_a)
            {
                ExportRgbAndAlpha(entry, extent, directory, result.files_written);
            }
            else
            {
                ExportSingleTexture(entry, extent, directory, view, result.files_written);
            }
        }

        const ExportEntry shadow_occlusion_entry{
            "rt_shadow_occlusion",
            outputs.shadow_debug_occlusion,
            ExportReadbackKind::SingleChannelRed,
            false};
        if (shadow_occlusion_entry.texture != 0)
        {
            ExportSingleTexture(shadow_occlusion_entry, extent, directory, view, result.files_written);
        }
        else
        {
            LOG_WARN("[FrameCaptureExporter] Skipping '{}' because the texture is unavailable this frame",
                     shadow_occlusion_entry.base_name);
        }

        result.success = result.files_written > 0;
        return result;
    }

} // namespace hybrid::core


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    basis_codec.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    9 Oct 26
//
//  Basis Universal stays inside the rendering DLL. Each call uses a fresh
//  compressor or transcoder; the rendering thread owns initialization.

#include "rendering/runtime/basis_codec.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

#include "debug/macros.hpp"
#include "types/fp16data_t.hpp"
#include "encoder/basisu_comp.h"
#include "transcoder/basisu_transcoder.h"

namespace rendering::basis_codec
{

static bool s_initialized = false;
static constexpr std::uint64_t k_max_texture_pixels = (16u * 1024u) * 1024u;
static constexpr std::size_t k_max_ktx2_bytes = (256u * 1024u) * 1024u;

static std::uint32_t bytes_per_pixel(const image::texture::EInputFormat format) noexcept
{
    using image::texture::EInputFormat;
    switch (format)
    {
        case EInputFormat::gray8: return 1u;
        case EInputFormat::rgba8: return 4u;
        case EInputFormat::rgba16f: return 8u;
        case EInputFormat::rgba32f: return 16u;
        default: return 0u;
    }
}

static bool read_hdr_component(const std::uint8_t* const source, const bool half, float& value) noexcept
{
    if (half)
    {
        std::uint16_t bits = 0u;
        std::memcpy((&bits), source, sizeof(bits));
        value = static_cast<float>(fp16data_t::fromBits(bits));
    }
    else
    {
        std::memcpy((&value), source, sizeof(value));
    }
    return std::isfinite(value) && (value >= 0.0f);
}

static bool read_hdr_scale(const basist::ktx2_transcoder& transcoder, float& scale) noexcept
{
    scale = 1.0f;
    const basisu::uint8_vec* const map_range = transcoder.find_key("KTXmapRange");
    if (map_range == nullptr)
    {
        return true;
    }
    if ((map_range->size() < 8u) || (map_range->size() > 12u))
    {
        return false;
    }
    for (std::size_t index = 8u; index < map_range->size(); ++index)
    {
        if ((*map_range)[index] != 0u)
        {
            return false;
        }
    }
    std::memcpy((&scale), map_range->data(), sizeof(scale));
    float offset = 0.0f;
    std::memcpy((&offset), (map_range->data() + sizeof(scale)), sizeof(offset));
    return std::isfinite(scale) && (scale > 0.0f) && (offset == 0.0f);
}

static bool describe_ktx2(const CByteConstView& bytes, image::texture::CEncodedDescription& description) noexcept
{
    if (!bytes.is_ready() || (bytes.size() > k_max_ktx2_bytes))
    {
        return false;
    }
    basist::ktx2_transcoder transcoder;
    if (!transcoder.init(bytes.data(), static_cast<std::uint32_t>(bytes.size())) ||
        (transcoder.get_faces() != 1u) || (transcoder.get_layers() != 0u))
    {
        return false;
    }
    const basist::basis_tex_format basis_format = transcoder.get_basis_tex_format();
    if (basis_format == basist::basis_tex_format::cUASTC_LDR_4x4)
    {
        description.encoding = image::texture::EEncoding::uastc_ldr_4x4;
    }
    else if (basis_format == basist::basis_tex_format::cUASTC_HDR_4x4)
    {
        description.encoding = image::texture::EEncoding::uastc_hdr_4x4;
    }
    else
    {
        return false;
    }
    description.width = transcoder.get_width();
    description.height = transcoder.get_height();
    description.levels = transcoder.get_levels();
    if ((description.width == 0u) || (description.height == 0u) || (description.levels == 0u) ||
        ((static_cast<std::uint64_t>(description.width) * description.height) > k_max_texture_pixels) ||
        (description.levels > 15u))
    {
        return false;
    }
    description.transfer = transcoder.is_srgb() ? image::texture::ETransfer::srgb : image::texture::ETransfer::linear;
    return read_hdr_scale(transcoder, description.hdr_scale);
}

bool initialise() noexcept
{
    if (s_initialized)
    {
        return true;
    }
    MV_REPORT_IMMEDIATE("Rendering Basis: initializing encoder and transcoder");
    s_initialized = basisu::basisu_encoder_init(false, false);
    MV_REPORT_IMMEDIATE("Rendering Basis: initialization %s", (s_initialized ? "succeeded" : "failed"));
    return s_initialized;
}

void shutdown() noexcept
{
    if (s_initialized)
    {
        basisu::basisu_encoder_deinit();
        s_initialized = false;
        MV_REPORT_IMMEDIATE("Rendering Basis: shut down");
    }
}

EStatus encode_ktx2(
    const image::texture::CInputView& input,
    const image::texture::CEncodeOptions& options,
    image::texture::CEncodedTexture& output) noexcept
{
    using namespace image::texture;
    output.buffer.deallocate();
    output.description = {};
    const std::uint32_t pixel_bytes = bytes_per_pixel(input.format);
    if (!s_initialized || !input.is_ready() || (pixel_bytes == 0u) ||
        ((input.bytes.row_width() % pixel_bytes) != 0u) || (options.max_pixels == 0u))
    {
        return EStatus::invalid_input;
    }
    const std::size_t width_size = input.bytes.row_width() / pixel_bytes;
    const std::size_t height_size = input.bytes.row_count();
    if ((width_size == 0u) || (height_size == 0u) ||
        (width_size > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION) ||
        (height_size > basist::BASISU_MAX_SUPPORTED_TEXTURE_DIMENSION) ||
        ((static_cast<std::uint64_t>(width_size) * height_size) > options.max_pixels) ||
        ((static_cast<std::uint64_t>(width_size) * height_size) > k_max_texture_pixels))
    {
        return EStatus::invalid_input;
    }
    const bool hdr = (options.encoding == EEncoding::uastc_hdr_4x4);
    if (((options.encoding != EEncoding::uastc_ldr_4x4) && !hdr) ||
        (hdr != ((input.format == EInputFormat::rgba16f) || (input.format == EInputFormat::rgba32f))) ||
        (hdr && (input.transfer != ETransfer::linear)) ||
        ((options.profile != EEncodeProfile::runtime_fast) && (options.profile != EEncodeProfile::build_quality)))
    {
        return EStatus::unsupported_format;
    }

    const std::uint32_t width = static_cast<std::uint32_t>(width_size);
    const std::uint32_t height = static_cast<std::uint32_t>(height_size);

    //  UASTC HDR 4x4 has no RDO quality knob; 100 avoids an upstream warning.
    const int quality = hdr ? 100 : ((options.profile == EEncodeProfile::build_quality) ? 100 : 80);
    const int effort = (options.profile == EEncodeProfile::build_quality) ? 10 : 1;
    std::uint32_t flags = basisu::cFlagKTX2;
    if (options.generate_mips)
    {
        flags |= basisu::cFlagGenMipsClamp;
    }
    if (input.transfer == ETransfer::srgb)
    {
        flags |= basisu::cFlagSRGB;
    }
    const basist::basis_tex_format mode = hdr ?
        basist::basis_tex_format::cUASTC_HDR_4x4 : basist::basis_tex_format::cUASTC_LDR_4x4;
    MV_REPORT_IMMEDIATE("Rendering Basis: encode begin %ux%u input %u mode %u quality %d effort %d mips %u",
        width, height, static_cast<unsigned int>(input.format), static_cast<unsigned int>(options.encoding),
        quality, effort, (options.generate_mips ? 1u : 0u));

    size_t encoded_size = 0u;
    void* encoded = nullptr;
    if (hdr)
    {
        basisu::vector<basisu::imagef> images(1);
        images[0].resize(width, height);
        const bool half = (input.format == EInputFormat::rgba16f);
        const std::uint32_t component_bytes = half ? 2u : 4u;
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const std::uint8_t* const row = input.bytes.row_data(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                float rgb[3]{};
                const std::uint8_t* const pixel = row + (static_cast<std::size_t>(x) * pixel_bytes);
                for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                {
                    if (!read_hdr_component((pixel + (channel * component_bytes)), half, rgb[channel]))
                    {
                        MV_REPORT_IMMEDIATE("Rendering Basis: HDR source rejected at %u,%u channel %u", x, y, channel);
                        return EStatus::invalid_input;
                    }
                }
                images[0](x, y).set(rgb[0], rgb[1], rgb[2], 1.0f);
            }
        }
        encoded = basisu::basis_compress2(mode, images, flags, quality, effort, (&encoded_size));
    }
    else
    {
        basisu::vector<basisu::image> images(1);
        images[0].resize(width, height);
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            const std::uint8_t* const row = input.bytes.row_data(y);
            for (std::uint32_t x = 0u; x < width; ++x)
            {
                const std::uint8_t* const pixel = row + (static_cast<std::size_t>(x) * pixel_bytes);
                if (input.format == EInputFormat::gray8)
                {
                    images[0](x, y).set_noclamp_rgba(pixel[0], pixel[0], pixel[0], 255);
                }
                else
                {
                    images[0](x, y).set_noclamp_rgba(pixel[0], pixel[1], pixel[2], pixel[3]);
                }
            }
        }
        encoded = basisu::basis_compress2(mode, images, flags, quality, effort, (&encoded_size));
    }
    if ((encoded == nullptr) || (encoded_size == 0u))
    {
        MV_REPORT_IMMEDIATE("Rendering Basis: encode failed");
        basisu::basis_free_data(encoded);
        return EStatus::codec_failed;
    }
    const bool allocated = output.buffer.resize(encoded_size, 16u);
    if (allocated)
    {
        std::memcpy(output.buffer.data(), encoded, encoded_size);
    }
    basisu::basis_free_data(encoded);
    if (!allocated)
    {
        MV_REPORT_IMMEDIATE("Rendering Basis: encoded buffer allocation failed (%zu bytes)", encoded_size);
        return EStatus::allocation_failed;
    }
    if (!describe_ktx2(output.buffer.const_view(), output.description))
    {
        output.buffer.deallocate();
        MV_REPORT_IMMEDIATE("Rendering Basis: encoded KTX2 metadata invalid");
        return EStatus::codec_failed;
    }
    MV_REPORT_IMMEDIATE("Rendering Basis: encode succeeded, %zu bytes, %u levels, HDR scale %f",
        encoded_size, output.description.levels, output.description.hdr_scale);
    return EStatus::success;
}

EStatus transcode_ktx2(
    const CByteConstView& input,
    const image::texture::EStorageFormat target,
    const std::uint32_t level,
    image::texture::CDecodedTexture& output) noexcept
{
    using namespace image::texture;
    output.buffer.deallocate();
    output.description = {};
    MV_REPORT_IMMEDIATE("Rendering Basis: transcode request source %zu bytes, level %u, target %u",
        input.size(), level, static_cast<unsigned int>(target));
    CEncodedDescription source;
    if (!s_initialized || !describe_ktx2(input, source) || (level >= source.levels))
    {
        return EStatus::invalid_input;
    }
    basist::transcoder_texture_format basis_target;
    switch (target)
    {
        case EStorageFormat::rgba8: basis_target = basist::transcoder_texture_format::cTFRGBA32; break;
        case EStorageFormat::rgba16f: basis_target = basist::transcoder_texture_format::cTFRGBA_HALF; break;
        case EStorageFormat::bc7_unorm:
        case EStorageFormat::bc7_srgb: basis_target = basist::transcoder_texture_format::cTFBC7_RGBA; break;
        case EStorageFormat::bc6h_ufloat: basis_target = basist::transcoder_texture_format::cTFBC6H; break;
        default: return EStatus::unsupported_format;
    }
    const bool hdr_source = (source.encoding == EEncoding::uastc_hdr_4x4);
    if ((hdr_source != ((target == EStorageFormat::rgba16f) || (target == EStorageFormat::bc6h_ufloat))) ||
        ((target == EStorageFormat::bc7_srgb) && (source.transfer != ETransfer::srgb)) ||
        ((target == EStorageFormat::bc7_unorm) && (source.transfer != ETransfer::linear)) ||
        !basist::basis_is_format_supported(basis_target, (hdr_source ?
            basist::basis_tex_format::cUASTC_HDR_4x4 : basist::basis_tex_format::cUASTC_LDR_4x4)))
    {
        return EStatus::unsupported_format;
    }
    basist::ktx2_transcoder transcoder;
    MV_REPORT_IMMEDIATE("Rendering Basis: transcode begin %ux%u level %u target %u source %zu bytes",
        source.width, source.height, level, static_cast<unsigned int>(target), input.size());
    if (!transcoder.init(input.data(), static_cast<std::uint32_t>(input.size())) ||
        !transcoder.start_transcoding())
    {
        MV_REPORT_IMMEDIATE("Rendering Basis: transcoder initialization failed");
        return EStatus::codec_failed;
    }
    basist::ktx2_image_level_info info;
    if (!transcoder.get_image_level_info(info, level, 0u, 0u))
    {
        return EStatus::codec_failed;
    }
    const std::uint32_t width = info.m_orig_width;
    const std::uint32_t height = info.m_orig_height;
    const std::uint32_t bytes = basist::basis_compute_transcoded_image_size_in_bytes(basis_target, width, height);
    const std::uint32_t unit_bytes = basist::basis_get_bytes_per_block_or_pixel(basis_target);
    if ((bytes == 0u) || (unit_bytes == 0u) || !output.buffer.resize(bytes, 16u))
    {
        MV_REPORT_IMMEDIATE("Rendering Basis: transcode buffer allocation failed (%u bytes)", bytes);
        return EStatus::allocation_failed;
    }
    if (!transcoder.transcode_image_level(level, 0u, 0u, output.buffer.data(), (bytes / unit_bytes), basis_target))
    {
        output.buffer.deallocate();
        MV_REPORT_IMMEDIATE("Rendering Basis: transcode failed");
        return EStatus::codec_failed;
    }
    const bool blocks = (target == EStorageFormat::bc7_unorm) || (target == EStorageFormat::bc7_srgb) ||
        (target == EStorageFormat::bc6h_ufloat);
    const std::uint32_t row_units = blocks ? ((width + 3u) / 4u) : width;
    output.description = { width, height, level, (row_units * unit_bytes), target, source.transfer, source.hdr_scale };
    MV_REPORT_IMMEDIATE("Rendering Basis: transcode succeeded, %u bytes, HDR scale %f", bytes, source.hdr_scale);
    return EStatus::success;
}

}   //  namespace rendering::basis_codec

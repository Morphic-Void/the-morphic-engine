
//
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   tga.cpp
//  Author: Ritchie Brannan
//  Date:   2 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  TGA encode and decode over byte-oriented image buffers.
//
//  This codec models TGA file layout and image format selection only.
//  It does not make the buffer view types themselves format-bearing.
//
//  -----------------------------------------------------------------------------
//  Supported TGA subset
//  -----------------------------------------------------------------------------
//
//  Encoded output
//  --------------
//  This implementation writes only the following TGA image forms:
//  - 8-bit greyscale
//  - 24-bit true-colour
//  - 32-bit true-colour
//  - 8-bit indexed colour with 24-bit CLUT entries
//  - 8-bit indexed colour with 32-bit CLUT entries
//
//  For each of the above, the encoder may write either:
//  - raw image data
//  - TGA RLE image data
//
//  The encoder attempts to construct the smallest file image that it
//  can given the supplied input data and encoding contraint options.
//
//  The encoder does not write:
//  - 15-bit true-colour
//  - 16-bit true-colour
//  - 15-bit CLUT entries
//  - 16-bit CLUT entries
//  - image ID fields
//  - extension areas
//  - developer areas
//  - TGA footer data
//
//  Decoded input
//  -------------
//  This implementation reads only the following TGA image forms:
//  - 8-bit greyscale
//  - 24-bit true-colour
//  - 32-bit true-colour
//  - 8-bit indexed colour with 24-bit CLUT entries
//  - 8-bit indexed colour with 32-bit CLUT entries
//
//  For each of the above, the decoder accepts either:
//  - raw image data
//  - TGA RLE image data
//
//  The decoder rejects:
//  - image types outside 1, 2, 3, 9, 10, 11
//  - colour map type values other than 0 or 1
//  - indexed images whose pixel_depth_bits is not 8
//  - colour-mapped images whose CLUT entry size is not 24 or 32 bits
//  - true-colour images whose pixel depth is not 24 or 32 bits
//  - zero width or zero height images
//  - horizontally or vertically malformed image data
// 
//  Interleaving is not specially handled and will not be linearised.
//
//  -----------------------------------------------------------------------------
//  Codec-facing image model
//  -----------------------------------------------------------------------------
//
//  The encoder accepts source image data in one of two storage forms:
//  - single byte per texel
//  - four bytes per texel in native uint32 lane layout
//
//  The source interpretation is described by image_encode_src.
//
//  Single-byte source modes encode as:
//  - greyscale
//
//  Four-byte source modes may encode as:
//  - single-channel greyscale
//  - 24-bit colour
//  - 32-bit colour
//  - indexed colour, if allowed and beneficial
//
//  The encoder does not perform colour reduction.
//  Indexed output is used only when the source image already contains
//  256 or fewer distinct encoded texels.
//
//  -----------------------------------------------------------------------------
//  Decode result model
//  -----------------------------------------------------------------------------
//
//  The decoder returns one of:
//  - Gray : 1 byte per texel
//  - RGBX : 4 bytes per texel, alpha lane synthesised by decode
//  - RGBA : 4 bytes per texel, alpha lane decoded from source data
//
//  24-bit true-colour and 24-bit indexed images decode to RGBX.
//  32-bit true-colour and 32-bit indexed images decode to RGBA.
//
//  -----------------------------------------------------------------------------
//  TGA format usage in this implementation
//  -----------------------------------------------------------------------------
//
//  Byte order
//  ----------
//  All 16-bit header fields are little-endian on disk.
//
//  File layout
//  -----------
//  [18-byte header]
//  [image ID field, id_length bytes]
//  [colour map / CLUT, if present]
//  [image body, raw or RLE]
//
//  Image ID field
//  --------------
//  Read: skipped.
//  Write: omitted by writing id_length = 0.
//
//  Colour map first entry
//  ----------------------
//  This is a logical palette base index, not a byte offset.
//  Read: applied to incoming index values.
//  Write: 0.
//
//  Origin fields
//  -------------
//  x_origin and y_origin are ignored on read and written as 0.
//
//  Orientation bits
//  ----------------
//  Bit 4 of image_descriptor:
//  - 0 == left-to-right
//  - 1 == right-to-left
//
//  Bit 5 of image_descriptor:
//  - 0 == bottom-to-top
//  - 1 == top-to-bottom
//
//  Read:
//  - both horizontal and vertical origin bits are honoured
//  - decoded output is normalised to the codec's canonical engine-facing order
//  - the decode vflip option applies an additional vertical inversion
//
//  Write:
//  - horizontal origin is always written as left-to-right
//  - vertical origin is always written as this codec's fixed stored orientation
//  - encode-time vflip is handled by row traversal during body emission,
//    not by changing the stored origin bit
//
//  Interleaving bits
//  -----------------
//  Bits 6..7 of image_descriptor describe TGA interleaving mode.
//
//  Read:
//  - the interleaving bits are ignored
//  - image data is always decoded as if it were non-interleaved
//  - files using interleaved storage may therefore decode to an incorrect image
//
//  Write:
//  - non-interleaved only
//  - bits 6..7 are written as 0
//
//  Alpha / attribute bits
//  ----------------------
//  Bits 0..3 of image_descriptor describe image-body texel attributes,
//  not CLUT entry width.
//
//  This implementation writes:
//  - 0 for greyscale
//  - 0 for indexed colour
//  - 0 for 24-bit true-colour
//  - 8 for 32-bit true-colour
//
//  CLUT entry width is described only by color_map_entry_bits.
//
//  -----------------------------------------------------------------------------
//  Encoding policy
//  -----------------------------------------------------------------------------
//
//  The encoder chooses between raw and RLE forms based on output size.
//  If CLUT output is allowed and possible, the encoder compares indexed and
//  non-indexed forms and writes the smaller result.
//  Ties resolve toward:
//  - raw over RLE
//  - non-CLUT over CLUT
//
//  -----------------------------------------------------------------------------
//  Ignored or non-preserved source details
//  -----------------------------------------------------------------------------
//
//  The following TGA source details are not preserved through decode/re-encode:
//  - image ID field content
//  - x_origin / y_origin values
//  - original raw vs RLE storage form
//  - original CLUT vs non-CLUT storage choice
//  - extension / developer / footer data
//
//  Decode returns only the canonical image content plus decoded_image_desc.

#include <algorithm>    //  std::min
#include <cstdint>      //  std::uint8_t, std::uint32_t
#include <cstring>      //  std::memcpy, std::memmove, std::memset
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v
#include <utility>      //  std::move

#include "image/codec/tga.hpp"
#include "memory/memory_policies.hpp"
#include "containers/ByteBuffers.hpp"
#include "debug/macros.hpp"

namespace image::codec::tga
{

struct TGAHeader
{
    std::uint8_t id_length;                 // offset: 0
    std::uint8_t color_map_type;            // offset: 1
    std::uint8_t image_type;                // offset: 2
    std::uint8_t color_map_first_entry[2];  // offset: 3-4   little-endian
    std::uint8_t color_map_length[2];       // offset: 5-6   little-endian
    std::uint8_t color_map_entry_bits;      // offset: 7
    std::uint8_t x_origin[2];               // offset: 8-9   little-endian
    std::uint8_t y_origin[2];               // offset: 10-11 little-endian
    std::uint8_t width[2];                  // offset: 12-13 little-endian
    std::uint8_t height[2];                 // offset: 14-15 little-endian
    std::uint8_t pixel_depth_bits;          // offset: 16
    std::uint8_t image_descriptor;          // offset: 17
};

static_assert((sizeof(TGAHeader) == 18u), "TGAHeader must be 18 bytes");

static std::uint16_t read_le_u16(const std::uint8_t* const bytes) noexcept
{
    return static_cast<std::uint16_t>(
        static_cast<std::uint16_t>(bytes[0]) |
        (static_cast<std::uint16_t>(bytes[1]) << 8u));
}

static void write_le_u16(std::uint8_t* const bytes, const std::uint16_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value & 0x00ffu);
    bytes[1] = static_cast<std::uint8_t>((value >> 8u) & 0x00ffu);
}

struct EncoderState
{
    bool use_rle{ false };
    bool use_clut{ false };
    bool src_is_gray{ false };
    std::uint32_t width{ 0u };
    std::uint32_t height{ 0u };
    std::uint32_t channel_mask{ 0xffffffffu };
    std::uint32_t channel_shift{ 0u };
    std::uint32_t src_element_bytes{ 1u };
    std::uint32_t lookup_size{ 0u };
    std::uint32_t lookup_table[256]{ 0u };

    static std::uint32_t get_run_length(const std::uint8_t* const src, const std::uint32_t limit) noexcept
    {
        const std::uint8_t value = src[0];
        for (std::uint32_t count = 1u; count < limit; ++count)
        {
            if (src[count] != value)
            {
                return count;
            }
        }
        return limit;
    }

    static std::uint32_t get_run_length(const std::uint32_t* const src, const std::uint32_t limit, const std::uint32_t channel_mask) noexcept
    {
        const std::uint32_t value = src[0] & channel_mask;
        for (std::uint32_t count = 1u; count < limit; ++count)
        {
            if ((src[count] & channel_mask) != value)
            {
                return count;
            }
        }
        return limit;
    }

    std::uint32_t get_run_length(const void* const row, const std::uint32_t u) const noexcept
    {
        const std::uint32_t limit = width - u;
        if (src_is_gray)
        {
            return get_run_length((reinterpret_cast<const std::uint8_t*>(row) + u), limit);
        }
        return get_run_length((reinterpret_cast<const std::uint32_t*>(row) + u), limit, channel_mask);
    }

    bool lookup(const std::uint32_t texel, std::uint32_t& index) const noexcept
    {
        std::uint32_t lower = 0u;
        std::uint32_t upper = lookup_size;
        while (lower < upper)
        {
            index = (lower + upper) >> 1u;
            const std::uint32_t check = lookup_table[index];
            if (texel == check)
            {
                return true;
            }
            if (texel < check)
            {
                upper = index;
            }
            else
            {
                lower = index + 1u;
            }
        }
        index = lower;
        return false;
    }

    bool lookup_insert(const std::uint32_t texel) noexcept
    {
        std::uint32_t index = 0u;
        if (lookup(texel, index))
        {
            return true;
        }
        if (lookup_size >= 256u)
        {
            return false;
        }
        if (lookup_size > index)
        {
            std::memmove(&lookup_table[index + 1u], &lookup_table[index], (static_cast<std::size_t>(lookup_size - index) * sizeof(std::uint32_t)));
        }
        lookup_table[index] = texel;
        lookup_size++;
        return true;
    }

    std::uint8_t* write_raw(std::uint8_t* const dst, const std::uint8_t* const src, const std::uint32_t count) const noexcept
    {
        std::uint8_t* out = dst;
        std::memcpy(out, src, count);
        out += count;
        return out;
    }

    std::uint8_t* write_raw(std::uint8_t* const dst, const std::uint32_t* const src, const std::uint32_t count) const noexcept
    {
        std::uint8_t* out = dst;
        if (use_clut)
        {
            const std::uint32_t force_alpha = (src_element_bytes == 3u) ? 0xff000000u : 0u;
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const std::uint32_t texel = force_alpha | (src[index] & channel_mask);
                std::uint32_t lookup_index = 0u;
                (void)lookup(texel, lookup_index);
                out[0] = static_cast<std::uint8_t>(lookup_index);
                out++;
            }
        }
        else if (src_element_bytes == 1u)
        {
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                out[0] = static_cast<std::uint8_t>(src[index] >> channel_shift);
                out++;
            }
        }
        else if (src_element_bytes == 3u)
        {
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const std::uint32_t texel = src[index] & channel_mask;
                out[2] = static_cast<std::uint8_t>(texel);
                out[1] = static_cast<std::uint8_t>(texel >> 8);
                out[0] = static_cast<std::uint8_t>(texel >> 16);
                out += 3;
            }
        }
        else if (src_element_bytes == 4u)
        {
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const std::uint32_t texel = src[index];
                out[2] = static_cast<std::uint8_t>(texel);
                out[1] = static_cast<std::uint8_t>(texel >> 8);
                out[0] = static_cast<std::uint8_t>(texel >> 16);
                out[3] = static_cast<std::uint8_t>(texel >> 24);
                out += 4;
            }
        }
        return out;
    }

    std::uint8_t* write_raw(std::uint8_t* const dst, const void* const row, const std::uint32_t u, const std::uint32_t count) const noexcept
    {
        if (src_is_gray)
        {
            return write_raw(dst, (reinterpret_cast<const std::uint8_t*>(row) + u), count);
        }
        return write_raw(dst, (reinterpret_cast<const std::uint32_t*>(row) + u), count);
    }

    std::uint8_t* flush_rle_raw(std::uint8_t* const dst, const void* const row, const std::uint32_t u, const std::uint32_t length) const noexcept
    {
        std::uint8_t* out = dst;
        std::uint32_t remaining = length;
        while (remaining != 0u)
        {
            const std::uint32_t packet_length = std::min(remaining, 128u);
            out[0] = static_cast<std::uint8_t>(packet_length - 1u);
            out++;
            out = write_raw(out, row, (u - remaining), packet_length);
            remaining -= packet_length;
        }
        return out;
    }

    std::uint8_t* flush_rle_run(std::uint8_t* const dst, const void* const row, const std::uint32_t u, const std::uint32_t length) const noexcept
    {
        std::uint8_t* out = dst;
        std::uint32_t remaining = length;
        while (remaining != 0u)
        {
            const std::uint32_t packet_length = std::min(remaining, 128u);
            out[0] = static_cast<std::uint8_t>((packet_length - 1u) | 0x80u);
            out++;
            out = write_raw(out, row, u, 1u);
            remaining -= packet_length;
        }
        return out;
    }
};

CByteBuffer encode(const CByteRectConstView& view, const EncodeOptions& options) noexcept
{
    CByteBuffer buffer;
    if (view.is_empty())
    {   //  the view cannot be used, return the empty buffer
        return buffer;
    }

    //  initialise the encoding state
    EncoderState state;
    state.src_is_gray = (options.src == image_encode_src::Gray);
    state.channel_shift = 0u;
    state.src_element_bytes = 1u;
    switch (options.src)
    {
        case (image_encode_src::AutoTrue32):
        case (image_encode_src::RGBA):
        {
            state.channel_mask = 0xffffffffu;
            state.src_element_bytes = 4u;
            break;
        }
        case (image_encode_src::RGB):
        {
            state.channel_mask = 0x00ffffffu;
            state.src_element_bytes = 3u;
            break;
        }
        case (image_encode_src::A):
        {
            state.channel_shift += 24u;
            break;
        }
        case (image_encode_src::B):
        {
            state.channel_shift = 16u;
            break;
        }
        case (image_encode_src::G):
        {
            state.channel_shift = 8u;
            break;
        }
        case (image_encode_src::R):
        case (image_encode_src::Gray):
        {
            break;
        }
        default:    //  unrecognised or un-encodable source, return the empty buffer
        {
            return buffer;
        }
    }
    const std::size_t view_height = view.row_count();
    if (view_height > 0x0000ffffu)
    {   //  the image height is not compatible with the encoder, return the empty buffer
        return buffer;
    }
    state.height = static_cast<std::uint32_t>(view_height);
    const std::size_t view_width = view.row_width();
    if (state.src_is_gray)
    {
        if (view_width > 0x0000ffffu)
        {   //  the image width is not compatible with the encoder, return the empty buffer
            return buffer;
        }
        state.width = static_cast<std::uint32_t>(view_width);
    }
    else
    {
        if ((view.align() < 4u) || ((view_width & 3u) != 0u) || (view_width > 0x0003fffcu))
        {   //  the image alignment or width is not compatible with the encoder, return the empty buffer
            return buffer;
        }
        state.width = static_cast<std::uint32_t>(view_width >> 2);
    }
    const std::uint32_t image_texel_count = state.width * state.height;

    //  build the colour lookup table
    if (options.allow_clut)
    {
        if (state.src_element_bytes > 1u)
        {   //  attempt to build the lookup_table
            state.use_clut = true;
            const std::uint32_t force_alpha = (state.src_element_bytes == 3u) ? 0xff000000u : 0u;
            for (std::uint32_t v = 0u; v < state.height; ++v)
            {
                const uint32_t* row = reinterpret_cast<const uint32_t*>(view.row_data(v));
                for (std::uint32_t u = 0u; u < state.width; ++u)
                {
                    const std::uint32_t texel = row[u] | force_alpha;
                    if (!state.lookup_insert(texel))
                    {
                        state.use_clut = false;
                        break;
                    }
                }
                if (!state.use_clut)
                {
                    break;
                }
            }
        }
    }

    //  check if the source image uses the alpha channel
    if (options.src == image_encode_src::AutoTrue32)
    {
        bool is_true32 = false;
        if (state.use_clut)
        {   //  check if the lookup_table has all alpha == 0xffu
            for (std::uint32_t index = 0u; index < state.lookup_size; ++index)
            {
                const std::uint32_t texel = state.lookup_table[index];
                if ((texel & 0xff000000u) != 0xff000000u)
                {
                    is_true32 = true;
                    break;
                }
            }
        }
        else
        {   //  check if the bitmap has all alpha == 0xffu
            for (std::uint32_t v = 0u; v < state.height; ++v)
            {
                const std::uint32_t* row = reinterpret_cast<const std::uint32_t*>(view.row_data(v));
                for (std::uint32_t u = 0u; u < state.width; ++u)
                {
                    const std::uint32_t texel = row[u];
                    if ((texel & 0xff000000u) != 0xff000000u)
                    {
                        is_true32 = true;
                        break;
                    }
                }
                if (is_true32)
                {
                    break;
                }
            }
        }
        if (!is_true32)
        {
            state.channel_mask = 0x00ffffffu;
            state.src_element_bytes = 3u;
        }
    }
    const std::uint32_t wide_element_bytes = state.src_element_bytes;
    const std::uint32_t lookup_entry_bytes = (wide_element_bytes == 4u) ? 4u : 3u;
    const std::uint32_t lookup_table_bytes = state.lookup_size * lookup_entry_bytes;

    //  determine the optimal encoding and calculate the output file size
    std::uint64_t tga_file_bytes = sizeof(TGAHeader);
    if (options.allow_rle)
    {   //  calculate the compressed size, if no compression then no rle
        static const std::uint32_t k_byte_pass_mask = 1u;
        static const std::uint32_t k_wide_pass_mask = 2u;
        const std::uint32_t pass_mask = state.use_clut ? (k_byte_pass_mask | k_wide_pass_mask) : ((wide_element_bytes == 1u) ? k_byte_pass_mask : k_wide_pass_mask);
        std::uint32_t byte_packet_count = 0u;     //  single byte element rle control byte count
        std::uint32_t byte_element_count = 0u;    //  single byte element rle element count
        std::uint32_t wide_packet_count = 0u;     //  multi-byte element rle control byte count
        std::uint32_t wide_element_count = 0u;    //  multi-byte element rle element count
        for (std::uint32_t v = 0u; v < state.height; ++v)
        {
            const void* row = reinterpret_cast<const void*>(view.row_data(v));
            for (std::uint32_t pass = 0; pass < 2u; ++pass)
            {
                if (((pass_mask >> pass) & 1u) == 0u)
                {   //  this pass is not used
                    continue;
                }
                const bool byte_optimise = (1u << pass) == k_byte_pass_mask;
                std::uint32_t packet_count = 0u;
                std::uint32_t element_count = 0u;
                std::uint32_t raw_length = 0u;
                std::uint32_t run_length = 0u;
                for (std::uint32_t u = 0u; u < state.width; u += run_length)
                {
                    run_length = state.get_run_length(row, u);
                    if (run_length == 1u)
                    {   //  extend the current raw sequence
                        raw_length++;
                    }
                    else if (byte_optimise && (run_length == 2u) && (((raw_length + 1u) & 127u) > 1u))
                    {   //  single byte element encoding can absorb the run of 2 into the current raw sequence
                        raw_length += 2u;
                    }
                    else
                    {
                        if ((run_length & 127u) == 1u)
                        {   //  absorb a single element into the current or next raw sequence
                            run_length--;
                            if ((raw_length & 127u) != 0u)
                            {   //  absorb a single element into the current raw sequence
                                raw_length++;
                                u++;
                            }
                        }
                        if (raw_length != 0u)
                        {   //  flush the pending sequence
                            packet_count += (raw_length + 127u) >> 7;
                            element_count += raw_length;
                            raw_length = 0u;
                        }
                        const std::uint32_t run_packets = (run_length + 127u) >> 7;
                        packet_count += run_packets;
                        element_count += run_packets;
                    }
                }
                if (raw_length != 0u)
                {
                    packet_count += (raw_length + 127u) >> 7;
                    element_count += raw_length;
                    raw_length = 0u;
                }
                if (byte_optimise)
                {
                    byte_packet_count += packet_count;
                    byte_element_count += element_count;
                }
                else
                {
                    wide_packet_count += packet_count;
                    wide_element_count += element_count;
                }
            }
        }
        const std::uint64_t byte_raw_size = static_cast<std::uint64_t>(image_texel_count) + static_cast<std::uint64_t>(lookup_table_bytes);
        const std::uint64_t byte_rle_size = static_cast<std::uint64_t>(byte_element_count) + static_cast<std::uint64_t>(byte_packet_count) + static_cast<std::uint64_t>(lookup_table_bytes);
        const std::uint64_t wide_raw_size = static_cast<std::uint64_t>(image_texel_count) * static_cast<std::uint64_t>(wide_element_bytes);
        const std::uint64_t wide_rle_size = (static_cast<std::uint64_t>(wide_element_count) * static_cast<std::uint64_t>(wide_element_bytes)) + static_cast<std::uint64_t>(wide_packet_count);
        if (state.use_clut)
        {   //  determine if clut still makes sense
            state.use_clut = std::min(byte_raw_size, byte_rle_size) < std::min(wide_raw_size, wide_rle_size);
            state.use_rle = state.use_clut ? (byte_rle_size < byte_raw_size) : (wide_rle_size < wide_raw_size);
            tga_file_bytes += state.use_rle ? (state.use_clut ? byte_rle_size : wide_rle_size) : (state.use_clut ? byte_raw_size : wide_raw_size);
        }
        else if (wide_element_bytes == 1u)
        {   //  output is single byte
            state.use_rle = byte_rle_size < byte_raw_size;
            tga_file_bytes += state.use_rle ? byte_rle_size : byte_raw_size;
        }
        else
        {   //  output is multi-byte
            state.use_rle = wide_rle_size < wide_raw_size;
            tga_file_bytes += state.use_rle ? wide_rle_size : wide_raw_size;
        }
    }
    else
    {   //  forced no rle, check if clut still smaller
        const std::uint64_t byte_raw_size = static_cast<std::uint64_t>(image_texel_count) + static_cast<std::uint64_t>(lookup_table_bytes);
        const std::uint64_t wide_raw_size = static_cast<std::uint64_t>(image_texel_count) * static_cast<std::uint64_t>(wide_element_bytes);
        if (state.use_clut)
        {
            state.use_clut = byte_raw_size < wide_raw_size;
            tga_file_bytes += state.use_clut ? byte_raw_size : wide_raw_size;
        }
        else
        {
            tga_file_bytes += wide_raw_size;
        }
    }
    const std::uint32_t tga_element_bytes = state.use_clut ? 1u : wide_element_bytes;

    //  construct the tga file memory image
    if (tga_file_bytes > memory::k_byte_size_ceiling)
    {
        MV_ERROR("TGA encode output exceeded the byte-size ceiling");
        return buffer;
    }

    if (buffer.allocate(static_cast<std::size_t>(tga_file_bytes)))
    {
        (void)buffer.set_size(static_cast<std::size_t>(tga_file_bytes));

        TGAHeader* header = reinterpret_cast<TGAHeader*>(buffer.data());
        std::uint8_t* body = reinterpret_cast<std::uint8_t*>(header + 1u);

        //  populate the TGA header
        std::memset(header, 0, sizeof(TGAHeader));
        header->color_map_type = static_cast<std::uint8_t>(state.use_clut ? 1u : 0u);
        header->image_type = static_cast<std::uint8_t>((state.use_rle ? 9u : 1u) + (state.use_clut ? 0u : ((tga_element_bytes == 1u) ? 2u : 1u )));
        if (state.use_clut)
        {
            write_le_u16(header->color_map_length, static_cast<std::uint16_t>(state.lookup_size));
            header->color_map_entry_bits = static_cast<std::uint8_t>(lookup_entry_bytes << 3);
            for (std::uint32_t lookup_index = 0; lookup_index < state.lookup_size; ++lookup_index)
            {   //  populate the clut
                std::uint32_t lookup_entry = state.lookup_table[lookup_index];
                body[2] = static_cast<std::uint8_t>(lookup_entry & 0x000000ffu);            //  R
                body[1] = static_cast<std::uint8_t>((lookup_entry & 0x0000ff00u) >> 8);     //  G
                body[0] = static_cast<std::uint8_t>((lookup_entry & 0x00ff0000u) >> 16);    //  B
                if (lookup_entry_bytes == 4u)
                {
                    body[3] = static_cast<std::uint8_t>((lookup_entry & 0xff000000u) >> 24);
                }
                body += lookup_entry_bytes;
            }
        }
        write_le_u16(header->width, static_cast<std::uint16_t>(state.width));
        write_le_u16(header->height, static_cast<std::uint16_t>(state.height));
        header->pixel_depth_bits = static_cast<std::uint8_t>(tga_element_bytes << 3);
        header->image_descriptor = (tga_element_bytes == 4u) ? 8u : 0u;

        //  populate the image body
        const std::uint32_t max_v = state.height - 1u;
        if (state.use_rle)
        {   //  rle encoded
            const bool byte_optimise = tga_element_bytes == 1u;
            for (std::uint32_t v = 0u; v < state.height; ++v)
            {
                const void* row = reinterpret_cast<const void*>(view.row_data(options.vflip ? (max_v - v) : v));
                std::uint32_t raw_length = 0u;
                std::uint32_t run_length = 0u;
                for (std::uint32_t u = 0u; u < state.width; u += run_length)
                {
                    run_length = state.get_run_length(row, u);
                    if (run_length == 1u)
                    {   //  extend the current raw sequence
                        raw_length++;
                    }
                    else if (byte_optimise && (run_length == 2u) && (((raw_length + 1u) & 127u) > 1u))
                    {   //  single byte element encoding can absorb the run of 2 into the current raw sequence
                        raw_length += 2u;
                    }
                    else
                    {
                        if ((run_length & 127u) == 1u)
                        {   //  absorb a single element into the current or next raw sequence
                            run_length--;
                            if ((raw_length & 127u) != 0u)
                            {   //  absorb a single element into the current raw sequence
                                raw_length++;
                                u++;
                            }
                        }
                        if (raw_length != 0u)
                        {   //  flush the pending raw sequence
                            body = state.flush_rle_raw(body, row, u, raw_length);
                            raw_length = 0u;
                        }
                        body = state.flush_rle_run(body, row, u, run_length);
                    }
                }
                if (raw_length != 0u)
                {
                    body = state.flush_rle_raw(body, row, state.width, raw_length);
                    raw_length = 0u;
                }
            }
        }
        else
        {   //  raw encoded
            for (std::uint32_t v = 0u; v < state.height; ++v)
            {
                const void* row = reinterpret_cast<const void*>(view.row_data(options.vflip ? (max_v - v) : v));
                body = state.write_raw(body, row, 0u, state.width);
            }
        }
    }
    else
    {
        MV_ERROR("TGA encode failed to allocate the output buffer");
    }
    return buffer;
}

CByteRectBuffer decode(const CByteConstView& view, decoded_image_desc& desc, const bool vflip) noexcept
{
    //  ensure that the desc is initialised regardless of whether we can decode
    desc = decoded_image_desc::Gray;

    //  basic sanity checks
    CByteRectBuffer buffer;
    if (view.is_empty())
    {   //  the view cannot be used, return the empty buffer
        return buffer;
    }
    const std::uint64_t file_size = static_cast<std::uint64_t>(view.size());
    if (file_size < static_cast<std::uint64_t>(sizeof(TGAHeader) + 1u))
    {   //  insufficient data for even a single 8-bit pixel TGA file, return the empty buffer
        return buffer;
    }

    //  extract the header parameters
    const TGAHeader* const header = reinterpret_cast<const TGAHeader*>(view.data());
    const std::uint32_t id_length = static_cast<std::uint32_t>(header->id_length);
    const std::uint32_t color_map_type = static_cast<std::uint32_t>(header->color_map_type);
    const std::uint32_t image_type = static_cast<std::uint32_t>(header->image_type);
    const std::uint32_t color_map_first_entry = static_cast<std::uint32_t>(read_le_u16(header->color_map_first_entry));
    const std::uint32_t color_map_length = static_cast<std::uint32_t>(read_le_u16(header->color_map_length));
    const std::uint32_t color_map_entry_bits = static_cast<std::uint32_t>(header->color_map_entry_bits);
    const std::uint32_t width = static_cast<std::uint32_t>(read_le_u16(header->width));
    const std::uint32_t height = static_cast<std::uint32_t>(read_le_u16(header->height));
    const std::uint32_t pixel_depth_bits = static_cast<std::uint32_t>(header->pixel_depth_bits);
    const std::uint32_t image_descriptor = static_cast<std::uint32_t>(header->image_descriptor);
    const std::uint32_t image_attributes = image_descriptor & 0x0fu;

    //  initialise the decoding state
    const bool is_gray = (image_type == 3u) || (image_type == 11u);
    const bool uses_rle = (image_type >= 9u) && (image_type <= 11u);
    const bool uses_clut = (color_map_type == 1u);
    const bool is_true32 = (color_map_type == 1u) ? (color_map_entry_bits == 32u) : ((pixel_depth_bits == 32u) && (image_attributes == 8u));
    const bool flipped_u = (image_descriptor & 0x10u) != 0u;
    const bool flipped_v = ((image_descriptor & 0x20u) != 0u) ? !vflip : vflip;
    const std::uint32_t image_element_count = width * height;
    const std::uint32_t clut_element_bytes = color_map_entry_bits >> 3;
    const std::uint32_t clut_storage_bytes = color_map_length * clut_element_bytes;
    const std::uint32_t tga_element_bytes = pixel_depth_bits >> 3;
    const std::uint32_t dst_element_bytes = is_gray ? 1u : 4u;
    const std::uint32_t clut_byte_offset = static_cast<std::uint32_t>(sizeof(TGAHeader)) + id_length;
    const std::uint32_t body_byte_offset = clut_byte_offset + clut_storage_bytes;
    const std::uint32_t min_row_bytes = uses_rle ? (((width + 127u) >> 7) * (tga_element_bytes + 1u)) : (width * tga_element_bytes);
    const std::uint64_t min_file_size = static_cast<std::uint64_t>(body_byte_offset) + (static_cast<std::uint64_t>(min_row_bytes) * static_cast<std::uint64_t>(height));
    const std::uint8_t* clut = reinterpret_cast<const std::uint8_t*>(header) + clut_byte_offset;
    const std::uint8_t* body = reinterpret_cast<const std::uint8_t*>(header) + body_byte_offset;

    //  validate the TGA header
    if ((width == 0u) || (height == 0u) || (color_map_type > 1u) || (((image_type - 1u) & ~8u) > 2u))
    {   //  unsupported width or height or color_map_type or image_type, possibly not a TGA file, return the empty buffer
        return buffer;
    }
    if (uses_clut)
    {   //  has a colour map, validate it
        if (((image_type != 1u) && (image_type != 9u)) || (pixel_depth_bits != 8u) ||
            ((color_map_entry_bits != 24u) && (color_map_entry_bits != 32u)) || (color_map_length > 256u))
        {   //  unsupported colour map, return the empty buffer
            return buffer;
        }
    }
    else
    {   //  no colour map, validate the format
        if (((image_type != 2u) && (image_type != 3u) && (image_type != 10u) && (image_type != 11u)) ||
            ((pixel_depth_bits != 8u) && (pixel_depth_bits != 24u) && (pixel_depth_bits != 32u)))
        {   //  unsupported pixel type, return the empty buffer
            return buffer;
        }
    }
    if (file_size < min_file_size)
    {   //  file size is too small, return the empty buffer
        return buffer;
    }

    //  extract the colour lookup table (if any)
    std::uint32_t lookup_size{ 0u };
    std::uint32_t lookup_table[256]{ 0u };
    if (uses_clut)
    {
        if (clut_element_bytes == 4u)
        {
            while (lookup_size < color_map_length)
            {
                lookup_table[lookup_size] =
                    (static_cast<std::uint32_t>(clut[2])) |
                    (static_cast<std::uint32_t>(clut[1]) << 8) |
                    (static_cast<std::uint32_t>(clut[0]) << 16) |
                    (static_cast<std::uint32_t>(clut[3]) << 24);
                lookup_size++;
                clut += 4u;
            }

        }
        else
        {
            while (lookup_size < color_map_length)
            {
                lookup_table[lookup_size] = 0xff000000u |
                    (static_cast<std::uint32_t>(clut[2])) |
                    (static_cast<std::uint32_t>(clut[1]) << 8) |
                    (static_cast<std::uint32_t>(clut[0]) << 16);
                lookup_size++;
                clut += 3u;
            }
        }
    }

    //  extract the image
    if (buffer.allocate((is_gray ? width : (width << 2)), height, (is_gray ? 1u : 4u)))
    {   //  the buffer was successfully allocated
        desc = is_gray ? decoded_image_desc::Gray : (is_true32 ? decoded_image_desc::RGBA : decoded_image_desc::RGBX);
        const std::uint32_t max_v = height - 1u;
        if (uses_rle)
        {   //  the rle path needs to check the available source data size to avoid overrun reading
            std::uint64_t src_bytes = file_size - static_cast<std::uint64_t>(body_byte_offset);
            std::uint32_t raw_length = 0u;
            std::uint32_t run_length = 0u;
            std::uint32_t run_value = 0u;
            for (std::uint32_t v = 0; v < height; ++v)
            {
                void* dst = reinterpret_cast<std::uint32_t*>(buffer.row_data(flipped_v ? (max_v - v) : v));
                for (std::uint32_t u = 0; u < width; ++u)
                {
                    if ((run_length | raw_length) == 0u)
                    {
                        bool read_exhausted = true;
                        if (src_bytes > tga_element_bytes)
                        {
                            const std::uint8_t lead_byte = body[0];
                            body++;
                            src_bytes--;
                            if (lead_byte & 0x80u)
                            {   //  run
                                raw_length = 1u;
                                run_length = static_cast<uint32_t>(lead_byte & 0x7fu);
                                read_exhausted = false;
                            }
                            else
                            {   //  raw
                                raw_length = static_cast<uint32_t>(lead_byte & 0x7fu) + 1u;
                                const std::uint32_t raw_bytes = raw_length * tga_element_bytes;
                                if (src_bytes >= raw_bytes)
                                {
                                    src_bytes -= raw_bytes;
                                    read_exhausted = false;
                                }
                            }
                        }
                        if (read_exhausted)
                        {   //  read exhausted, deallocate the buffer and return the empty buffer
                            buffer.deallocate();
                            return buffer;
                        }
                    }
                    if (raw_length)
                    {
                        raw_length--;
                        run_length++;
                        if (uses_clut)
                        {
                            std::uint32_t index = body[0];
                            if ((index < color_map_first_entry) || ((index - color_map_first_entry) >= lookup_size))
                            {   //  invalid index, deallocate the buffer and return the empty buffer
                                buffer.deallocate();
                                return buffer;
                            }
                            run_value = lookup_table[index - color_map_first_entry];
                        }
                        else if (tga_element_bytes == 4u)
                        {
                            run_value =
                                (static_cast<std::uint32_t>(body[2])) |
                                (static_cast<std::uint32_t>(body[1]) << 8) |
                                (static_cast<std::uint32_t>(body[0]) << 16) |
                                (static_cast<std::uint32_t>(body[3]) << 24);
                        }
                        else if (tga_element_bytes == 3u)
                        {
                            run_value = 0xff000000u |
                                (static_cast<std::uint32_t>(body[2])) |
                                (static_cast<std::uint32_t>(body[1]) << 8) |
                                (static_cast<std::uint32_t>(body[0]) << 16);
                        }
                        else
                        {
                            run_value = static_cast<std::uint32_t>(body[0]);
                        }
                        body += tga_element_bytes;
                    }
                    if (run_length)
                    {
                        run_length--;
                        if (is_gray)
                        {
                            reinterpret_cast<std::uint8_t*>(dst)[u] = static_cast<std::uint8_t>(run_value);
                        }
                        else
                        {
                            reinterpret_cast<std::uint32_t*>(dst)[u] = run_value;
                        }
                    }
                }
            }
        }
        else
        {   //  the non-rle path does not need to check the available source data size as this was already validated
            if (uses_clut)
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(buffer.row_data(flipped_v ? (max_v - v) : v));
                    for (std::uint32_t u = 0; u < width; ++u)
                    {
                        std::uint32_t index = body[0];
                        if ((index < color_map_first_entry) || ((index - color_map_first_entry) >= lookup_size))
                        {   //  invalid index, deallocate the buffer and return the empty buffer
                            buffer.deallocate();
                            return buffer;
                        }
                        dst[u] = lookup_table[index - color_map_first_entry];
                        body++;
                    }
                }
            }
            else if (tga_element_bytes == 4u)
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(buffer.row_data(flipped_v ? (max_v - v) : v));
                    for (std::uint32_t u = 0; u < width; ++u)
                    {
                        const std::uint32_t texel = 
                            (static_cast<std::uint32_t>(body[2])) |
                            (static_cast<std::uint32_t>(body[1]) << 8) |
                            (static_cast<std::uint32_t>(body[0]) << 16) |
                            (static_cast<std::uint32_t>(body[3]) << 24);
                        dst[u] = texel;
                        body += 4;
                    }
                }
            }
            else if (tga_element_bytes == 3u)
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint32_t* dst = reinterpret_cast<std::uint32_t*>(buffer.row_data(flipped_v ? (max_v - v) : v));
                    for (std::uint32_t u = 0; u < width; ++u)
                    {
                        const std::uint32_t texel = 0xff000000u |
                            (static_cast<std::uint32_t>(body[2])) |
                            (static_cast<std::uint32_t>(body[1]) << 8) |
                            (static_cast<std::uint32_t>(body[0]) << 16);
                        dst[u] = texel;
                        body += 3;
                    }
                }
            }
            else
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint8_t* dst = reinterpret_cast<std::uint8_t*>(buffer.row_data(flipped_v ? (max_v - v) : v));
                    std::memcpy(dst, body, width);
                    body += width;
                }
            }
        }
        if (flipped_u)
        {   //  horizontally flip the image
            const std::uint32_t half_width = width >> 1;
            const std::uint32_t right_edge = width - 1u;
            if (is_gray)
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint8_t* row = reinterpret_cast<std::uint8_t*>(buffer.row_data(v));
                    for (std::uint32_t u = 0; u < half_width; ++u)
                    {
                        const std::uint32_t t = right_edge - u;
                        const uint8_t swap = row[u];
                        row[u] = row[t];
                        row[t] = swap;
                    }
                }
            }
            else
            {
                for (std::uint32_t v = 0; v < height; ++v)
                {
                    std::uint32_t* row = reinterpret_cast<std::uint32_t*>(buffer.row_data(v));
                    for (std::uint32_t u = 0; u < half_width; ++u)
                    {
                        const std::uint32_t t = right_edge - u;
                        const uint32_t swap = row[u];
                        row[u] = row[t];
                        row[t] = swap;
                    }
                }
            }
        }
    }
    return buffer;
}

}	//	namespace image::codec::tga


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   byte_view_messages.hpp
//  Author: Ritchie Brannan
//  Date:   30 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Compact message transport descriptors for byte and byte-rect views.
//
//  These structures are not owning storage and are not the native in-process
//  view representation. They are narrow POD descriptors for message transport.
//
//  Native byte views use std::size_t for size and pitch fields.
//  Message views use std::uint32_t fields and can only be created from
//  native views when all represented metadata is in the transportable range.

#pragma once

#ifndef BYTE_VIEW_PAYLOADS_HPP_INCLUDED
#define BYTE_VIEW_PAYLOADS_HPP_INCLUDED

#include <cstddef>  //  std::size_t
#include <cstdint>  //  std::uint8_t, std::uint32_t

#include "foundation/containers/ByteBuffers.hpp"

//==============================================================================
//  MessageByteDesc
//  Narrow metadata for a contiguous byte transport view.
//==============================================================================

struct MessageByteDesc
{
    std::uint32_t align{ 0u };
    std::uint32_t size{ 0u };
};

//==============================================================================
//  MessageByteRectDesc
//  Narrow metadata for a rectangular byte transport view.
//==============================================================================

struct MessageByteRectDesc
{
    std::uint32_t align{ 0u };
    std::uint32_t row_pitch{ 0u };
    std::uint32_t row_width{ 0u };
    std::uint32_t row_count{ 0u };
};

//==============================================================================
//  Pointer-bearing transport views
//==============================================================================

struct MessageByteView
{
    std::uint8_t* data{ nullptr };
    MessageByteDesc desc;
};

struct MessageByteConstView
{
    const std::uint8_t* data{ nullptr };
    MessageByteDesc desc;
};

struct MessageByteRectView
{
    std::uint8_t* data{ nullptr };
    MessageByteRectDesc desc;
};

struct MessageByteRectConstView
{
    const std::uint8_t* data{ nullptr };
    MessageByteRectDesc desc;
};

//==============================================================================
//  Descriptor narrowing
//==============================================================================

[[nodiscard]] inline bool try_make_message_byte_desc(
    const std::size_t align,
    const std::size_t size,
    MessageByteDesc& out_desc) noexcept
{
    if ((align <= 0x80000000u) && (size <= 0x80000000u))
    {
        out_desc.align = static_cast<std::uint32_t>(align);
        out_desc.size = static_cast<std::uint32_t>(size);
        return true;
    }

    out_desc = MessageByteDesc{};
    return false;
}

[[nodiscard]] inline bool try_make_message_byte_rect_desc(
    const std::size_t align,
    const std::size_t row_pitch,
    const std::size_t row_width,
    const std::size_t row_count,
    MessageByteRectDesc& out_desc) noexcept
{
    if ((align <= 0x80000000u) &&
        (row_pitch <= 0x80000000u) &&
        (row_width <= 0x80000000u) &&
        (row_count <= 0x80000000u))
    {
        out_desc.align = static_cast<std::uint32_t>(align);
        out_desc.row_pitch = static_cast<std::uint32_t>(row_pitch);
        out_desc.row_width = static_cast<std::uint32_t>(row_width);
        out_desc.row_count = static_cast<std::uint32_t>(row_count);
        return true;
    }

    out_desc = MessageByteRectDesc{};
    return false;
}

//==============================================================================
//  View narrowing
//==============================================================================

[[nodiscard]] inline bool try_make_message_byte_view(const CByteView& view, MessageByteView& out_view) noexcept
{
    if (view.is_ready() && try_make_message_byte_desc(view.align(), view.size(), out_view.desc))
    {
        out_view.data = view.data();
        return true;
    }

    out_view = MessageByteView{};
    return false;
}

[[nodiscard]] inline bool try_make_message_byte_const_view(const CByteConstView& view, MessageByteConstView& out_view) noexcept
{
    if (view.is_ready() && try_make_message_byte_desc(view.align(), view.size(), out_view.desc))
    {
        out_view.data = view.data();
        return true;
    }

    out_view = MessageByteConstView{};
    return false;
}

[[nodiscard]] inline bool try_make_message_byte_rect_view(const CByteRectView& view, MessageByteRectView& out_view) noexcept
{
    if (view.is_ready() &&
        try_make_message_byte_rect_desc(view.align(), view.row_pitch(), view.row_width(), view.row_count(), out_view.desc))
    {
        out_view.data = view.data();
        return true;
    }

    out_view = MessageByteRectView{};
    return false;
}

[[nodiscard]] inline bool try_make_message_byte_rect_const_view(const CByteRectConstView& view, MessageByteRectConstView& out_view) noexcept
{
    if (view.is_ready() &&
        try_make_message_byte_rect_desc(view.align(), view.row_pitch(), view.row_width(), view.row_count(), out_view.desc))
    {
        out_view.data = view.data();
        return true;
    }

    out_view = MessageByteRectConstView{};
    return false;
}

//==============================================================================
//  Widening back to native views
//==============================================================================

[[nodiscard]] inline CByteView make_byte_view(const MessageByteView& view) noexcept
{
    return CByteView{
        view.data,
        static_cast<std::size_t>(view.desc.size),
        static_cast<std::size_t>(view.desc.align)
    };
}

[[nodiscard]] inline CByteConstView make_byte_const_view(const MessageByteConstView& view) noexcept
{
    return CByteConstView{
        view.data,
        static_cast<std::size_t>(view.desc.size),
        static_cast<std::size_t>(view.desc.align)
    };
}

[[nodiscard]] inline CByteRectView make_byte_rect_view(const MessageByteRectView& view) noexcept
{
    return CByteRectView{
        view.data,
        static_cast<std::size_t>(view.desc.row_pitch),
        static_cast<std::size_t>(view.desc.row_width),
        static_cast<std::size_t>(view.desc.row_count),
        static_cast<std::size_t>(view.desc.align)
    };
}

[[nodiscard]] inline CByteRectConstView make_byte_rect_const_view(const MessageByteRectConstView& view) noexcept
{
    return CByteRectConstView{
        view.data,
        static_cast<std::size_t>(view.desc.row_pitch),
        static_cast<std::size_t>(view.desc.row_width),
        static_cast<std::size_t>(view.desc.row_count),
        static_cast<std::size_t>(view.desc.align)
    };
}

#endif  //  #ifndef BYTE_VIEW_PAYLOADS_HPP_INCLUDED

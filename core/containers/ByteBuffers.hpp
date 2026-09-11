
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   ByteBuffers.hpp
//  Author: Ritchie Brannan
//  Date:   22 Feb 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Owning and non-owning byte storage utilities for contiguous and
//  rectangular layouts.
//
//  Models raw byte storage only. Does not impose element, texel,
//  pixel, or structure semantics.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  Alignment refers to the guaranteed alignment at the current storage
//  origin or view origin. Subviews may reduce alignment guarantees.
//
//  Rect storage is byte-contiguous only when row_width == row_pitch.
//
//  See docs/ByteBuffers.md for the full documentation.

#pragma once

#ifndef BYTE_BUFFERS_HPP_INCLUDED
#define BYTE_BUFFERS_HPP_INCLUDED

#include <algorithm>    //  std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t
#include <cstring>      //  std::memcpy, std::memset
#include <utility>      //  std::move

#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "memory/memory_view.hpp"
#include "bit_utils/bit_ops.hpp"
#include "debug/macros.hpp"

//==============================================================================
//  Forward declarations
//==============================================================================

class CByteBuffer;
class CByteView;
class CByteConstView;
class CByteRectBuffer;
class CByteRectView;
class CByteRectConstView;

//==============================================================================
//  MetaByteView
//  Metadata for a contiguous byte view.
//
//  State model:
//  - size is the logical byte extent.
//  - size == 0 is the canonical empty state.
//  - ready requires size != 0 and size <= memory::k_byte_size_ceiling.
//==============================================================================

struct MetaByteView
{
    std::size_t size = 0u;
    void reset() noexcept { size = 0u; }
    [[nodiscard]] bool is_valid() const noexcept { return size <= memory::k_byte_size_ceiling; }
    [[nodiscard]] bool is_empty() const noexcept { return size == 0u; }
    [[nodiscard]] bool is_ready() const noexcept { return memory::in_non_empty_range(size, memory::k_byte_size_ceiling); }
};

//==============================================================================
//  MetaByteBuffer
//  Metadata for an owning contiguous byte buffer.
//
//  State model:
//  - size is the logical byte extent.
//  - capacity is the allocated byte extent.
//  - {size == 0, capacity == 0} is the canonical empty state.
//  - {size == 0, capacity != 0} is a valid ready state.
//  - ready requires size <= capacity <= memory::k_byte_size_ceiling.
//==============================================================================

struct MetaByteBuffer
{
    std::size_t size = 0u;
    std::size_t capacity = 0u;
    void reset() noexcept { size = capacity = 0u; }
    [[nodiscard]] bool is_valid() const noexcept { return (size <= capacity) && (capacity <= memory::k_byte_size_ceiling); }
    [[nodiscard]] bool is_empty() const noexcept { return (size == 0u) || (capacity == 0u); }
    [[nodiscard]] bool is_ready() const noexcept { return (size <= capacity) && memory::in_non_empty_range(capacity, memory::k_byte_size_ceiling); }
    [[nodiscard]] MetaByteView byte_view() const noexcept { return MetaByteView{ size }; }
};

//==============================================================================
//  CByteBuffer
//  Owning contiguous byte buffer with optional spare capacity.
//==============================================================================

class CByteBuffer
{
public:

    //  Default and deleted lifetime
    CByteBuffer() noexcept = default;
    CByteBuffer(const CByteBuffer&) noexcept = delete;
    CByteBuffer& operator=(const CByteBuffer&) noexcept = delete;

    //  Move/lifetime
    CByteBuffer(CByteBuffer&&) noexcept;
    CByteBuffer& operator=(CByteBuffer&&) noexcept;
    ~CByteBuffer() noexcept { deallocate(); };

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Views
    [[nodiscard]] CByteView view() const noexcept;
    [[nodiscard]] CByteConstView const_view() const noexcept;

    //  Accessors
    [[nodiscard]] std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<std::uint8_t*>(const_cast<void*>(m_token.data())) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_token.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_meta.size : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t capacity() const noexcept { return is_ready() ? m_meta.capacity : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t available() const noexcept { return is_ready() ? (m_meta.capacity - m_meta.size) : std::size_t{ 0 }; }

    //  Append and logical size
    [[nodiscard]] bool append(const std::uint8_t* const data, const std::size_t size) noexcept;
    [[nodiscard]] bool append(const std::size_t size, const bool zero = true) noexcept;
    [[nodiscard]] bool set_size(const std::size_t size) noexcept;

    //  Allocation and capacity management
    [[nodiscard]] bool allocate(const std::size_t capacity, const std::size_t align = 0u) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t size, const std::size_t capacity, const std::size_t align = 0u) noexcept;
    [[nodiscard]] bool resize(const std::size_t size, const std::size_t align = 0u) noexcept;
    [[nodiscard]] bool reserve(const std::size_t minimum_capacity, const std::size_t align = 0u) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t extra, const std::size_t align = 0u) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept;
    [[nodiscard]] bool construct_and_copy_from(const CByteConstView& view) noexcept;
    void deallocate() noexcept;

    //  Utilities
    void zero_fill() const noexcept;

    //  Handoff
    memory::CMemoryToken disown(MetaByteBuffer& meta) noexcept;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* const context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* const context = nullptr) noexcept;

private:
    friend class CStringBuffer;
    friend class CErasedOwner;
    friend class CBakedDocumentBlock;

    [[nodiscard]] memory::CMemoryContext* memory_source_context() const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    memory::CMemoryToken m_token{ 1u, 1u };
    MetaByteBuffer m_meta;
};

//==============================================================================
//  CByteView
//  Non-owning mutable view over a contiguous byte range.
//==============================================================================

class CByteView
{
public:

    //  Default lifetime
    CByteView() noexcept = default;
    CByteView(CByteView&&) noexcept = default;
    CByteView& operator=(CByteView&&) noexcept = default;
    CByteView(const CByteView&) noexcept = default;
    CByteView& operator=(const CByteView&) noexcept = default;
    ~CByteView() noexcept = default;

    //  Construction
    CByteView(std::uint8_t* const data, const std::size_t size, const std::size_t align = 0u) noexcept { (void)set(data, size, align); }
    CByteView(const memory::CMemoryView& view, const MetaByteView& meta) noexcept { (void)set(view, meta); }

    //  View state
    CByteView& set(std::uint8_t* const data, const std::size_t size, const std::size_t align = 0u) noexcept;
    CByteView& set(const memory::CMemoryView& view, const MetaByteView& meta) noexcept;
    CByteView& reset() noexcept { m_view.reset(); m_meta.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid() && m_meta.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty() || m_meta.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid() && m_meta.is_ready(); }

    //  Derived views
    [[nodiscard]] CByteConstView const_view() const noexcept;
    [[nodiscard]] CByteView subview(const std::size_t offset, const std::size_t count) const noexcept;
    [[nodiscard]] CByteView head_to(const std::size_t count) const noexcept;
    [[nodiscard]] CByteView tail_from(const std::size_t offset) const noexcept;

    //  Accessors
    [[nodiscard]] std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<std::uint8_t*>(m_view.data()) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_view.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_meta.size : std::size_t{ 0 }; }

    //  Utilities
    void zero_fill() const noexcept;

private:
    memory::CMemoryView m_view;
    MetaByteView m_meta;
};

//==============================================================================
//  CByteConstView
//  Non-owning immutable view over a contiguous byte range.
//==============================================================================

class CByteConstView
{
public:

    //  Default lifetime
    CByteConstView() noexcept = default;
    CByteConstView(CByteConstView&&) noexcept = default;
    CByteConstView& operator=(CByteConstView&&) noexcept = default;
    CByteConstView(const CByteConstView&) noexcept = default;
    CByteConstView& operator=(const CByteConstView&) noexcept = default;
    ~CByteConstView() noexcept = default;

    //  Construction
    CByteConstView(const std::uint8_t* const data, const std::size_t size, const std::size_t align = 0u) noexcept { (void)set(data, size, align); }
    CByteConstView(const memory::CMemoryConstView& view, const MetaByteView& meta) noexcept { (void)set(view, meta); }

    //  View state
    CByteConstView& set(const std::uint8_t* const data, const std::size_t size, const std::size_t align = 0u) noexcept;
    CByteConstView& set(const memory::CMemoryConstView& view, const MetaByteView& meta) noexcept;
    CByteConstView& reset() noexcept { m_view.reset(); m_meta.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid() && m_meta.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty() || m_meta.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid() && m_meta.is_ready(); }

    //  Derived views
    [[nodiscard]] CByteConstView subview(const std::size_t offset, const std::size_t count) const noexcept;
    [[nodiscard]] CByteConstView head_to(const std::size_t count) const noexcept;
    [[nodiscard]] CByteConstView tail_from(const std::size_t offset) const noexcept;

    //  Accessors
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<const std::uint8_t*>(m_view.data()) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_view.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_meta.size : std::size_t{ 0 }; }

private:
    memory::CMemoryConstView m_view;
    MetaByteView m_meta;
};

//==============================================================================
//  MetaByteRectView
//  Metadata for a rectangular byte range.
//
//  Rect model:
//  - row_pitch is the byte step from one row start to the next.
//  - row_width is the active byte extent within each row.
//  - row_count is the number of rows.
//  - contiguous iff row_width == row_pitch.
//
//  State model:
//  - {0, 0, 0} is the canonical empty state.
//  - non-empty states require all three fields to be non-zero.
//  - ready requires row_width <= row_pitch and
//    row_pitch * row_count <= memory::k_byte_size_ceiling.
//==============================================================================

struct MetaByteRectView
{
    std::size_t row_pitch = 0u;
    std::size_t row_width = 0u;
    std::size_t row_count = 0u;
    void reset() noexcept { row_pitch = row_width = row_count = 0u; }
    [[nodiscard]] bool set(const std::size_t pitch, const std::size_t width, const std::size_t count) noexcept;
    [[nodiscard]] bool is_valid() const noexcept { return any_zero() ? all_zero() : range_ok(); }
    [[nodiscard]] bool is_empty() const noexcept { return any_zero(); }
    [[nodiscard]] bool is_ready() const noexcept { return any_zero() ? false : range_ok(); }
    [[nodiscard]] bool is_contiguous() const noexcept { return is_ready() && (row_width == row_pitch); }
    [[nodiscard]] std::size_t span_bytes() const noexcept { return is_ready() ? (row_width + (row_pitch * (row_count - 1u))) : std::size_t{ 0u }; }
    [[nodiscard]] std::size_t size_as_buffer() const noexcept { return is_contiguous() ? (row_pitch * row_count) : std::size_t{ 0 }; }
    [[nodiscard]] MetaByteView byte_view() const noexcept { return MetaByteView{ size_as_buffer() }; }
    [[nodiscard]] MetaByteRectView subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept;
private:
    [[nodiscard]] bool any_zero() const noexcept { return std::min(row_pitch, std::min(row_width, row_count)) == 0u; }
    [[nodiscard]] bool all_zero() const noexcept { return (row_pitch | row_width | row_count) == 0u; }
    [[nodiscard]] bool range_ok() const noexcept;
};

//==============================================================================
//  MetaByteRectBuffer
//  Owning rect buffers use the same metadata model as rect views.
//==============================================================================

using MetaByteRectBuffer = MetaByteRectView;

//==============================================================================
//  CByteRectBuffer
//  Owning rectangular byte buffer with aligned row starts.
//==============================================================================

class CByteRectBuffer
{
public:

    //  Default and deleted lifetime
    CByteRectBuffer() noexcept = default;
    CByteRectBuffer(const CByteRectBuffer&) noexcept = delete;
    CByteRectBuffer& operator=(const CByteRectBuffer&) noexcept = delete;

    //  Move/lifetime
    CByteRectBuffer(CByteRectBuffer&&) noexcept;
    CByteRectBuffer& operator=(CByteRectBuffer&&) noexcept;
    ~CByteRectBuffer() noexcept { deallocate(); };

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] bool is_contiguous() const noexcept { return is_ready() && m_meta.is_contiguous(); }

    //  Views
    [[nodiscard]] CByteRectView view() const noexcept;
    [[nodiscard]] CByteRectConstView const_view() const noexcept;

    //  Contiguous byte views
    [[nodiscard]] CByteView byte_view() const noexcept { return is_contiguous() ? CByteView{ data(), m_meta.size_as_buffer(), align() } : CByteView{}; }
    [[nodiscard]] CByteConstView byte_const_view() const noexcept { return is_contiguous() ? CByteConstView{ data(), m_meta.size_as_buffer(), align() } : CByteConstView{}; }

    //  Accessors
    [[nodiscard]] std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<std::uint8_t*>(const_cast<void*>(m_token.data())) : nullptr; }
    [[nodiscard]] std::uint8_t* row_data(const std::size_t y) const noexcept { return (is_ready() && (y < m_meta.row_count)) ? (data() + (y * m_meta.row_pitch)) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_token.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_pitch() const noexcept { return is_ready() ? m_meta.row_pitch : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_width() const noexcept { return is_ready() ? m_meta.row_width : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_count() const noexcept { return is_ready() ? m_meta.row_count : std::size_t{ 0 }; }

    //  Allocation and ownership
    [[nodiscard]] bool allocate(const std::size_t row_width, const std::size_t row_count, const std::size_t row_align = 0u, const bool zero = true) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t row_width, const std::size_t row_count, const std::size_t row_align = 0u, const bool zero_uninitialised = true) noexcept;
    [[nodiscard]] bool construct_and_copy_from(const CByteRectConstView& view) noexcept;
    void deallocate() noexcept;

    //  Utilities
    void zero_fill() const noexcept;

    //  Handoff
    memory::CMemoryToken disown(MetaByteRectBuffer& meta) noexcept;

    //  Direct storage attribution
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept;
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(memory::CMemoryContext* const context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(memory::CMemoryContext* const context = nullptr) noexcept;

private:
    friend class CErasedOwner;

    [[nodiscard]] memory::CMemoryContext* memory_source_context() const noexcept;
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

    memory::CMemoryToken m_token{ 1u, 1u };
    MetaByteRectBuffer m_meta;
};

//==============================================================================
//  CByteRectView
//  Non-owning mutable view over a rectangular byte range.
//==============================================================================

class CByteRectView
{
public:

    //  Default lifetime
    CByteRectView() noexcept = default;
    CByteRectView(CByteRectView&&) noexcept = default;
    CByteRectView& operator=(CByteRectView&&) noexcept = default;
    CByteRectView(const CByteRectView&) noexcept = default;
    CByteRectView& operator=(const CByteRectView&) noexcept = default;
    ~CByteRectView() noexcept = default;

    //  Construction
    CByteRectView(std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align = 0u) noexcept { (void)set(data, row_pitch, row_width, row_count, align); }
    CByteRectView(const memory::CMemoryView& view, const MetaByteRectView& meta) noexcept { (void)set(view, meta); }

    //  View state
    CByteRectView& set(std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align = 0u) noexcept;
    CByteRectView& set(const memory::CMemoryView& view, const MetaByteRectView& meta) noexcept;
    CByteRectView& reset() noexcept { m_view.reset(); m_meta.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid() && m_meta.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty() || m_meta.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid() && m_meta.is_ready(); }
    [[nodiscard]] bool is_contiguous() const noexcept { return m_view.is_valid() && m_meta.is_contiguous(); }

    //  Derived views
    [[nodiscard]] CByteRectConstView const_view() const noexcept;
    [[nodiscard]] CByteRectView subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept;

    //  Contiguous byte views
    [[nodiscard]] CByteView byte_view() const noexcept { return is_contiguous() ? CByteView{ data(), m_meta.size_as_buffer(), align() } : CByteView{}; }
    [[nodiscard]] CByteConstView byte_const_view() const noexcept { return is_contiguous() ? CByteConstView{ data(), m_meta.size_as_buffer(), align() } : CByteConstView{}; }

    //  Accessors
    [[nodiscard]] std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<std::uint8_t*>(m_view.data()) : nullptr; }
    [[nodiscard]] std::uint8_t* row_data(const std::size_t y) const noexcept { return (is_ready() && (y < m_meta.row_count)) ? (data() + (y * m_meta.row_pitch)) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_view.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_pitch() const noexcept { return is_ready() ? m_meta.row_pitch : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_width() const noexcept { return is_ready() ? m_meta.row_width : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_count() const noexcept { return is_ready() ? m_meta.row_count : std::size_t{ 0 }; }

    //  Utilities
    void zero_fill() const noexcept;

private:
    memory::CMemoryView m_view;
    MetaByteRectView m_meta;
};

//==============================================================================
//  CByteRectConstView
//  Non-owning immutable view over a rectangular byte range.
//==============================================================================

class CByteRectConstView
{
public:

    //  Default lifetime
    CByteRectConstView() noexcept = default;
    CByteRectConstView(CByteRectConstView&&) noexcept = default;
    CByteRectConstView& operator=(CByteRectConstView&&) noexcept = default;
    CByteRectConstView(const CByteRectConstView&) noexcept = default;
    CByteRectConstView& operator=(const CByteRectConstView&) noexcept = default;
    ~CByteRectConstView() noexcept = default;

    //  Construction
    CByteRectConstView(const std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align = 0u) noexcept { (void)set(data, row_pitch, row_width, row_count, align); }
    CByteRectConstView(const memory::CMemoryConstView& view, const MetaByteRectView& meta) noexcept { (void)set(view, meta); }

    //  View state
    CByteRectConstView& set(const std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align = 0u) noexcept;
    CByteRectConstView& set(const memory::CMemoryConstView& view, const MetaByteRectView& meta) noexcept;
    CByteRectConstView& reset() noexcept { m_view.reset(); m_meta.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid() && m_meta.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty() || m_meta.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid() && m_meta.is_ready(); }
    [[nodiscard]] bool is_contiguous() const noexcept { return m_view.is_valid() && m_meta.is_contiguous(); }

    //  Derived views
    [[nodiscard]] CByteRectConstView subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept;

    //  Contiguous byte views
    [[nodiscard]] CByteConstView byte_view() const noexcept { return is_contiguous() ? CByteConstView{ data(), m_meta.size_as_buffer(), align() } : CByteConstView{}; }

    //  Accessors
    [[nodiscard]] const std::uint8_t* data() const noexcept { return m_meta.is_ready() ? static_cast<const std::uint8_t*>(m_view.data()) : nullptr; }
    [[nodiscard]] const std::uint8_t* row_data(const std::size_t y) const noexcept { return (is_ready() && (y < m_meta.row_count)) ? (data() + (y * m_meta.row_pitch)) : nullptr; }
    [[nodiscard]] std::size_t align() const noexcept { return m_meta.is_ready() ? m_view.storage_alignment() : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_pitch() const noexcept { return is_ready() ? m_meta.row_pitch : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_width() const noexcept { return is_ready() ? m_meta.row_width : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t row_count() const noexcept { return is_ready() ? m_meta.row_count : std::size_t{ 0 }; }

private:
    memory::CMemoryConstView m_view;
    MetaByteRectView m_meta;
};

//==============================================================================
//  MetaByteRectView out of class function bodies
//==============================================================================

[[nodiscard]] inline bool MetaByteRectView::set(const std::size_t pitch, const std::size_t width, const std::size_t count) noexcept
{
    using memory::in_non_empty_range;
    if (in_non_empty_range(width, pitch) && in_non_empty_range(count, memory::max_elements(pitch)))
    {
        row_pitch = pitch;
        row_width = width;
        row_count = count;
        return true;
    }
    return false;
}

[[nodiscard]] inline MetaByteRectView MetaByteRectView::subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept
{
    if (is_ready() &&
        (std::max(x, (width - 1u)) < row_width) && (x <= (row_width - width)) &&
        (std::max(y, (height - 1u)) < row_count) && (y <= (row_count - height)))
    {
        return MetaByteRectView{ row_pitch, width, height };
    }
    return MetaByteRectView{};
}

[[nodiscard]] inline bool MetaByteRectView::range_ok() const noexcept
{
    using memory::in_non_empty_range;
    return
        in_non_empty_range(row_pitch, memory::k_byte_size_ceiling) &&
        in_non_empty_range(row_width, row_pitch) &&
        in_non_empty_range(row_count, memory::max_elements(row_pitch));
}

//==============================================================================
//  CByteBuffer out of class function bodies
//==============================================================================

inline CByteBuffer::CByteBuffer(CByteBuffer&& other) noexcept
{
    m_token = other.disown(m_meta);
}

inline CByteBuffer& CByteBuffer::operator=(CByteBuffer&& other) noexcept
{
    if (this != &other)
    {
        m_token = other.disown(m_meta);
    }
    return *this;
}

inline memory::CMemoryToken CByteBuffer::disown(MetaByteBuffer& meta) noexcept
{
    meta = m_meta;
    m_meta.reset();
    return std::move(m_token);
}

inline bool CByteBuffer::is_valid() const noexcept
{
    return m_meta.is_valid() && m_token.is_relocatable() &&
        (m_token.stride() == 1u) && (m_token.count() == m_meta.capacity);
}

inline bool CByteBuffer::is_empty() const noexcept
{
    return (m_token.data() == nullptr) || m_meta.is_empty();
}

inline bool CByteBuffer::is_ready() const noexcept
{
    return is_valid() && (m_token.data() != nullptr);
}

inline memory::CMemoryContext* CByteBuffer::memory_source_context() const noexcept
{
    return m_token.owns_storage() ? m_token.context() : nullptr;
}

inline void CByteBuffer::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

[[nodiscard]] inline CByteView CByteBuffer::view() const noexcept
{
    return (is_ready() && (m_meta.size != 0u)) ? CByteView{ data(), m_meta.size, align() } : CByteView{};
}

[[nodiscard]] inline CByteConstView CByteBuffer::const_view() const noexcept
{
    return (is_ready() && (m_meta.size != 0u)) ? CByteConstView{ data(), m_meta.size, align() } : CByteConstView{};
}

[[nodiscard]] inline bool CByteBuffer::append(const std::uint8_t* const data, const std::size_t size) noexcept
{
    if (data == nullptr)
    {
        return size == 0u;
    }

    if (ensure_free(size))
    {
        std::memcpy((this->data() + m_meta.size), data, size);
        m_meta.size += size;
        return true;
    }
    return false;
}

[[nodiscard]] inline bool CByteBuffer::append(const std::size_t size, const bool zero) noexcept
{
    if (size == 0u)
    {
        return is_valid();
    }

    if (ensure_free(size))
    {
        if (zero)
        {
            std::memset((data() + m_meta.size), 0, size);
        }
        m_meta.size += size;
        return true;
    }
    return false;
}

[[nodiscard]] inline bool CByteBuffer::set_size(const std::size_t size) noexcept
{
    if (is_ready() && (size <= m_meta.capacity))
    {
        m_meta.size = size;
        return true;
    }
    return false;
}

[[nodiscard]] inline bool CByteBuffer::allocate(const std::size_t capacity, const std::size_t align) noexcept
{
    return reallocate(0u, capacity, align);
}

[[nodiscard]] inline bool CByteBuffer::reallocate(const std::size_t size, const std::size_t capacity, const std::size_t align) noexcept
{
    if (is_valid() && (size <= capacity) && (capacity <= memory::k_byte_size_ceiling))
    {
        if (capacity == 0u)
        {
            deallocate();
            return true;
        }

        const std::size_t norm_align = bit_ops::reduce_alignment_to_pow2((align == 0u) ? m_token.storage_alignment() : align);
        if ((capacity == m_meta.capacity) && (norm_align <= m_token.storage_alignment()))
        {
            if (size > m_meta.size)
            {
                std::memset((data() + m_meta.size), 0, (size - m_meta.size));
            }
            m_meta.size = size;
            return true;
        }

        //  Reallocation path
        {
            memory::CMemoryToken token{ 1u, norm_align };
            if (token.allocate(capacity, false))
            {
                const std::size_t copy_size = std::min(size, m_meta.size);
                if (copy_size != 0u)
                {
                    std::memcpy(token.data(), data(), copy_size);
                }
                if (size > m_meta.size)
                {
                    std::memset(static_cast<std::uint8_t*>(token.data()) + m_meta.size, 0, (size - m_meta.size));
                }
                MetaByteBuffer meta{ size, capacity };
                m_token = std::move(token);
                m_meta = meta;
                MV_ASSERT(m_token.storage_alignment() == norm_align);
                MV_ASSERT(m_meta.size == size);
                MV_ASSERT(m_meta.capacity == capacity);
                MV_ASSERT(is_ready());
                return true;
            }
            return false;
        }
    }
    return false;
}

[[nodiscard]] inline bool CByteBuffer::resize(const std::size_t size, const std::size_t align) noexcept
{
    return (size <= memory::k_byte_size_ceiling) ?
        reallocate(size, ((size > m_meta.capacity) ? memory::buffer_growth_policy(size, memory::k_byte_size_ceiling) : m_meta.capacity), align) :
        false;
}

[[nodiscard]] inline bool CByteBuffer::reserve(const std::size_t minimum_capacity, const std::size_t align) noexcept
{
    return (minimum_capacity <= memory::k_byte_size_ceiling) ?
        reallocate(m_meta.size, std::max(memory::buffer_growth_policy(minimum_capacity, memory::k_byte_size_ceiling), m_meta.capacity), align) :
        false;
}

[[nodiscard]] inline bool CByteBuffer::ensure_free(const std::size_t extra, const std::size_t align) noexcept
{
    return (extra <= (memory::k_byte_size_ceiling - m_meta.size)) ? reserve((m_meta.size + extra), align) : false;
}

[[nodiscard]] inline bool CByteBuffer::shrink_to_fit() noexcept
{
    return reallocate(m_meta.size, m_meta.size, align());
}

[[nodiscard]] inline bool CByteBuffer::construct_and_copy_from(const CByteConstView& view) noexcept
{
    if (!view.is_valid())
    {
        return false;
    }

    if (view.is_empty())
    {
        deallocate();
        return true;
    }

    //  Construction and copy path
    {
        memory::CMemoryToken token{ 1u, view.align() };
        if (token.allocate(view.size(), false))
        {
            std::memcpy(token.data(), view.data(), view.size());
            MetaByteBuffer meta{ view.size(), view.size() };
            m_token = std::move(token);
            m_meta = meta;
            MV_ASSERT(m_token.storage_alignment() == view.align());
            MV_ASSERT(m_meta.size == view.size());
            MV_ASSERT(m_meta.capacity == view.size());
            MV_ASSERT(is_ready());
            return true;
        }
        return false;
    }
}

inline void CByteBuffer::deallocate() noexcept
{
    m_token.deallocate();
    m_meta.reset();
}

inline void CByteBuffer::zero_fill() const noexcept
{
    std::uint8_t* fill = data();
    if (fill != nullptr)
    {
        std::memset(fill, 0, m_meta.size);
    }
}

inline std::uint32_t CByteBuffer::memory_token_count() const noexcept
{
    return m_token.memory_token_count();
}

inline std::uint32_t CByteBuffer::memory_allocation_count() const noexcept
{
    return m_token.memory_allocation_count();
}

inline std::uint64_t CByteBuffer::memory_allocation_size() const noexcept
{
    return m_token.memory_allocation_size();
}

inline bool CByteBuffer::can_reattribute_to(memory::CMemoryContext* const context) const noexcept
{
    return m_token.can_reattribute_to(context);
}

inline bool CByteBuffer::reattribute(memory::CMemoryContext* const context) noexcept
{
    return m_token.reattribute(context);
}

//==============================================================================
//  CByteView out of class function bodies
//==============================================================================

inline CByteView& CByteView::set(std::uint8_t* const data, const std::size_t size, const std::size_t align) noexcept
{
    if (m_view.set(data, size, 1u, align))
    {
        m_meta.size = size;
    }
    else
    {
        reset();
    }
    return *this;
}

inline CByteView& CByteView::set(const memory::CMemoryView& view, const MetaByteView& meta) noexcept
{
    if (meta.is_ready() && view.is_valid() && (view.stride() == 1u) && view.contains_range(0u, meta.size))
    {
        m_view = view.subview(0u, meta.size);
        m_meta = meta;
    }
    else
    {
        reset();
    }
    return *this;
}

[[nodiscard]] inline CByteConstView CByteView::const_view() const noexcept
{
    return is_ready() ? CByteConstView{ m_view.const_view(), m_meta } : CByteConstView{};
}

[[nodiscard]] inline CByteView CByteView::subview(const std::size_t offset, const std::size_t count) const noexcept
{
    return (is_ready() && m_view.contains_range(offset, count))
        ? CByteView{ m_view.subview(offset, count), MetaByteView{ count } }
        : CByteView{};
}

[[nodiscard]] inline CByteView CByteView::head_to(const std::size_t count) const noexcept
{
    return (is_ready() && m_view.contains_range(0u, count))
        ? CByteView{ m_view.subview(0u, count), MetaByteView{ count } }
        : CByteView{};
}

[[nodiscard]] inline CByteView CByteView::tail_from(const std::size_t offset) const noexcept
{
    return (is_ready() && (m_meta.size > offset)) ? CByteView{ m_view.subview(offset), MetaByteView{ m_meta.size - offset } } : CByteView{};
}

inline void CByteView::zero_fill() const noexcept
{
    std::uint8_t* fill = data();
    if (fill != nullptr)
    {
        std::memset(fill, 0, m_meta.size);
    }
}

//==============================================================================
//  CByteConstView out of class function bodies
//==============================================================================

inline CByteConstView& CByteConstView::set(const std::uint8_t* const data, const std::size_t size, const std::size_t align) noexcept
{
    if (m_view.set(data, size, 1u, align))
    {
        m_meta.size = size;
    }
    else
    {
        reset();
    }
    return *this;
}

inline CByteConstView& CByteConstView::set(const memory::CMemoryConstView& view, const MetaByteView& meta) noexcept
{
    if (meta.is_ready() && view.is_valid() && (view.stride() == 1u) && view.contains_range(0u, meta.size))
    {
        m_view = view.subview(0u, meta.size);
        m_meta = meta;
    }
    else
    {
        reset();
    }
    return *this;
}

[[nodiscard]] inline CByteConstView CByteConstView::subview(const std::size_t offset, const std::size_t count) const noexcept
{
    return (is_ready() && m_view.contains_range(offset, count))
        ? CByteConstView{ m_view.subview(offset, count), MetaByteView{ count } }
        : CByteConstView{};
}

[[nodiscard]] inline CByteConstView CByteConstView::head_to(const std::size_t count) const noexcept
{
    return (is_ready() && m_view.contains_range(0u, count))
        ? CByteConstView{ m_view.subview(0u, count), MetaByteView{ count } }
        : CByteConstView{};
}

[[nodiscard]] inline CByteConstView CByteConstView::tail_from(const std::size_t offset) const noexcept
{
    return (is_ready() && (m_meta.size > offset)) ? CByteConstView{ m_view.subview(offset), MetaByteView{ m_meta.size - offset } } : CByteConstView{};
}

//==============================================================================
//  CByteRectBuffer out of class function bodies
//==============================================================================

inline CByteRectBuffer::CByteRectBuffer(CByteRectBuffer&& other) noexcept
{
    m_token = other.disown(m_meta);
}

inline CByteRectBuffer& CByteRectBuffer::operator=(CByteRectBuffer&& other) noexcept
{
    if (this != &other)
    {
        m_token = other.disown(m_meta);
    }
    return *this;
}

inline memory::CMemoryToken CByteRectBuffer::disown(MetaByteRectBuffer& meta) noexcept
{
    meta = m_meta;
    m_meta.reset();
    return std::move(m_token);
}

inline bool CByteRectBuffer::is_valid() const noexcept
{
    return m_meta.is_valid() && m_token.is_relocatable() &&
        (m_token.stride() == 1u) && (m_token.count() == (m_meta.row_pitch * m_meta.row_count));
}

inline bool CByteRectBuffer::is_empty() const noexcept
{
    return (m_token.data() == nullptr) || m_meta.is_empty();
}

inline bool CByteRectBuffer::is_ready() const noexcept
{
    return is_valid() && (m_token.data() != nullptr);
}

inline memory::CMemoryContext* CByteRectBuffer::memory_source_context() const noexcept
{
    return m_token.owns_storage() ? m_token.context() : nullptr;
}

inline void CByteRectBuffer::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

[[nodiscard]] inline CByteRectView CByteRectBuffer::view() const noexcept
{
    return is_ready() ? CByteRectView{ data(), m_meta.row_pitch, m_meta.row_width, m_meta.row_count, align() } : CByteRectView{};
}

[[nodiscard]] inline CByteRectConstView CByteRectBuffer::const_view() const noexcept
{
    return is_ready() ? CByteRectConstView{ data(), m_meta.row_pitch, m_meta.row_width, m_meta.row_count, align() } : CByteRectConstView{};
}

[[nodiscard]] inline bool CByteRectBuffer::allocate(const std::size_t row_width, const std::size_t row_count, const std::size_t row_align, const bool zero) noexcept
{
    if (reallocate(row_width, row_count, row_align, false))
    {
        if (zero)
        {
            std::memset(data(), 0, (m_meta.row_pitch * m_meta.row_count));
        }
        return true;
    }
    return false;
}

[[nodiscard]] inline bool CByteRectBuffer::reallocate(const std::size_t row_width, const std::size_t row_count, const std::size_t row_align, const bool zero_uninitialised) noexcept
{
    using memory::in_non_empty_range;
    if ((row_width == 0u) && (row_count == 0u))
    {   //  this is a deallocation request
        deallocate();
        return true;
    }

    if (in_non_empty_range(row_width, memory::k_byte_size_ceiling))
    {   //  row_width is valid
        const std::size_t use_align = bit_ops::reduce_alignment_to_pow2((row_align != 0u) ? row_align : m_token.storage_alignment());
        const std::size_t row_pitch = bit_ops::round_up_to_pow2_multiple(row_width, use_align);
        if (in_non_empty_range(row_count, memory::max_elements(row_pitch)))
        {   //  row_count is valid
            if ((use_align <= m_token.storage_alignment()) && (row_pitch == m_meta.row_pitch) && (row_width == m_meta.row_width) && (row_count == m_meta.row_count))
            {   //  nothing meaningful has changed that requires reallocation
                return true;
            }

            //  Reallocation path
            {   //  something meaningful has changed that requires reallocation
                const std::size_t bytes = row_pitch * row_count;
                memory::CMemoryToken token{ 1u, use_align };
                if (token.allocate(bytes, false))
                {   //  the allocation succeeded
                    if (is_ready())
                    {   //  true reallocation
                        const std::uint8_t* src_data = data();
                        std::uint8_t* dst_data = static_cast<std::uint8_t*>(token.data());
                        if ((row_width == row_pitch) && (row_width == m_meta.row_width) && (row_pitch == m_meta.row_pitch))
                        {   //  a single copy is possible
                            std::memcpy(dst_data, src_data, (row_pitch * std::min(row_count, m_meta.row_count)));
                        }
                        else
                        {   //  needs row by row copying and optional zeroing
                            const std::size_t copy_width = std::min(row_width, m_meta.row_width);
                            const std::size_t zero_width = zero_uninitialised ? (row_pitch - copy_width) : std::size_t{ 0 };
                            for (std::size_t count = std::min(row_count, m_meta.row_count); count != 0u; --count)
                            {
                                std::memcpy(dst_data, src_data, copy_width);
                                if (zero_width != 0u)
                                {
                                    std::memset((dst_data + copy_width), 0, zero_width);
                                }
                                src_data += m_meta.row_pitch;
                                dst_data += row_pitch;
                            }
                        }
                        if (zero_uninitialised && (row_count > m_meta.row_count))
                        {   //  there is extra accessible space to zero below the image
                            std::memset(static_cast<std::uint8_t*>(token.data()) + (row_pitch * m_meta.row_count), 0, (row_pitch * (row_count - m_meta.row_count)));
                        }
                    }
                    else if (zero_uninitialised)
                    {   //  primary allocation zero
                        std::memset(token.data(), 0, bytes);
                    }
                    MetaByteRectBuffer meta{ row_pitch, row_width, row_count };
                    m_token = std::move(token);
                    m_meta = meta;
                    MV_ASSERT(m_token.storage_alignment() == use_align);
                    MV_ASSERT(m_meta.row_pitch == row_pitch);
                    MV_ASSERT(m_meta.row_width == row_width);
                    MV_ASSERT(m_meta.row_count == row_count);
                    MV_ASSERT(is_ready());
                    return true;
                }
            }
        }
    }
    return false;
}

[[nodiscard]] inline bool CByteRectBuffer::construct_and_copy_from(const CByteRectConstView& view) noexcept
{
    if (!view.is_valid())
    {
        return false;
    }

    if (view.is_empty())
    {
        deallocate();
        return true;
    }

    //  Construction and copy path
    {
        const std::size_t row_align = view.align();
        const std::size_t row_width = view.row_width();
        const std::size_t row_count = view.row_count();
        const std::size_t row_pitch = bit_ops::round_up_to_pow2_multiple(row_width, row_align);
        memory::CMemoryToken token{ 1u, row_align };
        if (token.allocate((row_pitch * row_count), false))
        {
            const std::size_t src_pitch = view.row_pitch();
            const std::uint8_t* src_data = view.data();
            std::uint8_t* dst_data = static_cast<std::uint8_t*>(token.data());
            if ((row_width == row_pitch) && (row_pitch == src_pitch))
            {   //  a single copy is possible
                std::memcpy(dst_data, src_data, (row_pitch * row_count));
            }
            else
            {   //  needs row by row copying, with destination tail zeroing if present
                const std::size_t zero_width = row_pitch - row_width;
                for (std::size_t count = row_count; count != 0u; --count)
                {
                    std::memcpy(dst_data, src_data, row_width);
                    if (zero_width != 0u)
                    {
                        std::memset((dst_data + row_width), 0, zero_width);
                    }
                    src_data += src_pitch;
                    dst_data += row_pitch;
                }
            }
            MetaByteRectBuffer meta{ row_pitch, row_width, row_count };
            m_token = std::move(token);
            m_meta = meta;
            MV_ASSERT(m_token.storage_alignment() == row_align);
            MV_ASSERT(m_meta.row_pitch == row_pitch);
            MV_ASSERT(m_meta.row_width == row_width);
            MV_ASSERT(m_meta.row_count == row_count);
            MV_ASSERT(is_ready());
            return true;
        }
        return false;
    }
}

inline void CByteRectBuffer::deallocate() noexcept
{
    m_token.deallocate();
    m_meta.reset();
}

inline void CByteRectBuffer::zero_fill() const noexcept
{
    std::uint8_t* fill = data();
    if (fill != nullptr)
    {
        if (m_meta.is_contiguous())
        {
            std::memset(fill, 0, (m_meta.row_pitch * m_meta.row_count));
        }
        else
        {
            for (std::size_t count = m_meta.row_count; count != 0; --count)
            {
                std::memset(fill, 0, m_meta.row_width);
                fill += m_meta.row_pitch;
            }
        }
    }
}

inline std::uint32_t CByteRectBuffer::memory_token_count() const noexcept
{
    return m_token.memory_token_count();
}

inline std::uint32_t CByteRectBuffer::memory_allocation_count() const noexcept
{
    return m_token.memory_allocation_count();
}

inline std::uint64_t CByteRectBuffer::memory_allocation_size() const noexcept
{
    return m_token.memory_allocation_size();
}

inline bool CByteRectBuffer::can_reattribute_to(memory::CMemoryContext* const context) const noexcept
{
    return m_token.can_reattribute_to(context);
}

inline bool CByteRectBuffer::reattribute(memory::CMemoryContext* const context) noexcept
{
    return m_token.reattribute(context);
}

//==============================================================================
//  CByteRectView out of class function bodies
//==============================================================================

inline CByteRectView& CByteRectView::set(std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align) noexcept
{
    if (m_meta.set(row_pitch, row_width, row_count) &&
        m_view.set(data, m_meta.span_bytes(), 1u, align))
    {
        return *this;
    }
    return reset();
}

inline CByteRectView& CByteRectView::set(const memory::CMemoryView& view, const MetaByteRectView& meta) noexcept
{
    const std::size_t byte_count = meta.span_bytes();
    if (meta.is_ready() && view.is_valid() && (view.stride() == 1u) && view.contains_range(0u, byte_count))
    {
        m_view = view.subview(0u, byte_count);
        m_meta = meta;
    }
    else
    {
        reset();
    }
    return *this;
}

[[nodiscard]] inline CByteRectConstView CByteRectView::const_view() const noexcept
{
    return is_ready() ? CByteRectConstView{ data(), m_meta.row_pitch, m_meta.row_width, m_meta.row_count, align() } : CByteRectConstView{};
}

[[nodiscard]] inline CByteRectView CByteRectView::subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept
{
    if (is_ready())
    {
        const MetaByteRectView meta(m_meta.subview(x, y, width, height));
        if (meta.is_ready())
        {
            const std::size_t offset = x + (y * m_meta.row_pitch);
            return CByteRectView{ m_view.subview(offset, meta.span_bytes()), meta };
        }
    }
    return CByteRectView{};
}

inline void CByteRectView::zero_fill() const noexcept
{
    std::uint8_t* fill = data();
    if (fill != nullptr)
    {
        if (m_meta.is_contiguous())
        {
            std::memset(fill, 0, (m_meta.row_pitch * m_meta.row_count));
        }
        else
        {
            for (std::size_t count = m_meta.row_count; count != 0; --count)
            {
                std::memset(fill, 0, m_meta.row_width);
                fill += m_meta.row_pitch;
            }
        }
    }
}

//==============================================================================
//  CByteRectConstView out of class function bodies
//==============================================================================

inline CByteRectConstView& CByteRectConstView::set(const std::uint8_t* const data, const std::size_t row_pitch, const std::size_t row_width, const std::size_t row_count, const std::size_t align) noexcept
{
    if (m_meta.set(row_pitch, row_width, row_count) &&
        m_view.set(data, m_meta.span_bytes(), 1u, align))
    {
        return *this;
    }
    return reset();
}

inline CByteRectConstView& CByteRectConstView::set(const memory::CMemoryConstView& view, const MetaByteRectView& meta) noexcept
{
    const std::size_t byte_count = meta.span_bytes();
    if (meta.is_ready() && view.is_valid() && (view.stride() == 1u) && view.contains_range(0u, byte_count))
    {
        m_view = view.subview(0u, byte_count);
        m_meta = meta;
    }
    else
    {
        reset();
    }
    return *this;
}

[[nodiscard]] inline CByteRectConstView CByteRectConstView::subview(const std::size_t x, const std::size_t y, const std::size_t width, const std::size_t height) const noexcept
{
    if (is_ready())
    {
        const MetaByteRectView meta(m_meta.subview(x, y, width, height));
        if (meta.is_ready())
        {
            const std::size_t offset = x + (y * m_meta.row_pitch);
            return CByteRectConstView{ m_view.subview(offset, meta.span_bytes()), meta };
        }
    }
    return CByteRectConstView{};
}

#endif  //  #ifndef BYTE_BUFFERS_HPP_INCLUDED

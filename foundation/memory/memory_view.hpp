
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   memory_view.hpp
//  Author: Ritchie Brannan
//  Date:   13 Jul 26
//
//  Bounded non-owning views over contiguous strided memory.

#pragma once

#ifndef MEMORY_VIEW_HPP_INCLUDED
#define MEMORY_VIEW_HPP_INCLUDED

#include <algorithm>    //  std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t, std::uint16_t, std::uint32_t, std::uintptr_t
#include <limits>       //  std::numeric_limits
#include <type_traits>  //  std::is_trivially_copyable_v, std::is_trivially_destructible_v

#include "bit_utils/bit_ops.hpp"
#include "memory_policies.hpp"

namespace memory
{

class CMemoryConstView;

namespace view_detail
{

constexpr std::size_t k_max_stride = std::numeric_limits<std::uint16_t>::max();
constexpr std::size_t k_max_storage_alignment_log2 = 31u;

[[nodiscard]] inline bool valid_shape(const void* const data, const std::size_t count, const std::size_t stride) noexcept
{
    return (data != nullptr) &&
        (count != 0u) && (count <= std::numeric_limits<std::uint32_t>::max()) &&
        (stride != 0u) && (stride <= k_max_stride) &&
        (count <= memory::max_elements(stride));
}

[[nodiscard]] inline std::size_t normalize_storage_alignment(const void* const data, const std::size_t stride, const std::size_t requested_alignment) noexcept
{
    const std::size_t source = (requested_alignment != 0u) ? requested_alignment : stride;
    const std::size_t intent_alignment = bit_ops::reduce_alignment_to_pow2(source);
    if ((intent_alignment == 0u) ||
        (static_cast<std::size_t>(bit_ops::lo_bit_index(intent_alignment)) >
            k_max_storage_alignment_log2))
    {
        return 0u;
    }

    const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(data);
    return bit_ops::highest_common_alignment(intent_alignment, address);
}

[[nodiscard]] inline std::uint16_t alignment_log2(const std::size_t alignment) noexcept
{
    return static_cast<std::uint16_t>(bit_ops::lo_bit_index(alignment));
}

[[nodiscard]] inline std::size_t storage_alignment(const std::uint16_t alignment_log2) noexcept
{
    return std::size_t{ 1u } << alignment_log2;
}

[[nodiscard]] inline std::size_t element_alignment(const std::size_t storage_alignment, const std::size_t stride) noexcept
{
    const std::size_t natural_alignment = bit_ops::lo_bit_mask(stride);
    return std::min(storage_alignment, natural_alignment);
}

}   //  namespace view_detail

class CMemoryView
{
public:
    CMemoryView() noexcept = default;
    CMemoryView(void* const data, const std::size_t count, const std::size_t stride, const std::size_t storage_alignment = 0u) noexcept;

    [[nodiscard]] bool set(void* const data, const std::size_t count, const std::size_t stride, const std::size_t storage_alignment = 0u) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool is_valid() const noexcept { return m_data != nullptr; }
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

    [[nodiscard]] void* data() const noexcept { return m_data; }
    [[nodiscard]] std::size_t count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t stride() const noexcept { return m_stride; }
    [[nodiscard]] std::size_t bytes() const noexcept { return count() * stride(); }
    [[nodiscard]] std::size_t storage_alignment() const noexcept;
    [[nodiscard]] std::size_t element_alignment() const noexcept;

    [[nodiscard]] bool contains_index(const std::size_t index) const noexcept { return index < count(); }
    [[nodiscard]] bool contains_range(const std::size_t index, const std::size_t range_count) const noexcept;
    [[nodiscard]] void* index_ptr(const std::size_t index) const noexcept;

    [[nodiscard]] CMemoryView subview(const std::size_t index = 0u) const noexcept;
    [[nodiscard]] CMemoryView subview(const std::size_t index, const std::size_t subview_count) const noexcept;
    [[nodiscard]] CMemoryConstView const_view() const noexcept;

private:
    void*         m_data{ nullptr };
    std::uint32_t m_count{ 0u };
    std::uint16_t m_stride{ 0u };
    std::uint16_t m_storage_alignment_log2{ 0u };
};

class CMemoryConstView
{
public:
    CMemoryConstView() noexcept = default;
    CMemoryConstView(const void* const data, const std::size_t count, const std::size_t stride, const std::size_t storage_alignment = 0u) noexcept;
    CMemoryConstView(const CMemoryView& view) noexcept;

    CMemoryConstView& operator=(const CMemoryView& view) noexcept;

    [[nodiscard]] bool set(const void* const data, const std::size_t count, const std::size_t stride,
        const std::size_t storage_alignment = 0u) noexcept;
    [[nodiscard]] bool set(const CMemoryView& view) noexcept;
    void reset() noexcept;

    [[nodiscard]] bool is_valid() const noexcept { return m_data != nullptr; }
    [[nodiscard]] bool is_empty() const noexcept { return !is_valid(); }
    [[nodiscard]] explicit operator bool() const noexcept { return is_valid(); }

    [[nodiscard]] const void* data() const noexcept { return m_data; }
    [[nodiscard]] std::size_t count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t stride() const noexcept { return m_stride; }
    [[nodiscard]] std::size_t bytes() const noexcept { return count() * stride(); }
    [[nodiscard]] std::size_t storage_alignment() const noexcept;
    [[nodiscard]] std::size_t element_alignment() const noexcept;

    [[nodiscard]] bool contains_index(const std::size_t index) const noexcept { return index < count(); }
    [[nodiscard]] bool contains_range(const std::size_t index, const std::size_t range_count) const noexcept;
    [[nodiscard]] const void* index_ptr(const std::size_t index) const noexcept;

    [[nodiscard]] CMemoryConstView subview(const std::size_t index = 0u) const noexcept;
    [[nodiscard]] CMemoryConstView subview(const std::size_t index, const std::size_t subview_count) const noexcept;

private:
    const void*   m_data{ nullptr };
    std::uint32_t m_count{ 0u };
    std::uint16_t m_stride{ 0u };
    std::uint16_t m_storage_alignment_log2{ 0u };
};

static_assert(sizeof(void*) != 8u || sizeof(CMemoryView) == 16u,
    "CMemoryView must occupy 16 bytes on a 64-bit target");
static_assert(sizeof(void*) != 4u || sizeof(CMemoryView) == 12u,
    "CMemoryView must occupy 12 bytes on a 32-bit target");
static_assert(sizeof(CMemoryConstView) == sizeof(CMemoryView),
    "Mutable and const memory views must have matching layouts");
static_assert(std::is_trivially_copyable_v<CMemoryView> && std::is_trivially_destructible_v<CMemoryView>,
    "CMemoryView must remain a trivial non-owning descriptor");
static_assert(std::is_trivially_copyable_v<CMemoryConstView> && std::is_trivially_destructible_v<CMemoryConstView>,
    "CMemoryConstView must remain a trivial non-owning descriptor");

//==============================================================================
//  CMemoryView out of class function bodies
//==============================================================================

inline CMemoryView::CMemoryView(
    void* const data,
    const std::size_t count,
    const std::size_t stride,
    const std::size_t storage_alignment) noexcept
{
    (void)set(data, count, stride, storage_alignment);
}

inline bool CMemoryView::set(void* const data, const std::size_t count, const std::size_t stride, const std::size_t storage_alignment) noexcept
{
    if (!view_detail::valid_shape(data, count, stride))
    {
        reset();
        return false;
    }

    const std::size_t normalized_alignment =
        view_detail::normalize_storage_alignment(data, stride, storage_alignment);
    if (normalized_alignment == 0u)
    {
        reset();
        return false;
    }

    m_data = data;
    m_count = static_cast<std::uint32_t>(count);
    m_stride = static_cast<std::uint16_t>(stride);
    m_storage_alignment_log2 = view_detail::alignment_log2(normalized_alignment);
    return true;
}

inline void CMemoryView::reset() noexcept
{
    m_data = nullptr;
    m_count = 0u;
    m_stride = 0u;
    m_storage_alignment_log2 = 0u;
}

inline std::size_t CMemoryView::storage_alignment() const noexcept
{
    return is_valid() ? view_detail::storage_alignment(m_storage_alignment_log2) : std::size_t{ 0u };
}

inline std::size_t CMemoryView::element_alignment() const noexcept
{
    return is_valid() ? view_detail::element_alignment(storage_alignment(), stride()) : std::size_t{ 0u };
}

inline bool CMemoryView::contains_range(const std::size_t index, const std::size_t range_count) const noexcept
{
    return (range_count != 0u) && (index < count()) && (range_count <= (count() - index));
}

inline void* CMemoryView::index_ptr(const std::size_t index) const noexcept
{
    return contains_index(index)
        ? static_cast<void*>(static_cast<std::uint8_t*>(m_data) + (index * stride()))
        : nullptr;
}

inline CMemoryView CMemoryView::subview(const std::size_t index) const noexcept
{
    return contains_index(index) ? subview(index, count() - index) : CMemoryView{};
}

inline CMemoryView CMemoryView::subview(const std::size_t index, const std::size_t subview_count) const noexcept
{
    if (!contains_range(index, subview_count))
    {
        return {};
    }
    return CMemoryView{ index_ptr(index), subview_count, stride(), storage_alignment() };
}

inline CMemoryConstView CMemoryView::const_view() const noexcept
{
    return CMemoryConstView{ *this };
}

//==============================================================================
//  CMemoryConstView out of class function bodies
//==============================================================================

inline CMemoryConstView::CMemoryConstView(
    const void* const data,
    const std::size_t count,
    const std::size_t stride,
    const std::size_t storage_alignment) noexcept
{
    (void)set(data, count, stride, storage_alignment);
}

inline CMemoryConstView::CMemoryConstView(const CMemoryView& view) noexcept
{
    (void)set(view);
}

inline CMemoryConstView& CMemoryConstView::operator=(const CMemoryView& view) noexcept
{
    (void)set(view);
    return *this;
}

inline bool CMemoryConstView::set(const void* const data, const std::size_t count, const std::size_t stride, const std::size_t storage_alignment) noexcept
{
    if (!view_detail::valid_shape(data, count, stride))
    {
        reset();
        return false;
    }

    const std::size_t normalized_alignment =
        view_detail::normalize_storage_alignment(data, stride, storage_alignment);
    if (normalized_alignment == 0u)
    {
        reset();
        return false;
    }

    m_data = data;
    m_count = static_cast<std::uint32_t>(count);
    m_stride = static_cast<std::uint16_t>(stride);
    m_storage_alignment_log2 = view_detail::alignment_log2(normalized_alignment);
    return true;
}

inline bool CMemoryConstView::set(const CMemoryView& view) noexcept
{
    if (!view.is_valid())
    {
        reset();
        return false;
    }
    m_data = view.data();
    m_count = static_cast<std::uint32_t>(view.count());
    m_stride = static_cast<std::uint16_t>(view.stride());
    m_storage_alignment_log2 = view_detail::alignment_log2(view.storage_alignment());
    return true;
}

inline void CMemoryConstView::reset() noexcept
{
    m_data = nullptr;
    m_count = 0u;
    m_stride = 0u;
    m_storage_alignment_log2 = 0u;
}

inline std::size_t CMemoryConstView::storage_alignment() const noexcept
{
    return is_valid() ? view_detail::storage_alignment(m_storage_alignment_log2) : std::size_t{ 0u };
}

inline std::size_t CMemoryConstView::element_alignment() const noexcept
{
    return is_valid() ? view_detail::element_alignment(storage_alignment(), stride()) : std::size_t{ 0u };
}

inline bool CMemoryConstView::contains_range(const std::size_t index, const std::size_t range_count) const noexcept
{
    return (range_count != 0u) && (index < count()) && (range_count <= (count() - index));
}

inline const void* CMemoryConstView::index_ptr(const std::size_t index) const noexcept
{
    return contains_index(index)
        ? static_cast<const void*>(static_cast<const std::uint8_t*>(m_data) + (index * stride()))
        : nullptr;
}

inline CMemoryConstView CMemoryConstView::subview(const std::size_t index) const noexcept
{
    return contains_index(index) ? subview(index, count() - index) : CMemoryConstView{};
}

inline CMemoryConstView CMemoryConstView::subview(const std::size_t index, const std::size_t subview_count) const noexcept
{
    if (!contains_range(index, subview_count))
    {
        return {};
    }
    return CMemoryConstView{ index_ptr(index), subview_count, stride(), storage_alignment() };
}

}   //  namespace memory

#endif  //  #ifndef MEMORY_VIEW_HPP_INCLUDED

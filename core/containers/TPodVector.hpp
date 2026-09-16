
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TPodVector.hpp
//  Author: Ritchie Brannan
//  Date:   20 Mar 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  Contiguous tightly packed storage for trivially copyable T with
//  owning vector and non-owning typed views.
//
//  Does not construct or destroy elements and does not support
//  non-trivial relocation semantics.
//
//  IMPORTANT TERMINOLOGY NOTE
//  --------------------------
//  Storage is interpreted as tightly packed T[] with no per-element
//  padding and element stride sizeof(T).
//
//  Reallocation preserves only the logical element range [0, size).
//  Spare capacity is not part of the logical range and is not preserved.
//
//  See docs/TPodVector.md for the full documentation.

#pragma once

#ifndef TPOD_VECTOR_HPP_INCLUDED
#define TPOD_VECTOR_HPP_INCLUDED

#include <algorithm>    //  std::max, std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t
#include <cstring>      //  std::memcpy, std::memmove, std::memset
#include <type_traits>  //  std::is_const_v, std::is_trivially_copyable_v
#include <utility>      //  std::move

#include "memory/memory_policies.hpp"
#include "memory/memory_token.hpp"
#include "memory/memory_view.hpp"
#include "ByteBuffers.hpp"
#include "debug/macros.hpp"

//==============================================================================
//  Forward declarations
//==============================================================================

template<typename T> class TPodVector;
template<typename T> class TPodView;
template<typename T> class TPodConstView;
template<typename T> class TPodUnorderedSlotsStorage;
template<typename T, typename TKey> class TPodOrderedSlotsStorage;
template<typename T> class TUnorderedCollectionStorage;
template<typename T, typename TKey> class TOrderedCollectionStorage;
class CStableStrings;

//==============================================================================
//  Shared POD storage helpers
//==============================================================================

template<typename T>
[[nodiscard]] inline bool pod_storage_contains_ptr(
    const T* const storage,
    const std::size_t capacity,
    const T* const ptr) noexcept
{
    if ((storage == nullptr) || (ptr == nullptr) || (capacity == 0u))
    {
        return false;
    }

    const std::uintptr_t storage_address = reinterpret_cast<std::uintptr_t>(storage);
    const std::uintptr_t ptr_address = reinterpret_cast<std::uintptr_t>(ptr);
    return (ptr_address >= storage_address) && ((ptr_address - storage_address) < (capacity * sizeof(T)));
}

//==============================================================================
//  OOB PANIC helpers
//==============================================================================

template<typename T>
T& pod_vector_oob_ref() noexcept
{
    static_assert(!std::is_const_v<T>, "pod_vector_oob_ref<T>() requires non-const T.");
    MV_ASSERT(false);
    static T last_gasp{};
    std::memset(&last_gasp, 0, sizeof(T));
    return last_gasp;
}

template<typename T>
const T& pod_vector_oob_const_ref() noexcept
{
    static_assert(!std::is_const_v<T>, "pod_vector_oob_const_ref<T>() requires non-const T.");
    MV_ASSERT(false);
    static T last_gasp{};
    std::memset(&last_gasp, 0, sizeof(T));
    return last_gasp;
}

template<typename T>
T& pod_vector_empty_ref() noexcept
{
    static_assert(!std::is_const_v<T>, "pod_vector_empty_ref<T>() requires non-const T.");
    MV_ASSERT(false);
    static T last_gasp{};
    std::memset(&last_gasp, 0, sizeof(T));
    return last_gasp;
}


template<typename T>
const T& pod_vector_empty_const_ref() noexcept
{
    static_assert(!std::is_const_v<T>, "pod_vector_empty_const_ref<T>() requires non-const T.");
    MV_ASSERT(false);
    static T last_gasp{};
    std::memset(&last_gasp, 0, sizeof(T));
    return last_gasp;
}

//==============================================================================
//  TPodVector<T>
//  Owning unique vector-like container.
//==============================================================================

template<typename T>
class TPodVector
{
private:
    static_assert(!std::is_const_v<T>, "TPodVector<T> requires non-const T.");
    static_assert(std::is_trivially_copyable_v<T>, "TPodVector<T> requires trivially copyable T.");

public:

    //  Default and deleted lifetime
    TPodVector() noexcept = default;
    TPodVector(const TPodVector&) noexcept = delete;
    TPodVector& operator=(const TPodVector&) noexcept = delete;

    //  Move lifetime
    TPodVector(TPodVector&&) noexcept;
    TPodVector& operator=(TPodVector&&) noexcept;

    //  Destructor
    ~TPodVector() noexcept { deallocate(); }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept;
    [[nodiscard]] bool is_ready() const noexcept;

    //  Views
    [[nodiscard]] TPodView<T> view() noexcept;
    [[nodiscard]] TPodConstView<T> view() const noexcept;
    [[nodiscard]] TPodConstView<T> const_view() const noexcept;

    //  Accessors
    [[nodiscard]] T* data() noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] const T* data() const noexcept { return is_ready() ? raw_data() : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return is_ready() ? m_size : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t capacity() const noexcept { return is_ready() ? m_capacity : std::size_t{ 0 }; }
    [[nodiscard]] std::size_t available() const noexcept { return is_ready() ? (m_capacity - m_size) : std::size_t{ 0 }; }

    //  Element accessors
    [[nodiscard]] T& first() noexcept { return (is_ready() && (m_size != 0u)) ? raw_data()[0] : pod_vector_empty_ref<T>(); }
    [[nodiscard]] T& last() noexcept { return (is_ready() && (m_size != 0u)) ? raw_data()[m_size - 1u] : pod_vector_empty_ref<T>(); }
    [[nodiscard]] T& operator[](const std::size_t index) noexcept { return (is_ready() && (index < m_size)) ? raw_data()[index] : pod_vector_oob_ref<T>(); }
    [[nodiscard]] const T& first() const noexcept { return (is_ready() && (m_size != 0u)) ? raw_data()[0] : pod_vector_empty_const_ref<T>(); }
    [[nodiscard]] const T& last() const noexcept { return (is_ready() && (m_size != 0u)) ? raw_data()[m_size - 1u] : pod_vector_empty_const_ref<T>(); }
    [[nodiscard]] const T& operator[](const std::size_t index) const noexcept { return (is_ready() && (index < m_size)) ? raw_data()[index] : pod_vector_oob_const_ref<T>(); }

    //  Vector operations
    void clear() noexcept { m_size = 0u; }
    [[nodiscard]] bool push_back(const T& item) noexcept { return push_back(&item); }
    [[nodiscard]] bool push_back(const T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool push_back(const TPodConstView<T>& src) noexcept { return src.is_valid() && push_back(src.data(), src.size()); }
    [[nodiscard]] T* push_back_zeroed(const std::size_t count = 1u) noexcept;
    [[nodiscard]] T* push_back_uninit(const std::size_t count = 1u) noexcept;
    [[nodiscard]] std::size_t try_push_back(const T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] std::size_t try_push_back(const TPodConstView<T>& src) noexcept { return src.is_valid() ? try_push_back(src.data(), src.size()) : std::size_t{ 0u }; }
    [[nodiscard]] bool pop_back(T& item) noexcept { return pop_back(&item); }
    [[nodiscard]] bool pop_back(T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool pop_back(const TPodView<T>& dst) noexcept { return dst.is_valid() && pop_back(dst.data(), dst.size()); }
    [[nodiscard]] bool pop_back_preserve_order(T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool pop_back_preserve_order(const TPodView<T>& dst) noexcept { return dst.is_valid() && pop_back_preserve_order(dst.data(), dst.size()); }
    [[nodiscard]] bool discard_back(const std::size_t count = 1u) noexcept;
    [[nodiscard]] std::size_t try_pop_back(T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] std::size_t try_pop_back(const TPodView<T>& dst) noexcept { return dst.is_valid() ? try_pop_back(dst.data(), dst.size()) : std::size_t{ 0u }; }
    [[nodiscard]] std::size_t try_discard_back(const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool insert(const std::size_t index, const T& item) noexcept { return insert(index, &item); }
    [[nodiscard]] bool insert(const std::size_t index, const T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] T* insert_zeroed(const std::size_t index, const std::size_t count = 1u) noexcept;
    [[nodiscard]] T* insert_uninit(const std::size_t index, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool erase(const std::size_t index, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool swap_insert(const std::size_t index, const T& item) noexcept { return swap_insert(index, &item); }
    [[nodiscard]] bool swap_insert(const std::size_t index, const T* const items, const std::size_t count = 1u) noexcept;
    [[nodiscard]] bool swap_erase(const std::size_t index, const std::size_t count = 1u) noexcept;

    //  Logical size adjustment
    [[nodiscard]] bool set_size(const std::size_t size) noexcept { if (size <= m_capacity) { m_size = size; return true; } return false; }

    //  Allocation and capacity management
    [[nodiscard]] bool allocate(const std::size_t capacity) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t capacity) noexcept { return reallocate(size(), capacity); }
    [[nodiscard]] bool reallocate(const std::size_t size, const std::size_t capacity) noexcept;
    [[nodiscard]] bool resize(const std::size_t size) noexcept;
    [[nodiscard]] bool reserve(const std::size_t minimum_capacity) noexcept;
    [[nodiscard]] bool ensure_free(const std::size_t extra) noexcept;
    [[nodiscard]] bool shrink_to_fit() noexcept;
    void deallocate() noexcept { m_token.deallocate(); m_size = 0u; m_capacity = 0u; }

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();

//  Interface for memory accounting and ownership-transfer infrastructure.
public:

    //  Observe all owned backing storage without changing its attribution.
    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept;

    //  Requires completed source/allocator preflight and accounting adjustment.
    //  Replace all owned contexts, including unallocated members.
    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept;

private:
    static constexpr std::size_t k_max_bytes = k_max_elements * k_element_size;

    [[nodiscard]] T* raw_data() noexcept { return static_cast<T*>(m_token.data()); }
    [[nodiscard]] const T* raw_data() const noexcept { return static_cast<const T*>(m_token.data()); }
    [[nodiscard]] bool is_internal_ptr(const T* const ptr) const noexcept;
    void move_from(TPodVector& src) noexcept;

    static_assert(k_element_size <= 0xffffu, "TPodVector<T> element size exceeds the memory token stride field.");

    memory::CMemoryToken m_token{ k_element_size, k_align };
    std::size_t m_size = 0u;
    std::size_t m_capacity = 0u;
};

//==============================================================================
//  TPodView<T>
//  Non-owning view over a contiguous mutable POD range.
//==============================================================================

template<typename T>
class TPodView
{
private:
    static_assert(std::is_trivially_copyable_v<T>, "TPodView<T> requires trivially copyable T.");
    static_assert(!std::is_const_v<T>, "TPodView<T> requires non-const T.");

public:

    //  Default lifetime
    TPodView() noexcept = default;
    TPodView(const TPodView&) noexcept = default;
    TPodView& operator=(const TPodView&) noexcept = default;
    ~TPodView() noexcept = default;

    //  Construction
    explicit TPodView(const CByteView& view) noexcept { (void)set(view); }
    TPodView(T* const data, const std::size_t size) noexcept { (void)set(data, size); }
    TPodView(const memory::CMemoryView& view, const std::size_t size) noexcept { (void)set(view, size); }

    //  View state
    TPodView& set(const CByteView& view) noexcept;
    TPodView& set(T* const data, const std::size_t size) noexcept;
    TPodView& set(const memory::CMemoryView& view, const std::size_t size) noexcept;
    TPodView& reset() noexcept { m_view.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid(); }

    //  Derived views
    [[nodiscard]] TPodConstView<T> const_view() const noexcept { return is_ready() ? TPodConstView<T>{ m_view.const_view(), size() } : TPodConstView<T>{}; }
    [[nodiscard]] TPodView subview(const std::size_t offset, const std::size_t count) const noexcept;
    [[nodiscard]] TPodView head_to(const std::size_t count) const noexcept;
    [[nodiscard]] TPodView tail_from(const std::size_t offset) const noexcept;

    //  Accessors
    [[nodiscard]] T* data() const noexcept { return is_ready() ? static_cast<T*>(m_view.data()) : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return m_view.count(); }

    //  Element accessors
    T& operator[](const std::size_t index) noexcept { return (index < size()) ? data()[index] : pod_vector_oob_ref<T>(); }
    const T& operator[](const std::size_t index) const noexcept { return (index < size()) ? data()[index] : pod_vector_oob_const_ref<T>(); }

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();

private:
    static constexpr std::size_t k_max_bytes = k_max_elements * k_element_size;

    static_assert((k_element_size <= 0xffffu), "TPodView<T> element size exceeds the memory view stride field.");

    memory::CMemoryView m_view{};
};

//==============================================================================
//  TPodConstView<T>
//  Non-owning view over a contiguous immutable POD range.
//==============================================================================

template<typename T>
class TPodConstView
{
private:
    static_assert(std::is_trivially_copyable_v<T>, "TPodConstView<T> requires trivially copyable T.");
    static_assert(!std::is_const_v<T>, "TPodConstView<T> requires non-const T.");

public:

    //  Default lifetime
    TPodConstView() noexcept = default;
    TPodConstView(const TPodConstView&) noexcept = default;
    TPodConstView& operator=(const TPodConstView&) noexcept = default;
    ~TPodConstView() noexcept = default;

    //  Construction
    explicit TPodConstView(const CByteView& view) noexcept { (void)set(view); }
    explicit TPodConstView(const CByteConstView& view) noexcept { (void)set(view); }
    TPodConstView(const T* const data, const std::size_t size) noexcept { (void)set(data, size); }
    TPodConstView(const memory::CMemoryView& view, const std::size_t size) noexcept { (void)set(view, size); }
    TPodConstView(const memory::CMemoryConstView& view, const std::size_t size) noexcept { (void)set(view, size); }

    //  View state
    TPodConstView& set(const CByteView& view) noexcept;
    TPodConstView& set(const CByteConstView& view) noexcept;
    TPodConstView& set(const T* const data, const std::size_t size) noexcept;
    TPodConstView& set(const memory::CMemoryView& view, const std::size_t size) noexcept;
    TPodConstView& set(const memory::CMemoryConstView& view, const std::size_t size) noexcept;
    TPodConstView& reset() noexcept { m_view.reset(); return *this; }

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_view.is_valid(); }
    [[nodiscard]] bool is_empty() const noexcept { return m_view.is_empty(); }
    [[nodiscard]] bool is_ready() const noexcept { return m_view.is_valid(); }

    //  Derived views
    [[nodiscard]] TPodConstView subview(const std::size_t offset, const std::size_t count) const noexcept;
    [[nodiscard]] TPodConstView head_to(const std::size_t count) const noexcept;
    [[nodiscard]] TPodConstView tail_from(const std::size_t offset) const noexcept;

    //  Accessors
    [[nodiscard]] const T* data() const noexcept { return is_ready() ? static_cast<const T*>(m_view.data()) : nullptr; }
    [[nodiscard]] std::size_t size() const noexcept { return m_view.count(); }

    //  Element accessors
    const T& operator[](const std::size_t index) const noexcept { return (index < size()) ? data()[index] : pod_vector_oob_const_ref<T>(); }

    //  Constants
    static constexpr std::size_t k_element_size = sizeof(T);
    static constexpr std::size_t k_align = memory::t_default_align<T>();
    static constexpr std::size_t k_max_elements = memory::t_max_elements<T>();

private:
    static constexpr std::size_t k_max_bytes = k_max_elements * k_element_size;

    static_assert(k_element_size <= 0xffffu, "TPodConstView<T> element size exceeds the memory view stride field.");

    memory::CMemoryConstView m_view{};
};

//==============================================================================
//  TPodVector<T> out of class function bodies
//==============================================================================

template<typename T>
inline TPodVector<T>::TPodVector(TPodVector&& src) noexcept
{
    move_from(src);
}

template<typename T>
inline TPodVector<T>& TPodVector<T>::operator=(TPodVector&& src) noexcept
{
    if (this != &src)
    {
        move_from(src);
    }
    return *this;
}

template<typename T>
inline bool TPodVector<T>::is_valid() const noexcept
{
    if (!m_token.is_relocatable() ||
        (m_token.stride() != k_element_size) || (m_token.storage_alignment() != k_align) ||
        (m_token.count() != m_capacity))
    {
        return false;
    }
    return (raw_data() != nullptr) ?
        ((m_size <= m_capacity) && memory::in_non_empty_range(m_capacity, k_max_elements)) :
        ((m_size | m_capacity) == 0u);
}

template<typename T>
inline bool TPodVector<T>::is_empty() const noexcept
{
    return (raw_data() == nullptr) || (m_size == 0u) || (m_capacity == 0u);
}

template<typename T>
inline bool TPodVector<T>::is_ready() const noexcept
{
    return is_valid() && (raw_data() != nullptr);
}

template<typename T>
inline memory::SMemoryAttribution TPodVector<T>::memory_attribution() const noexcept
{
    return memory::observe_memory_attribution(m_token);
}

template<typename T>
inline void TPodVector<T>::unsafe_replace_memory_context_without_accounting(
    memory::CMemoryContext* const expected_source,
    memory::CMemoryContext* const target) noexcept
{
    m_token.unsafe_replace_context_without_accounting(expected_source, target);
}

template<typename T>
inline TPodView<T> TPodVector<T>::view() noexcept
{
    return is_ready() ? TPodView<T>{ m_token.view(), m_size } : TPodView<T>{};
}

template<typename T>
inline TPodConstView<T> TPodVector<T>::view() const noexcept
{
    return const_view();
}

template<typename T>
inline TPodConstView<T> TPodVector<T>::const_view() const noexcept
{
    return is_ready() ? TPodConstView<T>{ m_token.const_view(), m_size } : TPodConstView<T>{};
}

template<typename T>
inline bool TPodVector<T>::push_back(const T* const items, const std::size_t count) noexcept
{
    if ((items == nullptr) || is_internal_ptr(items) || !memory::in_non_empty_range(count, (k_max_elements - m_size)))
    {
        return count == 0u;
    }
    if (ensure_free(count))
    {
        T* ptr = raw_data() + m_size;
        std::memcpy(ptr, items, (count * k_element_size));
        m_size += count;
        return true;
    }
    return false;
}

template<typename T>
inline T* TPodVector<T>::push_back_zeroed(const std::size_t count) noexcept
{
    T* ptr = push_back_uninit(count);
    if (ptr != nullptr)
    {
        std::memset(ptr, 0, (count * k_element_size));
        return ptr;
    }
    return nullptr;
}

template<typename T>
inline T* TPodVector<T>::push_back_uninit(const std::size_t count) noexcept
{
    if ((count != 0u) && ensure_free(count))
    {
        T* ptr = raw_data() + m_size;
        m_size += count;
        return ptr;
    }
    return nullptr;
}

template<typename T>
inline std::size_t TPodVector<T>::try_push_back(const T* const items, const std::size_t count) noexcept
{
    const std::size_t push_back_count = std::min(count, available());
    return ((push_back_count != 0u) && push_back(items, push_back_count)) ? push_back_count : std::size_t{ 0 };
}

template<typename T>
inline bool TPodVector<T>::pop_back(T* const items, const std::size_t count) noexcept
{
    if ((items == nullptr) || is_internal_ptr(items) || !memory::in_non_empty_range(count, m_size))
    {
        return count == 0u;
    }
    if (is_ready())
    {
        const T* src = raw_data() + m_size;
        T* dst = items;
        for (std::size_t copy_count = count; copy_count > 0u; --copy_count)
        {
            --src;
            std::memcpy(dst, src, k_element_size);
            ++dst;
        }
        m_size -= count;
        return true;
    }
    return false;
}

template<typename T>
inline bool TPodVector<T>::pop_back_preserve_order(T* const items, const std::size_t count) noexcept
{
    if ((items == nullptr) || is_internal_ptr(items) || !memory::in_non_empty_range(count, m_size))
    {
        return count == 0u;
    }
    if (is_ready())
    {
        m_size -= count;
        T* ptr = raw_data() + m_size;
        std::memcpy(items, ptr, (count * k_element_size));
        return true;
    }
    return false;
}

template<typename T>
inline bool TPodVector<T>::discard_back(const std::size_t count) noexcept
{
    if (!memory::in_non_empty_range(count, m_size))
    {
        return count == 0u;
    }
    if (is_ready())
    {
        m_size -= count;
        return true;
    }
    return false;
}

template<typename T>
inline std::size_t TPodVector<T>::try_pop_back(T* const items, const std::size_t count) noexcept
{
    const std::size_t pop_back_count = std::min(count, size());
    return ((pop_back_count != 0u) && pop_back(items, pop_back_count)) ? pop_back_count : std::size_t{ 0 };
}

template<typename T>
inline std::size_t TPodVector<T>::try_discard_back(const std::size_t count) noexcept
{
    const std::size_t pop_back_count = std::min(count, size());
    return ((pop_back_count != 0u) && discard_back(pop_back_count)) ? pop_back_count : std::size_t{ 0 };
}

template<typename T>
inline bool TPodVector<T>::insert(const std::size_t index, const T* const items, const std::size_t count) noexcept
{
    if ((items == nullptr) || is_internal_ptr(items) || (index > m_size) || !memory::in_non_empty_range(count, (k_max_elements - m_size)))
    {
        return (index <= m_size) && (count == 0u);
    }
    if (ensure_free(count))
    {
        T* ptr = raw_data() + index;
        if (m_size != index)
        {
            std::memmove((ptr + count), ptr, ((m_size - index) * k_element_size));
        }
        std::memcpy(ptr, items, (count * k_element_size));
        m_size += count;
        return true;
    }
    return false;
}

template<typename T>
inline T* TPodVector<T>::insert_zeroed(const std::size_t index, const std::size_t count) noexcept
{
    T* const ptr = insert_uninit(index, count);
    if (ptr != nullptr)
    {
        std::memset(ptr, 0, (count * k_element_size));
    }
    return ptr;
}

template<typename T>
inline T* TPodVector<T>::insert_uninit(const std::size_t index, const std::size_t count) noexcept
{
    if ((index <= m_size) && memory::in_non_empty_range(count, (k_max_elements - m_size)) && ensure_free(count))
    {
        T* ptr = raw_data() + index;
        if (m_size != index)
        {
            std::memmove((ptr + count), ptr, ((m_size - index) * k_element_size));
        }
        m_size += count;
        return ptr;
    }
    return nullptr;
}

template<typename T>
inline bool TPodVector<T>::erase(const std::size_t index, const std::size_t count) noexcept
{
    if ((index > m_size) || !memory::in_non_empty_range(count, (m_size - index)))
    {
        return (index <= m_size) && (count == 0u);
    }
    if ((index + count) < m_size)
    {
        T* ptr = raw_data() + index;
        std::memmove(ptr, (ptr + count), ((m_size - count - index) * k_element_size));
    }
    m_size -= count;
    return true;
}

template<typename T>
inline bool TPodVector<T>::swap_insert(const std::size_t index, const T* const items, const std::size_t count) noexcept
{
    if ((items == nullptr) || is_internal_ptr(items) || (index > m_size) || !memory::in_non_empty_range(count, (k_max_elements - m_size)))
    {
        return (index <= m_size) && (count == 0u);
    }
    if (ensure_free(count))
    {
        T* ptr = raw_data() + index;
        if (m_size != index)
        {
            std::memmove((ptr + m_size - index), ptr, (std::min(count, (m_size - index)) * k_element_size));
        }
        std::memcpy(ptr, items, (count * k_element_size));
        m_size += count;
        return true;
    }
    return false;
}

template<typename T>
inline bool TPodVector<T>::swap_erase(const std::size_t index, const std::size_t count) noexcept
{
    if ((index > m_size) || !memory::in_non_empty_range(count, (m_size - index)))
    {
        return (index <= m_size) && (count == 0u);
    }
    if ((index + count) < m_size)
    {
        T* ptr = raw_data();
        const std::size_t tail_size = std::min(count, (m_size - count - index));
        std::memmove((ptr + index), (ptr + m_size - tail_size), (tail_size * k_element_size));
    }
    m_size -= count;
    return true;
}

template<typename T>
inline bool TPodVector<T>::allocate(const std::size_t capacity) noexcept
{
    if ((capacity <= k_max_elements) && ((capacity == m_capacity) || m_token.allocate(capacity, false)))
    {
        m_size = 0u;
        m_capacity = capacity;
        return true;
    }
    return false;
}

template<typename T>
inline bool TPodVector<T>::reallocate(const std::size_t size, const std::size_t capacity) noexcept
{
    if ((size <= capacity) && std::max(size, capacity) <= k_max_elements)
    {
        //  Pass survivable logical range, not old capacity:
        //  only the logical prefix [0, m_size) is preserved, truncated to the new size if shrinking.
        if ((capacity == m_capacity) || m_token.reallocate(capacity, std::min(m_size, size), false))
        {
            if (size > m_size)
            {
                T* ptr = raw_data() + m_size;
                std::memset(ptr, 0, ((size - m_size) * k_element_size));
            }
            m_size = size;
            m_capacity = capacity;
            return true;
        }
    }
    return false;
}

template<typename T>
inline bool TPodVector<T>::resize(const std::size_t size) noexcept
{
    return (size <= k_max_elements) ?
        reallocate(size, ((size > m_capacity) ? memory::vector_growth_policy(size, k_max_elements) : m_capacity)) :
        false;
}

template<typename T>
inline bool TPodVector<T>::reserve(const std::size_t minimum_capacity) noexcept
{
    if (minimum_capacity > k_max_elements)
    {
        return false;
    }
    return (minimum_capacity <= m_capacity) ? true :
        reallocate(m_size, memory::vector_growth_policy(minimum_capacity, k_max_elements));
}

template<typename T>
inline bool TPodVector<T>::ensure_free(const std::size_t extra) noexcept
{
    return (extra <= (k_max_elements - m_size)) ? reserve(m_size + extra) : false;
}

template<typename T>
inline bool TPodVector<T>::shrink_to_fit() noexcept
{
    if (m_size == 0u)
    {
        deallocate();
        return true;
    }
    return reallocate(m_size, m_size);
}

template<typename T>
inline bool TPodVector<T>::is_internal_ptr(const T* const ptr) const noexcept
{
    return pod_storage_contains_ptr(raw_data(), m_capacity, ptr);
}

template<typename T>
inline void TPodVector<T>::move_from(TPodVector& src) noexcept
{
    m_token = std::move(src.m_token);
    m_size = src.m_size;
    m_capacity = src.m_capacity;
    src.m_size = 0u;
    src.m_capacity = 0u;
}

//==============================================================================
//  TPodView<T> out of class function bodies
//==============================================================================

template<typename T>
inline TPodView<T>& TPodView<T>::set(const CByteView& view) noexcept
{
    const std::size_t view_size = view.size();
    if ((view_size != 0u) && (view_size <= k_max_bytes) && ((view_size % k_element_size) == 0u) &&
        m_view.set(view.data(), (view_size / k_element_size), k_element_size, view.align()) &&
        (m_view.element_alignment() >= k_align))
    {
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodView<T>& TPodView<T>::set(T* const data, const std::size_t size) noexcept
{
    if (m_view.set(data, size, k_element_size, k_align) && (m_view.element_alignment() >= k_align))
    {
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodView<T>& TPodView<T>::set(const memory::CMemoryView& view, const std::size_t size) noexcept
{
    if (view.is_valid() && (view.stride() == k_element_size) &&
        (view.element_alignment() >= k_align) && view.contains_range(0u, size))
    {
        m_view = view.subview(0u, size);
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodView<T> TPodView<T>::subview(const std::size_t offset, const std::size_t count) const noexcept
{
    return TPodView<T>{ m_view.subview(offset, count), count };
}

template<typename T>
inline TPodView<T> TPodView<T>::head_to(const std::size_t count) const noexcept
{
    return subview(0u, count);
}

template<typename T>
inline TPodView<T> TPodView<T>::tail_from(const std::size_t offset) const noexcept
{
    return is_ready() && (offset < size()) ? subview(offset, size() - offset) : TPodView<T>{};
}

//==============================================================================
//  TPodConstView<T> out of class function bodies
//==============================================================================

template<typename T>
inline TPodConstView<T>& TPodConstView<T>::set(const CByteView& view) noexcept
{
    const std::size_t view_size = view.size();
    if ((view_size != 0u) && (view_size <= k_max_bytes) && ((view_size % k_element_size) == 0u) &&
        m_view.set(view.data(), (view_size / k_element_size), k_element_size, view.align()) &&
        (m_view.element_alignment() >= k_align))
    {
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodConstView<T>& TPodConstView<T>::set(const CByteConstView& view) noexcept
{
    const std::size_t view_size = view.size();
    if ((view_size != 0u) && (view_size <= k_max_bytes) && ((view_size % k_element_size) == 0u) &&
        m_view.set(view.data(), (view_size / k_element_size), k_element_size, view.align()) &&
        (m_view.element_alignment() >= k_align))
    {
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodConstView<T>& TPodConstView<T>::set(const T* const data, const std::size_t size) noexcept
{
    if (m_view.set(data, size, k_element_size, k_align) && (m_view.element_alignment() >= k_align))
    {
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodConstView<T>& TPodConstView<T>::set(const memory::CMemoryView& view, const std::size_t size) noexcept
{
    if (view.is_valid() && (view.stride() == k_element_size) &&
        (view.element_alignment() >= k_align) && view.contains_range(0u, size))
    {
        m_view = view.subview(0u, size);
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodConstView<T>& TPodConstView<T>::set(const memory::CMemoryConstView& view, const std::size_t size) noexcept
{
    if (view.is_valid() && (view.stride() == k_element_size) &&
        (view.element_alignment() >= k_align) && view.contains_range(0u, size))
    {
        m_view = view.subview(0u, size);
        return *this;
    }
    return reset();
}

template<typename T>
inline TPodConstView<T> TPodConstView<T>::subview(const std::size_t offset, const std::size_t count) const noexcept
{
    return TPodConstView<T>{ m_view.subview(offset, count), count };
}

template<typename T>
inline TPodConstView<T> TPodConstView<T>::head_to(const std::size_t count) const noexcept
{
    return subview(0u, count);
}

template<typename T>
inline TPodConstView<T> TPodConstView<T>::tail_from(const std::size_t offset) const noexcept
{
    return is_ready() && (offset < size()) ? subview(offset, (size() - offset)) : TPodConstView<T>{};
}

#endif  //  TPOD_VECTOR_HPP_INCLUDED

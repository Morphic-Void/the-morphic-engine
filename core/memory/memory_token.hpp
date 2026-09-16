
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   memory_token.hpp
//  Author: Ritchie Brannan
//  Date:   11 Jul 26
//
//  Unified ownership token for contiguous relocatable storage and segmented
//  address-stable storage.

#pragma once

#ifndef MEMORY_TOKEN_HPP_INCLUDED
#define MEMORY_TOKEN_HPP_INCLUDED

#include <algorithm>    //  std::min
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint8_t, std::uint16_t, std::uint32_t
#include <cstring>      //  std::memcpy, std::memset
#include <limits>       //  std::numeric_limits

#include "memory_context.hpp"
#include "memory_policies.hpp"
#include "memory_view.hpp"
#include "bit_utils/bit_ops.hpp"
#include "bit_utils/TBitField.hpp"

namespace memory
{

class CMemoryToken
{
public:
    enum class EMode : std::uint8_t
    {
        relocatable,
        stable
    };

    static constexpr std::size_t k_default_buffer_capacity = 32u;
    static constexpr std::size_t k_min_directory_capacity = 8u;

    CMemoryToken() noexcept = default;

    CMemoryToken(const std::size_t stride, const std::size_t storage_alignment, CMemoryContext* const context = nullptr) noexcept;
    CMemoryToken(const std::size_t stride, const std::size_t storage_alignment, const std::size_t buffer_capacity_hint, CMemoryContext* const context = nullptr) noexcept;

    CMemoryToken(const CMemoryToken&) = delete;
    CMemoryToken& operator=(const CMemoryToken&) = delete;

    CMemoryToken(CMemoryToken&& other) noexcept;
    CMemoryToken& operator=(CMemoryToken&& other) noexcept;

    ~CMemoryToken() noexcept;

    // Configuration preserves an existing context and otherwise selects the ambient context.
    [[nodiscard]] bool configure_relocatable(const std::size_t stride, const std::size_t storage_alignment = 0u) noexcept;
    [[nodiscard]] bool configure_stable(const std::size_t stride, const std::size_t storage_alignment = 0u, const std::size_t buffer_capacity_hint = 0u) noexcept;

    [[nodiscard]] bool clone(const CMemoryToken& source) noexcept;
    [[nodiscard]] bool clone(const CMemoryToken& source, CMemoryContext* context) noexcept;

    [[nodiscard]] bool is_configured() const noexcept;
    [[nodiscard]] bool is_empty() const noexcept { return m_count == 0u; }
    [[nodiscard]] bool owns_storage() const noexcept { return m_memory != nullptr; }
    [[nodiscard]] bool is_relocatable() const noexcept;
    [[nodiscard]] bool is_stable() const noexcept;
    [[nodiscard]] explicit operator bool() const noexcept { return is_configured(); }

    [[nodiscard]] CMemoryContext* context() const noexcept { return m_context; }

    [[nodiscard]] std::size_t count() const noexcept { return m_count; }
    [[nodiscard]] std::size_t stride() const noexcept { return m_stride; }
    [[nodiscard]] std::size_t storage_alignment() const noexcept;
    [[nodiscard]] std::size_t element_alignment() const noexcept;
    [[nodiscard]] std::size_t bytes() const noexcept { return count() * stride(); }
    [[nodiscard]] std::size_t max_count() const noexcept;
    [[nodiscard]] std::size_t per_buffer_capacity() const noexcept;

    [[nodiscard]] bool contains_index(const std::size_t index) const noexcept { return index < count(); }
    [[nodiscard]] bool can_grow_to(const std::size_t count) const noexcept;

    [[nodiscard]] void* data() noexcept;
    [[nodiscard]] const void* data() const noexcept;
    [[nodiscard]] CMemoryView view() noexcept;
    [[nodiscard]] CMemoryConstView view() const noexcept;
    [[nodiscard]] CMemoryConstView const_view() const noexcept { return view(); }
    [[nodiscard]] void* map_index(const std::size_t index, const bool zero_new = true) noexcept;
    [[nodiscard]] void* index_ptr(const std::size_t index) noexcept;
    [[nodiscard]] const void* index_ptr(const std::size_t index) const noexcept;

    // allocate() replaces all storage in either mode. reallocate() preserves a
    // relocatable prefix; grow_to() preserves all stable addresses and content.
    [[nodiscard]] bool allocate(const std::size_t count, const bool zero = true) noexcept;
    [[nodiscard]] bool reallocate(const std::size_t count, const std::size_t copy_count, const bool zero_new = true) noexcept;
    [[nodiscard]] bool grow_to(const std::size_t count, const bool zero_new = true) noexcept;
    void deallocate() noexcept;

    // Direct storage attribution.
    [[nodiscard]] std::uint32_t memory_token_count() const noexcept { return 1u; }
    [[nodiscard]] std::uint32_t memory_allocation_count() const noexcept;
    [[nodiscard]] std::uint64_t memory_allocation_size() const noexcept;
    [[nodiscard]] bool can_reattribute_to(CMemoryContext* context = nullptr) const noexcept;
    [[nodiscard]] bool reattribute(CMemoryContext* context = nullptr) noexcept;
    [[nodiscard]] bool set_context(CMemoryContext* const context = nullptr) noexcept { return reattribute(context); }
    void unsafe_replace_context_without_accounting(CMemoryContext* const expected_source, CMemoryContext* const target) noexcept;

private:
    using storage_alignment_field = TBitField16<0x001fu>;       // bits  0..4
    using buffer_capacity_field = TBitField16<0x03e0u>;         // bits  5..9

    static constexpr std::uint16_t k_stable_mask = 0x0400u;     // bit  10
    static constexpr std::uint16_t k_known_mask = static_cast<std::uint16_t>(
        storage_alignment_field::k_mask | buffer_capacity_field::k_mask | k_stable_mask);

    [[nodiscard]] bool configure(const EMode mode, const std::size_t stride, const std::size_t storage_alignment,
        const std::size_t buffer_capacity_hint, CMemoryContext* context) noexcept;
    [[nodiscard]] static bool make_control(
        const EMode mode,
        const std::size_t stride,
        const std::size_t storage_alignment,
        const std::size_t buffer_capacity_hint,
        std::uint16_t& control) noexcept;

    [[nodiscard]] std::size_t count_limit() const noexcept { return memory::max_elements(m_stride); }
    [[nodiscard]] std::size_t get_storage_alignment_log2() const noexcept { return storage_alignment_field::decode(m_control); }
    [[nodiscard]] std::size_t get_buffer_capacity_log2() const noexcept { return buffer_capacity_field::decode(m_control); }
    [[nodiscard]] std::size_t get_buffer_capacity_mask() const noexcept { return (std::size_t{ 1u } << get_buffer_capacity_log2()) - 1u; }
    [[nodiscard]] bool get_stable() const noexcept { return (m_control & k_stable_mask) != 0u; }
    void set_storage_alignment_log2(const std::size_t value) noexcept;
    void set_buffer_capacity_log2(const std::size_t value) noexcept;
    void set_stable(const bool stable) noexcept;

    [[nodiscard]] static std::size_t directory_capacity(const std::size_t buffer_count) noexcept;
    [[nodiscard]] std::size_t buffer_count_for(const std::size_t count) const noexcept;
    [[nodiscard]] std::size_t directory_bytes_for(const std::size_t buffer_count) const noexcept;
    [[nodiscard]] bool allocate_stable_buffers(void** const buffers, const std::size_t first_buffer, const std::size_t buffer_count, const bool zero) noexcept;
    [[nodiscard]] bool create_stable_storage(const std::size_t count, const bool zero, void*& new_memory) noexcept;
    [[nodiscard]] bool clone_to(const CMemoryToken& source, CMemoryContext* const context) noexcept;
    [[nodiscard]] bool grow(const std::size_t count, const bool zero_new) noexcept;
    void release_storage(void* const memory, const std::uint32_t count) noexcept;

    void*           m_memory{ nullptr };
    CMemoryContext* m_context{ nullptr };
    std::uint32_t   m_count{ 0u };
    std::uint16_t   m_stride{ 0u };
    std::uint16_t   m_control{ 0u };
};

static_assert(sizeof(void*) != 8u || sizeof(CMemoryToken) == 24u,
    "CMemoryToken must occupy 24 bytes on a 64-bit target");
static_assert(sizeof(void*) != 4u || sizeof(CMemoryToken) == 16u,
    "CMemoryToken must occupy 16 bytes on a 32-bit target");
static_assert(memory::k_byte_size_ceiling <= 0xffffffffu,
    "CMemoryToken count storage cannot represent the shared memory limit");

//==============================================================================
//  CMemoryToken out of class function bodies
//==============================================================================

inline CMemoryToken::~CMemoryToken() noexcept
{
    deallocate();
    m_context = nullptr;
    m_stride = 0u;
    m_control = 0u;
}

inline CMemoryToken::CMemoryToken(
    const std::size_t stride,
    const std::size_t storage_alignment,
    CMemoryContext* const context) noexcept
{
    (void)configure(EMode::relocatable, stride, storage_alignment, 0u, context);
}

inline CMemoryToken::CMemoryToken(
    const std::size_t stride,
    const std::size_t storage_alignment,
    const std::size_t buffer_capacity_hint,
    CMemoryContext* const context) noexcept
{
    (void)configure(EMode::stable, stride, storage_alignment, buffer_capacity_hint, context);
}

inline CMemoryToken::CMemoryToken(CMemoryToken&& other) noexcept
    : m_memory(other.m_memory)
    , m_context(other.m_context)
    , m_count(other.m_count)
    , m_stride(other.m_stride)
    , m_control(other.m_control)
{
    other.m_memory = nullptr;
    other.m_count = 0u;
}

inline CMemoryToken& CMemoryToken::operator=(CMemoryToken&& other) noexcept
{
    if (this != &other)
    {
        deallocate();
        m_memory = other.m_memory;
        m_context = other.m_context;
        m_count = other.m_count;
        m_stride = other.m_stride;
        m_control = other.m_control;
        other.m_memory = nullptr;
        other.m_count = 0u;
    }
    return *this;
}

inline void CMemoryToken::set_storage_alignment_log2(const std::size_t value) noexcept
{
    m_control = storage_alignment_field::replace(m_control, value);
}

inline void CMemoryToken::set_buffer_capacity_log2(const std::size_t value) noexcept
{
    m_control = buffer_capacity_field::replace(m_control, value);
}

inline void CMemoryToken::set_stable(const bool stable) noexcept
{
    if (stable)
    {
        m_control |= k_stable_mask;
    }
    else
    {
        m_control &= static_cast<std::uint16_t>(~k_stable_mask);
    }
}

inline bool CMemoryToken::make_control(
    const EMode mode,
    const std::size_t stride,
    const std::size_t storage_alignment,
    const std::size_t buffer_capacity_hint,
    std::uint16_t& control) noexcept
{
    if ((stride == 0u) || (stride > 0xffffu))
    {
        return false;
    }

    const std::size_t alignment_source = (storage_alignment != 0u) ? storage_alignment : stride;
    const std::size_t normalized_storage_alignment = bit_ops::lo_bit_mask(alignment_source);
    if (normalized_storage_alignment == 0u)
    {
        return false;
    }

    const std::size_t storage_alignment_log2 = static_cast<std::size_t>(
        bit_ops::lo_bit_index(normalized_storage_alignment));
    if (!storage_alignment_field::can_encode(storage_alignment_log2))
    {
        return false;
    }

    std::size_t buffer_capacity_log2 = 0u;
    if (mode == EMode::stable)
    {
        const std::size_t requested_capacity =
            (buffer_capacity_hint == 0u) ? k_default_buffer_capacity : buffer_capacity_hint;
        const std::size_t normalized_capacity = bit_ops::round_up_to_pow2(requested_capacity);
        if ((normalized_capacity == 0u) || (normalized_capacity > memory::max_elements(stride)))
        {
            return false;
        }

        buffer_capacity_log2 = static_cast<std::size_t>(
            bit_ops::lo_bit_index(normalized_capacity));
        if (!buffer_capacity_field::can_encode(buffer_capacity_log2))
        {
            return false;
        }
    }

    control = storage_alignment_field::encode(storage_alignment_log2) |
        buffer_capacity_field::encode(buffer_capacity_log2) |
        ((mode == EMode::stable) ? k_stable_mask : std::uint16_t{ 0u });
    return true;
}

inline bool CMemoryToken::configure(
    const EMode mode,
    const std::size_t stride,
    const std::size_t storage_alignment,
    const std::size_t buffer_capacity_hint,
    CMemoryContext* context) noexcept
{
    if ((m_memory != nullptr) || (m_count != 0u) || (m_stride != 0u) || (m_control != 0u))
    {
        return false;
    }

    std::uint16_t control = 0u;
    context = (context != nullptr) ? context : get_ambient_memory_context();
    if ((context == nullptr) || !make_control(mode, stride, storage_alignment, buffer_capacity_hint, control))
    {
        return false;
    }

    m_context = context;
    m_stride = static_cast<std::uint16_t>(stride);
    m_control = control;
    return true;
}

inline bool CMemoryToken::configure_relocatable(const std::size_t stride, const std::size_t storage_alignment) noexcept
{
    return configure(EMode::relocatable, stride, storage_alignment, 0u, m_context);
}

inline bool CMemoryToken::configure_stable(
    const std::size_t stride,
    const std::size_t storage_alignment,
    const std::size_t buffer_capacity_hint) noexcept
{
    return configure(EMode::stable, stride, storage_alignment, buffer_capacity_hint, m_context);
}

inline bool CMemoryToken::clone_to(const CMemoryToken& source, CMemoryContext* const context) noexcept
{
    CMemoryToken new_token;
    if (source.is_configured())
    {
        if (context == nullptr)
        {
            return false;
        }

        new_token.m_context = context;
        new_token.m_stride = source.m_stride;
        new_token.m_control = source.m_control;
        if (source.m_count != 0u)
        {
            if (!new_token.allocate(source.m_count, false))
            {
                return false;
            }

            if (source.is_relocatable())
            {
                std::memcpy(new_token.m_memory, source.m_memory, source.bytes());
            }
            else if (source.is_stable())
            {
                const std::size_t capacity = source.per_buffer_capacity();
                std::size_t index = 0u;
                while (index < source.m_count)
                {
                    const std::size_t copy_count = std::min((source.m_count - index), capacity);
                    std::memcpy(new_token.index_ptr(index), source.index_ptr(index), (copy_count * source.stride()));
                    index += copy_count;
                }
            }
        }
    }

    deallocate();
    m_memory = new_token.m_memory;
    m_context = new_token.m_context;
    m_count = new_token.m_count;
    m_stride = new_token.m_stride;
    m_control = new_token.m_control;
    new_token.m_memory = nullptr;
    new_token.m_context = nullptr;
    new_token.m_count = 0u;
    new_token.m_stride = 0u;
    new_token.m_control = 0u;
    return true;
}

inline bool CMemoryToken::clone(const CMemoryToken& source) noexcept
{
    if (this == &source)
    {
        return true;
    }
    return clone_to(source, source.m_context);
}

inline bool CMemoryToken::clone(const CMemoryToken& source, CMemoryContext* context) noexcept
{
    context = (context != nullptr) ? context : get_ambient_memory_context();
    if ((this == &source) && (context == source.m_context))
    {
        return true;
    }
    return clone_to(source, context);
}

inline bool CMemoryToken::is_configured() const noexcept
{
    return (m_context != nullptr) && (m_stride != 0u) &&
        ((m_control & static_cast<std::uint16_t>(~k_known_mask)) == 0u);
}

inline bool CMemoryToken::is_relocatable() const noexcept
{
    return is_configured() && !get_stable();
}

inline bool CMemoryToken::is_stable() const noexcept
{
    return is_configured() && get_stable();
}

inline std::size_t CMemoryToken::storage_alignment() const noexcept
{
    return is_configured() ? (std::size_t{ 1u } << get_storage_alignment_log2()) : std::size_t{ 0u };
}

inline std::size_t CMemoryToken::element_alignment() const noexcept
{
    if (!is_configured())
    {
        return 0u;
    }
    const std::size_t natural_alignment = bit_ops::lo_bit_mask(stride());
    return std::min(storage_alignment(), natural_alignment);
}

inline std::size_t CMemoryToken::max_count() const noexcept
{
    return is_configured() ? count_limit() : std::size_t{ 0u };
}

inline std::size_t CMemoryToken::per_buffer_capacity() const noexcept
{
    return is_stable() ? (std::size_t{ 1u } << get_buffer_capacity_log2()) : std::size_t{ 0u };
}

inline std::size_t CMemoryToken::buffer_count_for(const std::size_t element_count) const noexcept
{
    return (element_count == 0u) ? 0u : (1u + ((element_count - 1u) >> get_buffer_capacity_log2()));
}

inline std::size_t CMemoryToken::directory_capacity(const std::size_t buffer_count) noexcept
{
    if (buffer_count <= 1u)
    {
        return 0u;
    }
    return std::max(k_min_directory_capacity, bit_ops::round_up_to_pow2(buffer_count));
}

inline std::size_t CMemoryToken::directory_bytes_for(const std::size_t buffer_count) const noexcept
{
    return directory_capacity(buffer_count) * sizeof(void*);
}

inline bool CMemoryToken::can_grow_to(const std::size_t new_count) const noexcept
{
    if (!is_stable() || (new_count > max_count()))
    {
        return false;
    }
    const std::size_t required_buffers = buffer_count_for(new_count);
    return (required_buffers <= 1u) ||
        (directory_bytes_for(required_buffers) <= memory::k_byte_size_ceiling);
}

inline void* CMemoryToken::data() noexcept
{
    return (is_relocatable() && (m_count != 0u)) ? m_memory : nullptr;
}

inline const void* CMemoryToken::data() const noexcept
{
    return (is_relocatable() && (m_count != 0u)) ? m_memory : nullptr;
}

inline CMemoryView CMemoryToken::view() noexcept
{
    return CMemoryView{ data(), count(), stride(), storage_alignment() };
}

inline CMemoryConstView CMemoryToken::view() const noexcept
{
    return CMemoryConstView{ data(), count(), stride(), storage_alignment() };
}

inline const void* CMemoryToken::index_ptr(const std::size_t index) const noexcept
{
    if ((m_memory == nullptr) || !contains_index(index))
    {
        return nullptr;
    }
    if (is_relocatable())
    {
        return static_cast<const std::uint8_t*>(m_memory) + (index * stride());
    }

    const std::size_t buffer_shift = get_buffer_capacity_log2();
    const std::size_t buffer_mask = get_buffer_capacity_mask();
    const std::size_t buffer_index = index >> buffer_shift;
    const std::size_t buffer_offset = index & buffer_mask;
    const std::size_t buffer_count = buffer_count_for(m_count);
    void* const buffer = (buffer_count == 1u) ? m_memory : static_cast<void* const*>(m_memory)[buffer_index];
    return static_cast<const std::uint8_t*>(buffer) + (buffer_offset * stride());
}

inline void* CMemoryToken::index_ptr(const std::size_t index) noexcept
{
    return const_cast<void*>(static_cast<const CMemoryToken&>(*this).index_ptr(index));
}

inline void* CMemoryToken::map_index(const std::size_t index, const bool zero_new) noexcept
{
    if (!is_stable() || (index >= max_count()))
    {
        return nullptr;
    }
    const std::size_t new_count = index + 1u;
    if (!can_grow_to(new_count) || !grow(new_count, zero_new))
    {
        return nullptr;
    }
    return index_ptr(index);
}

inline bool CMemoryToken::allocate_stable_buffers(
    void** const buffers,
    const std::size_t first_buffer,
    const std::size_t buffer_count,
    const bool zero) noexcept
{
    const std::size_t buffer_bytes = per_buffer_capacity() * stride();
    std::size_t index = first_buffer;
    while (index < buffer_count)
    {
        buffers[index] = m_context->allocate(storage_alignment(), buffer_bytes);
        if (buffers[index] == nullptr)
        {
            while (index > first_buffer)
            {
                --index;
                m_context->deallocate(storage_alignment(), buffer_bytes, buffers[index]);
            }
            return false;
        }
        if (zero)
        {
            std::memset(buffers[index], 0, buffer_bytes);
        }
        ++index;
    }
    return true;
}

inline bool CMemoryToken::create_stable_storage(
    const std::size_t new_count,
    const bool zero,
    void*& new_memory) noexcept
{
    const std::size_t new_buffer_count = buffer_count_for(new_count);
    const std::size_t buffer_bytes = per_buffer_capacity() * stride();
    if (new_buffer_count == 1u)
    {
        new_memory = m_context->allocate(storage_alignment(), buffer_bytes);
        if ((new_memory != nullptr) && zero)
        {
            std::memset(new_memory, 0, buffer_bytes);
        }
        return new_memory != nullptr;
    }

    const std::size_t new_directory_bytes = directory_bytes_for(new_buffer_count);
    void** const new_directory = static_cast<void**>(m_context->allocate(alignof(void*), new_directory_bytes));
    if (new_directory == nullptr)
    {
        return false;
    }
    std::memset(new_directory, 0, new_directory_bytes);
    if (!allocate_stable_buffers(new_directory, 0u, new_buffer_count, zero))
    {
        m_context->deallocate(alignof(void*), new_directory_bytes, new_directory);
        return false;
    }
    new_memory = new_directory;
    return true;
}

inline bool CMemoryToken::allocate(const std::size_t new_count, const bool zero) noexcept
{
    if (!is_configured() || (new_count > max_count()) ||
        (is_stable() && !can_grow_to(new_count)))
    {
        return false;
    }
    if (new_count == 0u)
    {
        deallocate();
        return true;
    }

    void* new_memory = nullptr;
    if (is_relocatable())
    {
        new_memory = m_context->allocate(storage_alignment(), (new_count * stride()));
        if (new_memory == nullptr)
        {
            return false;
        }
        if (zero)
        {
            std::memset(new_memory, 0, (new_count * stride()));
        }
    }
    else if (!create_stable_storage(new_count, zero, new_memory))
    {
        return false;
    }

    release_storage(m_memory, m_count);
    m_memory = new_memory;
    m_count = static_cast<std::uint32_t>(new_count);
    return true;
}

inline bool CMemoryToken::reallocate(
    const std::size_t new_count,
    const std::size_t copy_count,
    const bool zero_new) noexcept
{
    if (!is_relocatable() || (new_count > max_count()) ||
        (copy_count > std::min(count(), new_count)))
    {
        return false;
    }
    if (new_count == 0u)
    {
        deallocate();
        return true;
    }
    if (new_count == count())
    {
        if (zero_new && (new_count > copy_count))
        {
            std::memset(static_cast<std::uint8_t*>(m_memory) + (copy_count * stride()), 0, ((new_count - copy_count) * stride()));
        }
        return true;
    }

    void* const new_memory = m_context->allocate(storage_alignment(), (new_count * stride()));
    if (new_memory == nullptr)
    {
        return false;
    }
    if (copy_count != 0u)
    {
        std::memcpy(new_memory, m_memory, (copy_count * stride()));
    }
    if (zero_new && (new_count > copy_count))
    {
        std::memset(static_cast<std::uint8_t*>(new_memory) + (copy_count * stride()), 0, ((new_count - copy_count) * stride()));
    }

    release_storage(m_memory, m_count);
    m_memory = new_memory;
    m_count = static_cast<std::uint32_t>(new_count);
    return true;
}

inline bool CMemoryToken::grow(const std::size_t new_count, const bool zero_new) noexcept
{
    if (new_count <= count())
    {
        return true;
    }
    if (m_memory == nullptr)
    {
        void* new_memory = nullptr;
        if (!create_stable_storage(new_count, zero_new, new_memory))
        {
            return false;
        }
        m_memory = new_memory;
        m_count = static_cast<std::uint32_t>(new_count);
        return true;
    }

    const std::size_t old_count = count();
    const std::size_t old_buffer_count = buffer_count_for(old_count);
    const std::size_t new_buffer_count = buffer_count_for(new_count);
    const std::size_t capacity = per_buffer_capacity();
    const std::size_t buffer_shift = get_buffer_capacity_log2();
    const std::size_t buffer_mask = get_buffer_capacity_mask();
    if (new_buffer_count == old_buffer_count)
    {
        if (zero_new)
        {
            std::size_t index = old_count;
            while (index < new_count)
            {
                const std::size_t buffer_offset = index & buffer_mask;
                const std::size_t chunk = std::min(new_count - index, capacity - buffer_offset);
                // index_ptr() uses m_count, so directly address newly exposed storage.
                void* const buffer = (old_buffer_count == 1u) ? m_memory : static_cast<void**>(m_memory)[index >> buffer_shift];
                std::memset(static_cast<std::uint8_t*>(buffer) + (buffer_offset * stride()), 0, (chunk * stride()));
                index += chunk;
            }
        }
        m_count = static_cast<std::uint32_t>(new_count);
        return true;
    }

    const std::size_t new_directory_bytes = directory_bytes_for(new_buffer_count);
    void** const new_directory = static_cast<void**>(m_context->allocate(alignof(void*), new_directory_bytes));
    if (new_directory == nullptr)
    {
        return false;
    }
    std::memset(new_directory, 0, new_directory_bytes);

    if (old_buffer_count == 1u)
    {
        new_directory[0] = m_memory;
    }
    else
    {
        std::memcpy(new_directory, m_memory, (old_buffer_count * sizeof(void*)));
    }

    if (!allocate_stable_buffers(new_directory, old_buffer_count, new_buffer_count, zero_new))
    {
        m_context->deallocate(alignof(void*), new_directory_bytes, new_directory);
        return false;
    }

    const std::size_t old_buffer_offset = old_count & buffer_mask;
    if (zero_new && (old_buffer_offset != 0u))
    {
        const std::size_t first_zero_count = std::min((new_count - old_count), (capacity - old_buffer_offset));
        std::memset(static_cast<std::uint8_t*>(new_directory[old_count >> buffer_shift]) + (old_buffer_offset * stride()), 0, (first_zero_count * stride()));
    }

    if (old_buffer_count > 1u)
    {
        m_context->deallocate(alignof(void*), directory_bytes_for(old_buffer_count), m_memory);
    }
    m_memory = new_directory;
    m_count = static_cast<std::uint32_t>(new_count);
    return true;
}

inline bool CMemoryToken::grow_to(const std::size_t new_count, const bool zero_new) noexcept
{
    return can_grow_to(new_count) && grow(new_count, zero_new);
}

inline void CMemoryToken::release_storage(void* const memory, const std::uint32_t element_count) noexcept
{
    if (memory == nullptr)
    {
        return;
    }
    if (is_relocatable())
    {
        m_context->deallocate(storage_alignment(), (static_cast<std::size_t>(element_count) * stride()), memory);
        return;
    }

    const std::size_t used_buffer_count = buffer_count_for(element_count);
    const std::size_t buffer_bytes = per_buffer_capacity() * stride();
    if (used_buffer_count == 1u)
    {
        m_context->deallocate(storage_alignment(), buffer_bytes, memory);
        return;
    }

    void** const buffers = static_cast<void**>(memory);
    std::size_t index = used_buffer_count;
    while (index != 0u)
    {
        --index;
        m_context->deallocate(storage_alignment(), buffer_bytes, buffers[index]);
    }
    m_context->deallocate(alignof(void*), directory_bytes_for(used_buffer_count), memory);
}

inline void CMemoryToken::deallocate() noexcept
{
    release_storage(m_memory, m_count);
    m_memory = nullptr;
    m_count = 0u;
}

inline std::uint32_t CMemoryToken::memory_allocation_count() const noexcept
{
    if (!owns_storage())
    {
        return 0u;
    }
    if (is_relocatable())
    {
        return 1u;
    }
    const std::size_t buffer_count = buffer_count_for(m_count);
    const std::size_t allocation_count = buffer_count + ((buffer_count > 1u) ? 1u : 0u);
    MV_ASSERT(allocation_count <= std::numeric_limits<std::uint32_t>::max());
    return static_cast<std::uint32_t>(allocation_count);
}

inline std::uint64_t CMemoryToken::memory_allocation_size() const noexcept
{
    if (!owns_storage())
    {
        return 0u;
    }
    if (is_relocatable())
    {
        const std::size_t conditioned_alignment = m_context->condition_alignment(storage_alignment());
        return m_context->condition_bytes(conditioned_alignment, bytes());
    }
    const std::size_t buffer_count = buffer_count_for(m_count);
    const std::size_t conditioned_buffer_alignment = m_context->condition_alignment(storage_alignment());
    const std::size_t conditioned_buffer_bytes = m_context->condition_bytes(
        conditioned_buffer_alignment, (per_buffer_capacity() * stride()));
    std::uint64_t total_bytes = static_cast<std::uint64_t>(buffer_count) * conditioned_buffer_bytes;
    if (buffer_count > 1u)
    {
        const std::size_t conditioned_directory_alignment = m_context->condition_alignment(alignof(void*));
        total_bytes += m_context->condition_bytes(
            conditioned_directory_alignment, directory_bytes_for(buffer_count));
    }
    return total_bytes;
}

inline bool CMemoryToken::can_reattribute_to(CMemoryContext* target) const noexcept
{
    target = (target != nullptr) ? target : get_ambient_memory_context();
    if (target == nullptr)
    {
        return false;
    }
    if ((target == m_context) || !owns_storage())
    {
        return true;
    }
    return is_configured() && m_context->is_compatible_with(*target);
}

inline bool CMemoryToken::reattribute(CMemoryContext* target) noexcept
{
    target = (target != nullptr) ? target : get_ambient_memory_context();
    if (!can_reattribute_to(target))
    {
        return false;
    }
    if (!owns_storage() || (target == m_context))
    {
        m_context = target;
        return true;
    }

    if (!memory::reattribute(*m_context, *target, memory_allocation_count(), memory_allocation_size()))
    {
        return false;
    }
    m_context = target;
    return true;
}

inline void CMemoryToken::unsafe_replace_context_without_accounting(CMemoryContext* const expected_source, CMemoryContext* const target) noexcept
{
    const bool valid = (target != nullptr) && (!owns_storage() ||
        ((expected_source != nullptr) && (m_context == expected_source) &&
            expected_source->is_compatible_with(*target)));
    MV_ASSERT(valid);
    if (valid)
    {
        m_context = target;
    }
}

//  Adapt the primitive token to the common container observation contract.
[[nodiscard]] inline SMemoryAttribution observe_memory_attribution(const CMemoryToken& token) noexcept
{
    return SMemoryAttribution{
        token.owns_storage() ? EMemorySourceState::coherent : EMemorySourceState::empty,
        token.owns_storage() ? token.context() : nullptr,
        token.memory_token_count(), token.memory_allocation_count(), token.memory_allocation_size() };
}

}   //  namespace memory

#endif  //  #ifndef MEMORY_TOKEN_HPP_INCLUDED

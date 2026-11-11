
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TStaticLookup.hpp
//  Author: Ritchie Brannan
//  Date:   9 May 26
//
//  Requirements:
//  - Requires C++17 or later.
//  - No exceptions.
//
//  A minimal static lookup for machine-word key/value pairs.

#pragma once

#ifndef TSTATIC_LOOKUP_HPP_INCLUDED
#define TSTATIC_LOOKUP_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uintptr_t
#include <type_traits>  //  std::is_const_v, std::is_enum_v, std::is_integral_v, std::is_pointer_v

#include "containers/TPodVector.hpp"

//==============================================================================
//  CStaticLookup
//  Sorted machine-word lookup with duplicate-key rejection.
//==============================================================================

class CStaticLookup
{
public:

    //  Default and deleted lifetime
    CStaticLookup() noexcept = default;
    CStaticLookup(const CStaticLookup&) = delete;
    CStaticLookup& operator=(const CStaticLookup&) = delete;
    CStaticLookup(CStaticLookup&&) noexcept = default;
    CStaticLookup& operator=(CStaticLookup&&) noexcept = default;
    ~CStaticLookup() noexcept = default;

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_lookup.is_valid(); }
    [[nodiscard]] std::size_t query_count() const noexcept { return m_lookup.size(); }

    //  Operations
    [[nodiscard]] bool add(const std::uintptr_t key, const std::uintptr_t value) noexcept;
    [[nodiscard]] bool find(const std::uintptr_t key, std::uintptr_t& value) const noexcept;
    [[nodiscard]] bool contains(const std::uintptr_t key) const noexcept;

    //  Storage management
    void optimise() noexcept { (void)m_lookup.shrink_to_fit(); }
    void discard() noexcept { m_lookup.deallocate(); }

private:
    [[nodiscard]] bool find_index(const std::uintptr_t key, std::size_t& insert_at) const noexcept;

    struct SEntry
    {
        std::uintptr_t key = 0u;
        std::uintptr_t value = 0u;
    };

    TPodVector<SEntry> m_lookup;
};

//==============================================================================
//  TStaticLookup<TKey,TValue>
//  Typed facade over CStaticLookup.
//==============================================================================

template<typename TKey, typename TValue>
class TStaticLookup
{
private:
    static_assert(!std::is_const_v<TKey>, "TStaticLookup<TKey, TValue> requires non-const TKey.");
    static_assert((std::is_integral_v<TKey> || std::is_enum_v<TKey> || std::is_pointer_v<TKey>),
        "TStaticLookup<TKey, TValue> requires TKey to be integral, enum, or pointer.");
    static_assert((sizeof(TKey) <= sizeof(std::uintptr_t)),
        "TStaticLookup<TKey, TValue> requires TKey storable as std::uintptr_t.");

    static_assert(!std::is_const_v<TValue>, "TStaticLookup<TKey, TValue> requires non-const TValue.");
    static_assert((std::is_integral_v<TValue> || std::is_enum_v<TValue> || std::is_pointer_v<TValue>),
        "TStaticLookup<TKey, TValue> requires TValue to be integral, enum, or pointer.");
    static_assert((sizeof(TValue) <= sizeof(std::uintptr_t)),
        "TStaticLookup<TKey, TValue> requires TValue storable as std::uintptr_t.");

public:

    //  Default and deleted lifetime
    TStaticLookup() noexcept = default;
    TStaticLookup(const TStaticLookup&) = delete;
    TStaticLookup& operator=(const TStaticLookup&) = delete;
    TStaticLookup(TStaticLookup&&) noexcept = default;
    TStaticLookup& operator=(TStaticLookup&&) noexcept = default;
    ~TStaticLookup() noexcept = default;

    //  Status
    [[nodiscard]] bool is_valid() const noexcept { return m_lookup.is_valid(); }
    [[nodiscard]] std::size_t query_count() const noexcept { return m_lookup.query_count(); }

    //  Operations
    [[nodiscard]] bool add(const TKey key, const TValue value) noexcept;
    [[nodiscard]] bool find(const TKey key, TValue& value) const noexcept;
    [[nodiscard]] bool contains(const TKey key) const noexcept;

    //  Storage management
    void optimise() noexcept { m_lookup.optimise(); }
    void discard() noexcept { m_lookup.discard(); }

private:
    template<typename T>
    [[nodiscard]] static std::uintptr_t encode(const T value) noexcept;

    template<typename T>
    [[nodiscard]] static T decode(const std::uintptr_t value) noexcept;

    CStaticLookup m_lookup;
};

//==============================================================================
//  CStaticLookup out of class function bodies
//==============================================================================

inline bool CStaticLookup::add(const std::uintptr_t key, const std::uintptr_t value) noexcept
{
    std::size_t index{ 0u };

    if (!find_index(key, index))
    {   //  key is unique
        return m_lookup.insert(index, SEntry{ key, value });
    }

    return false;
}

inline bool CStaticLookup::find(const std::uintptr_t key, std::uintptr_t& value) const noexcept
{
    std::size_t index{ 0u };

    if (find_index(key, index))
    {
        value = m_lookup[index].value;
        return true;
    }

    return false;
}

inline bool CStaticLookup::contains(const std::uintptr_t key) const noexcept
{
    std::size_t index{ 0u };
    return find_index(key, index);
}

inline bool CStaticLookup::find_index(const std::uintptr_t key, std::size_t& index) const noexcept
{
    std::size_t lower = 0u;
    std::size_t upper = m_lookup.size();

    const SEntry* const data = m_lookup.data();

    while (lower < upper)
    {
        const std::size_t pivot = lower + ((upper - lower) >> 1u);
        const std::uintptr_t pivot_key = data[pivot].key;

        if (key == pivot_key)
        {
            index = pivot;
            return true;
        }

        if (key < pivot_key)
        {
            upper = pivot;
        }
        else
        {
            lower = pivot + 1u;
        }
    }

    index = lower;
    return false;
}

//==============================================================================
//  TStaticLookup<TKey,TValue> out of class function bodies
//==============================================================================

template<typename TKey, typename TValue>
template<typename T>
inline std::uintptr_t TStaticLookup<TKey, TValue>::encode(const T value) noexcept
{
    if constexpr (std::is_pointer_v<T>)
    {
        return reinterpret_cast<std::uintptr_t>(value);
    }
    else
    {
        return static_cast<std::uintptr_t>(value);
    }
}

template<typename TKey, typename TValue>
template<typename T>
inline T TStaticLookup<TKey, TValue>::decode(const std::uintptr_t value) noexcept
{
    if constexpr (std::is_pointer_v<T>)
    {
        return reinterpret_cast<T>(value);
    }
    else
    {
        return static_cast<T>(value);
    }
}

template<typename TKey, typename TValue>
inline bool TStaticLookup<TKey, TValue>::add(const TKey key, const TValue value) noexcept
{
    return m_lookup.add(encode(key), encode(value));
}

template<typename TKey, typename TValue>
inline bool TStaticLookup<TKey, TValue>::find(const TKey key, TValue& value) const noexcept
{
    std::uintptr_t found{ 0u };

    if (m_lookup.find(encode(key), found))
    {
        value = decode<TValue>(found);
        return true;
    }

    return false;
}

template<typename TKey, typename TValue>
inline bool TStaticLookup<TKey, TValue>::contains(const TKey key) const noexcept
{
    return m_lookup.contains(encode(key));
}

#endif  //  TSTATIC_LOOKUP_HPP_INCLUDED

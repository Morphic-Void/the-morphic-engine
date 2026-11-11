
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   TBitField.hpp
//  Author: Ritchie Brannan
//  Date:   11 Jul 26
//
//  Bitfield encode/decode helper template.

#pragma once

#ifndef TBITFIELD_HPP_INCLUDED
#define TBITFIELD_HPP_INCLUDED

#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint16_t, std::uint32_t
#include <limits>       //  std::numeric_limits
#include <type_traits>  //  std::is_integral_v, std::is_unsigned_v

#include "bit_ops.hpp"

template<typename T, std::size_t t_mask>
struct TBitField
{
    static_assert(std::is_integral_v<T> && std::is_unsigned_v<T>, "TBitField requires an unsigned integral storage type");
    static_assert((t_mask != 0u), "TBitField requires a nonzero mask");
    static_assert((t_mask <= static_cast<std::size_t>(std::numeric_limits<T>::max())), "TBitField mask exceeds the storage type");

    using storage_type = T;

    static constexpr storage_type k_mask = static_cast<storage_type>(t_mask);
    static constexpr storage_type k_not_mask = static_cast<storage_type>(~k_mask);
    static constexpr std::size_t k_shift = static_cast<std::size_t>(bit_ops::lo_bit_index(t_mask));
    static constexpr std::size_t k_payload_mask = t_mask >> k_shift;

    static_assert(((k_payload_mask & (k_payload_mask + 1u)) == 0u), "TBitField requires a contiguous mask");

    [[nodiscard]] static constexpr bool can_encode(const std::size_t value) noexcept;

    [[nodiscard]] static constexpr storage_type encode(const std::size_t value) noexcept;

    [[nodiscard]] static constexpr std::size_t decode(const storage_type storage) noexcept;

    [[nodiscard]] static constexpr storage_type replace(const storage_type storage, const std::size_t value) noexcept;
};

//==============================================================================
//  TBitField out of class function bodies
//==============================================================================

template<typename T, std::size_t t_mask>
constexpr bool TBitField<T, t_mask>::can_encode(const std::size_t value) noexcept
{
    return value <= k_payload_mask;
}

template<typename T, std::size_t t_mask>
constexpr typename TBitField<T, t_mask>::storage_type TBitField<T, t_mask>::encode(const std::size_t value) noexcept
{
    return can_encode(value) ? static_cast<storage_type>(value << k_shift) : storage_type{ 0u };
}

template<typename T, std::size_t t_mask>
constexpr std::size_t TBitField<T, t_mask>::decode(const storage_type storage) noexcept
{
    return (static_cast<std::size_t>(storage) & t_mask) >> k_shift;
}

template<typename T, std::size_t t_mask>
constexpr typename TBitField<T, t_mask>::storage_type TBitField<T, t_mask>::replace(const storage_type storage, const std::size_t value) noexcept
{
    return static_cast<storage_type>((storage & k_not_mask) | encode(value));
}

template<std::size_t t_mask>
using TBitField16 = TBitField<std::uint16_t, t_mask>;

template<std::size_t t_mask>
using TBitField32 = TBitField<std::uint32_t, t_mask>;

#endif  //  #ifndef TBITFIELD_HPP_INCLUDED

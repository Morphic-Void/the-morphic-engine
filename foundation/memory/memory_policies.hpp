
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   memory_policies.hpp
//  Author: Ritchie Brannan
//  Date:   12 Jul 26
//
//  Shared memory limits, growth policies, and alignment policies.

#pragma once

#ifndef MEMORY_POLICIES_HPP_INCLUDED
#define MEMORY_POLICIES_HPP_INCLUDED

#include <algorithm>    //  std::min, std::max
#include <cstddef>      //  std::size_t
#include <cstdint>      //  std::uint32_t, std::uint64_t
#include <limits>       //  std::numeric_limits
#include <new>          //  std::align_val_t
#include <type_traits>  //  std::is_same_v

#include "bit_utils/bit_ops.hpp"
#include "debug/macros.hpp"

namespace memory
{

//==============================================================================
//  Shared limits
//==============================================================================

static_assert((std::is_same_v<std::size_t, std::uint32_t> || std::is_same_v<std::size_t, std::uint64_t>),
    "memory_policies.hpp requires std::size_t to be either std::uint32_t or std::uint64_t.");

//  *** DO NOT INCREASE THIS ***
//  allows the full positive signed 32-bit index range
constexpr std::size_t k_byte_size_ceiling = 0x80000000u;

//  element_size == 0 : returns the domain cap (used when size is not relevant)
[[nodiscard]] constexpr std::size_t max_elements(const std::size_t element_size = 0u) noexcept
{
    return k_byte_size_ceiling / std::max(element_size, std::size_t{ 1u });
}

template<class T>
constexpr std::size_t t_max_elements() noexcept { return max_elements(sizeof(T)); }

static_assert(max_elements() == k_byte_size_ceiling);
static_assert(max_elements(2u) == (k_byte_size_ceiling / 2u));

[[nodiscard]] constexpr bool in_non_empty_range(const std::uint32_t x, const std::uint32_t max) noexcept
{
    return (x - 1u) < max;
}

[[nodiscard]] constexpr bool in_non_empty_range(const std::uint64_t x, const std::uint64_t max) noexcept
{
    return (x - 1u) < max;
}

//==============================================================================
//  Growth policies
//==============================================================================

//  Naming convention used throughout growth policies:
//
//    capacity    = current container capacity
//    required    = minimum capacity required after growth
//    recommended = capacity recommended by the growth curve

//  base_growth_curve(required, maximum_capacity)
//
//  Produces a ~1.5x geometric growth curve by recommending the next
//  capacity class from the repeating pattern 2 -> 3 -> 4 scaled by
//  the highest power of two <= 'required'.
inline std::size_t base_growth_curve(
    const std::size_t required,
    const std::size_t maximum_capacity) noexcept
{   //  bucketed ~1.5x growth policy
    if ((maximum_capacity == 0u) || (required >= maximum_capacity))
    {
        return maximum_capacity;
    }

    std::size_t recommended = std::min(std::size_t{ 2u }, maximum_capacity);
    if (required >= 2u)
    {
        std::size_t base = bit_ops::hi_bit_mask(required);
        std::size_t step = base >> 1;
        std::size_t half = step;
        if (required & (step - 1u))
        {
            if (required & step)
            {
                step <<= 1;
            }
            half >>= 1;
        }
        if (required & half)
        {
            step <<= 1;
        }
        recommended = (step >= (maximum_capacity - base)) ? maximum_capacity : (base + step);
    }
    return recommended;
}

//  base_stepped_growth(required, pow2_stepping, maximum_capacity)
//
//  Returns the first pow2_stepping-spaced capacity class whose midpoint is greater than 'required'.
//
//  Equivalent to rounding (required + pow2_stepping / 2) up to the next multiple of pow2_stepping.
inline std::size_t base_stepped_growth(
    const std::size_t required,
    const std::size_t pow2_stepping,
    const std::size_t maximum_capacity) noexcept
{   //  pow2_stepping must be a power of 2 and >= 2.
    MV_ASSERT((pow2_stepping >= 2) && bit_ops::is_pow2(pow2_stepping));
    if (required >= maximum_capacity)
    {
        return maximum_capacity;
    }
    const std::size_t round_offset = pow2_stepping + (pow2_stepping >> 1u) - 1u;
    if (required > (std::numeric_limits<std::size_t>::max() - round_offset))
    {
        return maximum_capacity;
    }
    const std::size_t stepped = (required + round_offset) & ~(pow2_stepping - 1u);
    return std::min(stepped, maximum_capacity);
}

//  capped_growth_rate_curve(required, pow2_stepping, maximum_capacity)
//
//  Returns the minimum of the geometric base_growth_curve() and the stepped base_stepped_growth()
//  recommendation curves.
//
//  This makes pow2_stepping the maximum step between successive recommended capacity classes.
inline std::size_t capped_growth_rate_curve(
    const std::size_t required,
    const std::size_t pow2_stepping,
    const std::size_t maximum_capacity) noexcept
{   //  pow2_stepping must be a power of 2 and >= 2.
    MV_ASSERT((pow2_stepping >= 2) && bit_ops::is_pow2(pow2_stepping));
    return std::min(
        base_growth_curve(required, maximum_capacity),
        base_stepped_growth(required, pow2_stepping, maximum_capacity));
}

constexpr std::size_t k_buffer_growth_policy_capped_rate{ 65536u };
constexpr std::size_t k_buffer_growth_policy_min_capacity{ 4096u };

inline std::size_t buffer_growth_policy(
    const std::size_t required,
    const std::size_t maximum_capacity) noexcept
{   //  default policy when additional capacity is required for automatic buffer growth
    return std::min(
        std::max(capped_growth_rate_curve(required, k_buffer_growth_policy_capped_rate, maximum_capacity),
            k_buffer_growth_policy_min_capacity),
        maximum_capacity);
}

constexpr std::size_t k_vector_growth_policy_capped_rate{ 1024u };
constexpr std::size_t k_vector_growth_policy_min_capacity{ 32u };

inline std::size_t vector_growth_policy(
    const std::size_t required,
    const std::size_t maximum_capacity) noexcept
{   //  default policy when additional capacity is required for automatic vector growth
    return std::min(
        std::max(capped_growth_rate_curve(required, k_vector_growth_policy_capped_rate, maximum_capacity),
            k_vector_growth_policy_min_capacity),
        maximum_capacity);
}

constexpr std::size_t k_default_growth_policy_min_capacity{ 32u };

inline std::size_t default_growth_policy(
    const std::size_t required,
    const std::size_t maximum_capacity) noexcept
{   //  default policy when additional capacity is required with no capped growth rate
    return std::min(
        std::max(base_growth_curve(required, maximum_capacity), k_default_growth_policy_min_capacity),
        maximum_capacity);
}

//==============================================================================
//  Alignment policies
//==============================================================================

[[nodiscard]] constexpr std::size_t condition_alignment(const std::size_t requested_alignment) noexcept
{
    return std::max(bit_ops::reduce_alignment_to_pow2(requested_alignment), std::size_t{ alignof(void*) });
}

[[nodiscard]] constexpr std::size_t condition_bytes(
    const std::size_t conditioned_alignment,
    const std::size_t requested_bytes) noexcept
{
    if ((condition_alignment(conditioned_alignment) != conditioned_alignment) ||
        !in_non_empty_range(requested_bytes, k_byte_size_ceiling))
    {
        return 0u;
    }

    const std::size_t mask = conditioned_alignment - 1u;
    if (requested_bytes > (std::numeric_limits<std::size_t>::max() - mask))
    {
        return 0u;
    }
    const std::size_t conditioned_bytes = (requested_bytes + mask) & ~mask;
    return (conditioned_bytes <= k_byte_size_ceiling) ? conditioned_bytes : std::size_t{ 0u };
}

constexpr std::size_t byte_alignment_policy(const std::size_t align) noexcept
{   //  retained size-returning alternative to alignment_policy
    return condition_alignment(align);
}

constexpr std::align_val_t alignment_policy(const std::size_t align) noexcept
{   //  apply the alignment policy and cast the type
    return std::align_val_t{ condition_alignment(align) };
}

template<class T>
constexpr std::size_t t_default_align() noexcept
{
    constexpr std::size_t align = sizeof(T);
    return std::max((align & ~(align - 1u)), alignof(T));
}

}   //  namespace memory

#endif  //  #ifndef MEMORY_POLICIES_HPP_INCLUDED

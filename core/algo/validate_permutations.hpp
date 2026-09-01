
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
// 
//  File:   validate_permutations.hpp
//  Author: Ritchie Brannan
//  Date:   10 Mar 26
//
//  Permutation validation (C++17, noexcept).
//
//  Purpose:
//
//      Side-effect free noexcept permutation validation over either:
//      - raw values, or
//      - values extracted from a sequence of elements.
//
//      O(N) alternatives may be preferable if side-effects, mutability,
//      and/or dynamic allocation are acceptable.
//
//  Utilities:
//
//      validate_raw_permutation(...)
//          Validates that a raw value array contains every value in
//          [base_value, base_value + count) exactly once.
//
//      validate_extracted_permutation(...)
//          Validates that the values extracted from an element sequence
//          contain every value in [base_value, base_value + count)
//          exactly once.
//
//  Method:
//
//      * range-check all candidate values
//      * repeatedly split the validated value range in half
//      * count the population of the lower half only
//      * infer the upper-half population by subtraction
//      * defer non-trivial upper halves for later processing
//
//  Complexity:
//
//      Time:   O(N log N)
//      Space:  O(log V), bounded here by the bit width of std::size_t
//

#pragma once

#ifndef VALIDATE_PERMUTATIONS_HPP_INCLUDED
#define VALIDATE_PERMUTATIONS_HPP_INCLUDED

#include <cstdint>
#include <limits>

namespace algo
{

//==============================================================================
//  Validate that the values extracted from a sequence of elements contain
//  every value in [base_value, base_value + count) exactly once.
//
//  Template parameters:
//      TElement    - element type stored in the input sequence
//      TExtract    - callable returning the permutation value for an element
//
//  Requirements:
//      * values is either nullptr with count == 0, or points to at least count elements
//      * extract(values[i]) must be representable as std::size_t
//      * base_value + count must not overflow std::size_t
//
//  Notes:
//      * The function is side-effect free
//      * The extracted value domain is interpreted as std::size_t
//      * This is intended for validation and integrity checking, not hot-path use
//==============================================================================

template<typename TElement, typename TExtract>
inline bool validate_extracted_permutation(const TElement* const values, const std::size_t count, TExtract&& extract, const std::size_t base_value = 0u) noexcept
{
    //--------------------------------------------------------------------------
    //  Limits
    //--------------------------------------------------------------------------

    constexpr std::size_t k_maximum_value = static_cast<std::size_t>(std::numeric_limits<std::size_t>::max());
    constexpr std::size_t k_deferred_capacity = static_cast<std::size_t>(std::numeric_limits<std::size_t>::digits);

    //--------------------------------------------------------------------------
    //  Basic sanity checks
    //--------------------------------------------------------------------------

    if (count == 0u)
    {
        return true;
    }

    if ((values == nullptr) || (base_value > (k_maximum_value - count)))
    {
        return false;
    }

    //--------------------------------------------------------------------------
    //  Initial range validation
    //--------------------------------------------------------------------------

    for (std::size_t i = std::size_t{ 0 }; i < count; ++i)
    {
        const std::size_t x = static_cast<std::size_t>(extract(values[i]));

        if ((x - base_value) >= count)
        {
            return false;
        }
    }

    //--------------------------------------------------------------------------
    //  Deferred interval stack
    //--------------------------------------------------------------------------

    struct value_range_t
    {
        std::size_t lo;
        std::size_t hi;
    };

    value_range_t deferred[k_deferred_capacity];
    std::size_t deferred_count = 0u;

    //  Seed traversal with the full validated range [base_value, base_value + count).
    deferred[deferred_count++] = { base_value, static_cast<std::size_t>(base_value + count) };

    //--------------------------------------------------------------------------
    //  Depth-first traversal of value interval tree
    //--------------------------------------------------------------------------

    while (deferred_count > 0u)
    {
        const value_range_t range = deferred[--deferred_count];

        const std::size_t lo = range.lo;
        std::size_t hi = range.hi;
        std::size_t width = hi - lo;

        //  Descend the left spine of this subtree. After the shift, width is
        //  the expected population of the lower half [lo, lo + width).
        while (width > std::size_t{ 1 })
        {
            width >>= 1u;

            //  actual_width counts how many elements actually fall in the
            //  lower-half range [lo, lo + width).
            std::size_t actual_width = std::size_t{ 0 };

            for (std::size_t i = std::size_t{ 0 }; i < count; ++i)
            {
                const std::size_t x = static_cast<std::size_t>(extract(values[i]));

                if ((x - lo) < width)
                {
                    ++actual_width;
                }
            }

            if (actual_width != width)
            {
                return false;
            }

            const std::size_t mid = static_cast<std::size_t>(lo + width);

            if ((hi - mid) > std::size_t{ 1 })
            {
                deferred[deferred_count++] = { mid, hi };
            }

            hi = mid;
        }
    }

    return true;
}

//==============================================================================
//  Validate that a raw value array contains every value in
//  [base_value, base_value + count) exactly once.
//
//  Template parameters:
//      TElement    - element type stored in the array
//      TInternal   - internal arithmetic type (defaults to TElement)
//      ValueBits   - bit width of the value domain so that (max_value - base_value) < (1 << ValueBits)
//
//  Requirements:
//      * TElement and TInternal must be unsigned integer types
//      * ValueBits must fit within both TElement and TInternal
//      * values is either nullptr with count == 0, or points to at least count values
//      * base_value + count must not overflow the internal value domain
//
//  Notes:
//      * This is the raw-value specialisation of the permutation validator
//      * Prefer validate_extracted_permutation(...) when validating embedded fields
//==============================================================================

template<
    typename TElement = std::size_t,
    typename TInternal = TElement,
    std::size_t ValueBits = std::numeric_limits<std::conditional_t<(sizeof(TElement) <= sizeof(TInternal)), TElement, TInternal>>::digits>
inline bool validate_raw_permutation(const TElement* const values, const TInternal count, const TInternal base_value = TInternal{ 0 }) noexcept
{
    //--------------------------------------------------------------------------
    //  Compile-time constraints
    //--------------------------------------------------------------------------

    static_assert((std::numeric_limits<TElement>::is_integer && !std::numeric_limits<TElement>::is_signed && !std::is_same_v<TElement, bool>), "TElement must be an unsigned integer type");
    static_assert((std::numeric_limits<TInternal>::is_integer && !std::numeric_limits<TInternal>::is_signed && !std::is_same_v<TInternal, bool>), "TInternal must be an unsigned integer type");

    static_assert(ValueBits > 0u, "ValueBits must be at least 1");
    static_assert(ValueBits <= std::numeric_limits<TElement>::digits, "ValueBits exceeds TElement bit capacity");
    static_assert(ValueBits <= std::numeric_limits<TInternal>::digits, "ValueBits exceeds TInternal bit capacity");

    //--------------------------------------------------------------------------
    //  Derived limits
    //--------------------------------------------------------------------------

    constexpr TInternal k_maximum_value = (TInternal{ 1 } << (ValueBits - 1u));
    constexpr std::size_t k_deferred_capacity = ValueBits;

    //--------------------------------------------------------------------------
    //  Basic sanity checks
    //--------------------------------------------------------------------------

    if (count == TInternal{ 0 })
    {
        return true;
    }

    if ((values == nullptr) || (count > k_maximum_value) || (base_value >= k_maximum_value) || (count > (k_maximum_value - base_value)))
    {
        return false;
    }

    //--------------------------------------------------------------------------
    //  Initial range validation
    //--------------------------------------------------------------------------

    for (TInternal i = TInternal{ 0 }; i < count; ++i)
    {
        const TInternal x = static_cast<TInternal>(values[i]);

        if ((x - base_value) >= count)
        {
            return false;
        }
    }

    //--------------------------------------------------------------------------
    //  Deferred interval stack
    //--------------------------------------------------------------------------

    struct value_range_t
    {
        TInternal lo;
        TInternal hi;
    };

    value_range_t deferred[k_deferred_capacity];
    std::size_t deferred_count = 0u;

    //  Seed traversal with the full validated range [base_value, base_value + count).
    deferred[deferred_count++] = { base_value, static_cast<TInternal>(base_value + count) };

    //--------------------------------------------------------------------------
    //  Depth-first traversal of value interval tree
    //--------------------------------------------------------------------------

    while (deferred_count > 0u)
    {
        const value_range_t range = deferred[--deferred_count];

        const TInternal lo = range.lo;
        TInternal hi = range.hi;
        TInternal width = hi - lo;

        //  Descend the left spine of this subtree. After the shift, width is
        //  the expected population of the lower half [lo, lo + width).
        while (width > TInternal{ 1 })
        {
            width >>= 1u;

            //  actual_width counts how many elements actually fall in the
            //  lower-half range [lo, lo + width).
            TInternal actual_width = TInternal{ 0 };

            for (TInternal i = TInternal{ 0 }; i < count; ++i)
            {
                const TInternal x = static_cast<TInternal>(values[i]);

                if ((x - lo) < width)
                {
                    ++actual_width;
                }
            }

            if (actual_width != width)
            {
                return false;
            }

            const TInternal mid = static_cast<TInternal>(lo + width);

            if ((hi - mid) > TInternal{ 1 })
            {
                deferred[deferred_count++] = { mid, hi };
            }

            hi = mid;
        }
    }

    return true;
}

}   //  namespace algo

#endif  //  VALIDATE_PERMUTATIONS_HPP_INCLUDED


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
//      Allocation-free noexcept permutation validation over either:
//      - raw values, or
//      - values extracted from a sequence of elements.
//
//      Input values are never modified. The interval checkers require only a
//      bounded local stack and remain suitable for low-memory validation.
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
//      validate_raw_permutation_bitmap(...)
//      validate_extracted_permutation_bitmap(...)
//          Additional entry points using a bitmap when it fits. Overloads take
//          caller-owned scratch bytes or use a 1024-byte stack bitmap for up to
//          8192 values. Absent or insufficient scratch falls back to the interval
//          checker. No path dynamically allocates memory.
//
//  Interval method:
//
//      * range-check all candidate values
//      * repeatedly split the validated value range in half
//      * count the population of the lower half only
//      * infer the upper-half population by subtraction
//      * defer non-trivial upper halves for later processing
//
//  Complexity:
//
//      Time:   O(N^2); valid non-empty input requires exactly N^2 value reads:
//              N initial range checks plus N reads for each of N - 1 splits.
//      Space:  O(log N) pending intervals in a fixed-capacity local stack,
//              bounded by the arithmetic type's bit width (ValueBits for raw).
//
//      Bitmap path: O(N) time, ceil(N / 8) scratch bytes used. Scratch is cleared
//      internally; its used prefix may be changed even if validation fails.
//      Fallback retains the interval checker's O(N^2) time and stack bounds.
//

#pragma once

#ifndef VALIDATE_PERMUTATIONS_HPP_INCLUDED
#define VALIDATE_PERMUTATIONS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>
#include <utility>

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
//      * extract must not throw, modify the input, or change its returned value
//        for an element between calls
//      * base_value + count must not overflow std::size_t
//
//  Notes:
//      * The function is side-effect free
//        provided the extractor is side-effect free
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

namespace permutation_detail
{

template<typename TElement, typename TInternal, std::size_t ValueBits>
static constexpr bool is_raw_range_valid(const TInternal count, const TInternal base_value) noexcept
{
    static_assert((std::numeric_limits<TElement>::is_integer && !std::numeric_limits<TElement>::is_signed && !std::is_same_v<TElement, bool>), "TElement must be an unsigned integer type");
    static_assert((std::numeric_limits<TInternal>::is_integer && !std::numeric_limits<TInternal>::is_signed && !std::is_same_v<TInternal, bool>), "TInternal must be an unsigned integer type");

    static_assert(ValueBits > 0u, "ValueBits must be at least 1");
    static_assert(ValueBits <= std::numeric_limits<TElement>::digits, "ValueBits exceeds TElement bit capacity");
    static_assert(ValueBits <= std::numeric_limits<TInternal>::digits, "ValueBits exceeds TInternal bit capacity");

    constexpr TInternal k_maximum_value = std::numeric_limits<TInternal>::max();
    constexpr TInternal k_maximum_offset = static_cast<TInternal>(k_maximum_value >> (std::numeric_limits<TInternal>::digits - ValueBits));

    return (count == TInternal{ 0 }) || (((count - TInternal{ 1 }) <= k_maximum_offset) && (base_value <= (k_maximum_value - count)));
}

}   //  namespace permutation_detail

//==============================================================================
//  Validate that a raw value array contains every value in
//  [base_value, base_value + count) exactly once.
//
//  Template parameters:
//      TElement    - element type stored in the array
//      TInternal   - internal arithmetic type (defaults to TElement)
//      ValueBits   - bit width of offsets from base_value; count - 1 must fit
//                    within ValueBits for a non-empty range
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
//      * Values are checked before narrowing to TInternal
//      * ValueBits does not restrict the absolute base_value
//==============================================================================

template<
    typename TElement = std::size_t,
    typename TInternal = TElement,
    std::size_t ValueBits = std::numeric_limits<std::conditional_t<(sizeof(TElement) <= sizeof(TInternal)), TElement, TInternal>>::digits>
inline bool validate_raw_permutation(const TElement* const values, const TInternal count, const TInternal base_value = TInternal{ 0 }) noexcept
{
    //--------------------------------------------------------------------------
    //  Derived limits
    //--------------------------------------------------------------------------

    constexpr std::size_t k_deferred_capacity = ValueBits;

    //--------------------------------------------------------------------------
    //  Basic sanity checks
    //--------------------------------------------------------------------------

    if (count == TInternal{ 0 })
    {
        return true;
    }

    if ((values == nullptr) || !permutation_detail::is_raw_range_valid<TElement, TInternal, ValueBits>(count, base_value))
    {
        return false;
    }

    //--------------------------------------------------------------------------
    //  Initial range validation
    //--------------------------------------------------------------------------

    for (TInternal i = TInternal{ 0 }; i < count; ++i)
    {
        const TElement value = values[i];
        if constexpr (std::numeric_limits<TElement>::digits > std::numeric_limits<TInternal>::digits)
        {
            if (value > std::numeric_limits<TInternal>::max())
            {
                return false;
            }
        }

        const TInternal x = static_cast<TInternal>(value);

        if ((x < base_value) || ((x - base_value) >= count))
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

                if ((x >= lo) && ((x - lo) < width))
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

//==============================================================================
//  Bitmap validation of extracted values, with caller-owned scratch.
//
//  The value range and extractor requirements match validate_extracted_permutation.
//  Scratch is measured in bytes and needs ceil(count / 8) bytes for the bitmap
//  path. A null pointer or insufficient capacity selects the interval checker
//  without touching scratch. An empty input returns true without touching either
//  input or scratch. A null non-empty input or overflowing range returns false.
//
//  If used, scratch must point to writable storage of at least scratch_bytes
//  bytes and must not overlap the input or any state read by the extractor.
//  Only the required prefix is cleared and modified; its contents are unspecified
//  on return. The input is preserved. Sufficient scratch gives O(N) time with
//  exactly N extractor calls for valid input; fallback gives O(N^2) time.
//==============================================================================

template<typename TElement, typename TExtract>
inline bool validate_extracted_permutation_bitmap(
    const TElement* const values, const std::size_t count, TExtract&& extract,
    std::uint8_t* const scratch, const std::size_t scratch_bytes, const std::size_t base_value = 0u) noexcept
{
    if (count == 0u)
    {
        return true;
    }

    if ((values == nullptr) || (base_value > (std::numeric_limits<std::size_t>::max() - count)))
    {
        return false;
    }

    //  Division before addition avoids overflowing at the largest counts.
    const std::size_t required_bytes = (count / 8u) + ((count % 8u) != 0u);
    if ((scratch == nullptr) || (scratch_bytes < required_bytes))
    {
        return validate_extracted_permutation(values, count, std::forward<TExtract>(extract), base_value);
    }

    std::memset(scratch, 0, required_bytes);

    for (std::size_t i = 0u; i < count; ++i)
    {
        const std::size_t offset = static_cast<std::size_t>(extract(values[i])) - base_value;
        if (offset >= count)
        {
            return false;
        }

        const std::size_t byte_index = offset / 8u;
        const std::uint8_t mask = static_cast<std::uint8_t>(1u << (offset % 8u));
        if ((scratch[byte_index] & mask) != 0u)
        {
            return false;
        }
        scratch[byte_index] |= mask;
    }

    //  N distinct values in an N-value range necessarily cover the range.
    return true;
}

//==============================================================================
//  Stack-bitmap overload: up to 8192 values use 1024 local scratch bytes.
//  Larger inputs use the existing interval checker. Requirements and results
//  otherwise match the caller-scratch overload, with no caller-visible writes
//  provided the extractor is side-effect free.
//==============================================================================

template<typename TElement, typename TExtract>
inline bool validate_extracted_permutation_bitmap(
    const TElement* const values, const std::size_t count, TExtract&& extract, const std::size_t base_value = 0u) noexcept
{
    if (count > 8192u)
    {
        return validate_extracted_permutation(values, count, std::forward<TExtract>(extract), base_value);
    }

    std::uint8_t scratch[1024u];
    return validate_extracted_permutation_bitmap(values, count, std::forward<TExtract>(extract), scratch, sizeof(scratch), base_value);
}

//==============================================================================
//  Raw-value bitmap validation, with caller-owned scratch.
//
//  Types, ValueBits, and the value range follow validate_raw_permutation. Scratch
//  follows validate_extracted_permutation_bitmap, including fallback when absent
//  or too small. Counts exceeding size_t use the raw interval checker directly.
//  Values are range-checked before conversion to bitmap offsets, so a wider
//  TElement cannot be silently truncated to TInternal or size_t.
//==============================================================================

template<
    typename TElement = std::size_t,
    typename TInternal = TElement,
    std::size_t ValueBits = std::numeric_limits<std::conditional_t<(sizeof(TElement) <= sizeof(TInternal)), TElement, TInternal>>::digits>
inline bool validate_raw_permutation_bitmap(
    const TElement* const values, const TInternal count,
    std::uint8_t* const scratch, const std::size_t scratch_bytes, const TInternal base_value = TInternal{ 0 }) noexcept
{
    if (!permutation_detail::is_raw_range_valid<TElement, TInternal, ValueBits>(count, base_value))
    {
        return false;
    }

    if constexpr (std::numeric_limits<TInternal>::digits > std::numeric_limits<std::size_t>::digits)
    {
        if (count > std::numeric_limits<std::size_t>::max())
        {
            return validate_raw_permutation<TElement, TInternal, ValueBits>(values, count, base_value);
        }
    }

    const std::size_t bitmap_count = static_cast<std::size_t>(count);
    const std::size_t required_bytes = (bitmap_count / 8u) + ((bitmap_count % 8u) != 0u);
    if ((scratch == nullptr) || (scratch_bytes < required_bytes))
    {
        return validate_raw_permutation<TElement, TInternal, ValueBits>(values, count, base_value);
    }

    return validate_extracted_permutation_bitmap(values, bitmap_count,
        [count, base_value](const TElement value) noexcept -> std::size_t
        {
            if ((value < base_value) || ((value - base_value) >= count))
            {
                return static_cast<std::size_t>(count);   //  Invalid offset, outside [0, count).
            }
            return static_cast<std::size_t>(value - base_value);
        }, scratch, scratch_bytes);
}

//==============================================================================
//  Raw-value stack-bitmap overload, with the same 8192-value threshold and
//  interval fallback as the extracted stack-bitmap overload.
//==============================================================================

template<
    typename TElement = std::size_t,
    typename TInternal = TElement,
    std::size_t ValueBits = std::numeric_limits<std::conditional_t<(sizeof(TElement) <= sizeof(TInternal)), TElement, TInternal>>::digits>
inline bool validate_raw_permutation_bitmap(
    const TElement* const values, const TInternal count, const TInternal base_value = TInternal{ 0 }) noexcept
{
    if (count > 8192u)
    {
        return validate_raw_permutation<TElement, TInternal, ValueBits>(values, count, base_value);
    }

    std::uint8_t scratch[1024u];
    return validate_raw_permutation_bitmap<TElement, TInternal, ValueBits>(values, count, scratch, sizeof(scratch), base_value);
}

}   //  namespace algo

#endif  //  VALIDATE_PERMUTATIONS_HPP_INCLUDED


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    buddy_index.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    07 Nov 26
//
//  Internal three-tier distinct-node buddy index. All indices use smallest slots.
//  Ordinary words require exclusive access; this is not a concurrent allocator.

#pragma once

#include <algorithm>
#include <cstdint>
#include <type_traits>

#include "bit_utils/bit_ops.hpp"

namespace buddy_detail
{

template<typename TWord>
struct TPackedIndex
{
    static_assert((std::is_same_v<TWord, std::uint32_t> || std::is_same_v<TWord, std::uint64_t>),
        "Buddy words must be uint32_t or uint64_t.");

    static constexpr std::uint32_t k_width = sizeof(TWord) * 8u;
    static constexpr std::uint32_t k_levels = (k_width == 32u) ? 5u : 6u;
    static constexpr std::uint32_t k_max_order = (3u * k_levels) - 1u;
    static constexpr std::uint32_t k_slot_count = 1u << k_max_order;
    static constexpr std::uint32_t k_no_slot = k_slot_count;
    static constexpr std::uint32_t k_word_count = 1u + k_width + (k_width * k_width);

    TWord words[k_word_count]{};
    TWord top_routes[2u * k_levels]{};
    TWord middle_routes[k_width][k_levels]{};
    std::uint32_t available{ 0u };
    std::uint32_t active_order{ 0u };
    std::uint32_t live_count{ 0u };
    std::uint32_t live_slots{ 0u };

    static constexpr std::uint32_t tier(const std::uint32_t order) noexcept;
    static constexpr std::uint32_t local_depth(const std::uint32_t order) noexcept;
    static constexpr std::uint32_t word_base(const std::uint32_t layer) noexcept;
    [[nodiscard]] bool reserved(const std::uint32_t order, const std::uint32_t node) const noexcept;
    void set_reserved(const std::uint32_t order, const std::uint32_t node, const bool value) noexcept;
    void reset(const std::uint32_t order) noexcept;

    //  Bits are local positions at the requested level, not packed bit offsets.
    //  A clear node is maximal free only beside a reserved sibling. The active
    //  root is the sole exception; unreachable clear descendants never qualify.
    [[nodiscard]] TWord candidates(const std::uint32_t order, const std::uint32_t word) const noexcept;

    [[nodiscard]] TWord& route(const std::uint32_t layer, const std::uint32_t word, const std::uint32_t order) noexcept;
    [[nodiscard]] const TWord& route(const std::uint32_t layer, const std::uint32_t word, const std::uint32_t order) const noexcept;

    //  Refresh one word's contribution, stopping once its ancestors cannot change.
    void refresh(const std::uint32_t order, std::uint32_t word) noexcept;

    void refresh_path(const std::uint32_t slot, const std::uint32_t first_order, const std::uint32_t last_order) noexcept;
    [[nodiscard]] std::uint32_t allocate(const std::uint32_t requested_order) noexcept;

    //  Ascend only through left children with clear siblings. This rejects a
    //  previously freed left child even when its right buddy remains allocated.
    [[nodiscard]] std::int32_t allocation_order(const std::uint32_t slot) const noexcept;

    [[nodiscard]] std::int32_t release(const std::uint32_t slot) noexcept;
};

//==============================================================================
//  TPackedIndex out of class function bodies
//==============================================================================

template<typename TWord>
constexpr std::uint32_t TPackedIndex<TWord>::tier(const std::uint32_t order) noexcept
{
    return (k_max_order - order) / k_levels;
}

template<typename TWord>
constexpr std::uint32_t TPackedIndex<TWord>::local_depth(const std::uint32_t order) noexcept
{
    return (k_max_order - order) % k_levels;
}

template<typename TWord>
constexpr std::uint32_t TPackedIndex<TWord>::word_base(const std::uint32_t layer) noexcept
{
    return (layer == 0u) ? 0u : ((layer == 1u) ? 1u : (1u + k_width));
}

template<typename TWord>
bool TPackedIndex<TWord>::reserved(const std::uint32_t order, const std::uint32_t node) const noexcept
{
    const std::uint32_t depth = local_depth(order);
    const std::uint32_t count = 1u << depth;
    const std::uint32_t bit = k_width - (2u * count) + (node & (count - 1u));
    return (words[word_base(tier(order)) + (node >> depth)] & (TWord{ 1u } << bit)) != 0u;
}

template<typename TWord>
void TPackedIndex<TWord>::set_reserved(const std::uint32_t order, const std::uint32_t node, const bool value) noexcept
{
    const std::uint32_t depth = local_depth(order);
    const std::uint32_t count = 1u << depth;
    const std::uint32_t bit = k_width - (2u * count) + (node & (count - 1u));
    TWord& word = words[word_base(tier(order)) + (node >> depth)];
    const TWord mask = TWord{ 1u } << bit;
    word = value ? (word | mask) : (word & ~mask);
}

template<typename TWord>
void TPackedIndex<TWord>::reset(const std::uint32_t order) noexcept
{
    std::fill_n(words, k_word_count, TWord{ 0u });
    std::fill_n(top_routes, (2u * k_levels), TWord{ 0u });
    for (auto& routes : middle_routes) std::fill_n(routes, k_levels, TWord{ 0u });
    active_order = order;
    available = 1u << order;
    live_count = 0u;
    live_slots = 0u;
}

template<typename TWord>
TWord TPackedIndex<TWord>::candidates(const std::uint32_t order, const std::uint32_t word) const noexcept
{
    if (order > active_order) return 0u;
    if (order == active_order)
    {
        return ((word == 0u) && !reserved(order, 0u)) ? TWord{ 1u } : TWord{ 0u };
    }
    const std::uint32_t depth = local_depth(order);
    const std::uint32_t count = 1u << depth;
    const std::uint32_t active_nodes = 1u << (active_order - order);
    const std::uint32_t first = word * count;
    if (first >= active_nodes) return 0u;
    if (depth == 0u)
    {
        return (!reserved(order, word) && reserved(order, (word ^ 1u))) ? TWord{ 1u } : TWord{ 0u };
    }
    const std::uint32_t usable = std::min(count, (active_nodes - first));
    const TWord mask = (TWord{ 1u } << usable) - 1u;
    const TWord level = words[word_base(tier(order)) + word] >> (k_width - (2u * count));
    constexpr TWord even = ~TWord{ 0u } / 3u;
    const TWord siblings = ((level & even) << 1u) | ((level >> 1u) & even);
    return ~level & siblings & mask;
}

template<typename TWord>
TWord& TPackedIndex<TWord>::route(const std::uint32_t layer, const std::uint32_t word, const std::uint32_t order) noexcept
{
    return (layer == 0u) ? top_routes[order] : middle_routes[word][order];
}

template<typename TWord>
const TWord& TPackedIndex<TWord>::route(const std::uint32_t layer, const std::uint32_t word, const std::uint32_t order) const noexcept
{
    return (layer == 0u) ? top_routes[order] : middle_routes[word][order];
}

template<typename TWord>
void TPackedIndex<TWord>::refresh(const std::uint32_t order, std::uint32_t word) noexcept
{
    bool present = candidates(order, word) != 0u;
    std::uint32_t layer = tier(order);
    const std::uint32_t active_layer = tier(active_order);
    while (layer > active_layer)
    {
        const TWord bit = TWord{ 1u } << (word % k_width);
        word /= k_width;
        --layer;
        TWord& mask = route(layer, word, order);
        const TWord before = mask;
        mask = present ? (mask | bit) : (mask & ~bit);
        if ((before != 0u) == (mask != 0u)) return;
        present = mask != 0u;
    }
    const std::uint32_t bit = 1u << order;
    available = present ? (available | bit) : (available & ~bit);
}

template<typename TWord>
void TPackedIndex<TWord>::refresh_path(const std::uint32_t slot, const std::uint32_t first_order, const std::uint32_t last_order) noexcept
{
    for (std::uint32_t order = first_order; order <= last_order; ++order)
    {
        const std::uint32_t depth = local_depth(order);
        const std::uint32_t word = (slot >> order) >> depth;
        refresh(order, word);
        //  At a word root the changed node also affects its sibling word's
        //  maximal-free test. Within a word candidates() already sees both.
        if ((depth == 0u) && (order < active_order))
        {
            refresh(order, (word ^ 1u));
        }
    }
}

template<typename TWord>
std::uint32_t TPackedIndex<TWord>::allocate(const std::uint32_t requested_order) noexcept
{
    if (requested_order > active_order)
    {
        return k_no_slot;
    }
    const std::uint32_t sufficient = available & (~std::uint32_t{ 0u } << requested_order);
    if (sufficient == 0u) return k_no_slot;
    const std::uint32_t order = static_cast<std::uint32_t>(bit_ops::lo_bit_index(sufficient));
    std::uint32_t layer = tier(active_order);
    std::uint32_t word = 0u;
    while (layer < tier(order))
    {
        const std::uint32_t child = static_cast<std::uint32_t>(bit_ops::lo_bit_index(route(layer, word, order)));
        word = (word * k_width) + child;
        ++layer;
    }
    const std::uint32_t position = static_cast<std::uint32_t>(bit_ops::lo_bit_index(candidates(order, word)));
    const std::uint32_t slot = ((word << local_depth(order)) + position) << order;
    for (std::uint32_t parent = requested_order; parent <= active_order; ++parent)
    {
        set_reserved(parent, (slot >> parent), true);
    }
    refresh_path(slot, requested_order, order);
    ++live_count;
    live_slots += 1u << requested_order;
    return slot;
}

template<typename TWord>
std::int32_t TPackedIndex<TWord>::allocation_order(const std::uint32_t slot) const noexcept
{
    if (slot >= (1u << active_order)) return -1;
    std::uint32_t node = slot;
    for (std::uint32_t order = 0u; order <= active_order; ++order)
    {
        if (reserved(order, node))
        {
            return static_cast<std::int32_t>(order);
        }
        if ((order == active_order) || ((node & 1u) != 0u) || reserved(order, (node ^ 1u)))
        {
            return -1;
        }
        node >>= 1u;
    }
    return -1;
}

template<typename TWord>
std::int32_t TPackedIndex<TWord>::release(const std::uint32_t slot) noexcept
{
    const std::int32_t found = allocation_order(slot);
    if (found < 0) return -1;
    const std::uint32_t order = static_cast<std::uint32_t>(found);
    std::uint32_t parent = order;
    std::uint32_t node = slot >> order;
    set_reserved(parent, node, false);
    while (parent < active_order)
    {
        if (reserved(parent, (node ^ 1u))) break;
        node >>= 1u;
        ++parent;
        set_reserved(parent, node, false);
    }
    refresh_path(slot, order, parent);
    --live_count;
    live_slots -= 1u << order;
    return found;
}

}   //  namespace buddy_detail

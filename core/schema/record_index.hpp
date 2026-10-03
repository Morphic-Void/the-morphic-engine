
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    record_index.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Private ordinal indexes. Keys stay in their owning record tables.

#pragma once

#ifndef RECORD_INDEX_HPP_INCLUDED
#define RECORD_INDEX_HPP_INCLUDED

#include "containers/TPodVector.hpp"

namespace schema
{

inline constexpr std::uint32_t k_missing_record = UINT32_MAX;

//  Compatibility-pair ordinals at at most half occupancy. Reserve may rehash;
//  insert/find/erase never allocate. No pointers into the pair table survive.
class COrdinalHashIndex
{
public:
    template <class THash>
    [[nodiscard]] bool reserve(const std::size_t count, const std::size_t existing, const THash& hash) noexcept
    {
        if (count <= (m_slots.size() / 2u))
        {
            return true;
        }
        std::size_t capacity = m_slots.size() ? m_slots.size() : 8u;
        while (count > (capacity / 2u))
        {
            if (capacity > (memory::k_byte_size_ceiling / (2u * sizeof(std::uint32_t))))
            {
                return false;
            }
            capacity *= 2u;
        }
        COrdinalHashIndex staged;
        if (!staged.m_slots.allocate(capacity) || !staged.m_slots.set_size(capacity))
        {
            return false;
        }
        staged.rebuild(existing, hash);
        m_slots = std::move(staged.m_slots);
        return true;
    }

    void insert(const std::uint32_t ordinal, const std::uint32_t hash) noexcept
    {
        const std::size_t mask = m_slots.size() - 1u;
        std::size_t slot = static_cast<std::size_t>(hash) & mask;
        while (m_slots[slot] != k_missing_record)
        {
            slot = (slot + 1u) & mask;
        }
        m_slots[slot] = ordinal;
    }

    template <class THash>
    void erase(const std::uint32_t ordinal, const THash& hash) noexcept
    {
        const std::size_t mask = m_slots.size() - 1u;
        std::size_t hole = static_cast<std::size_t>(hash(ordinal)) & mask;
        while (m_slots[hole] != ordinal)
        {
            hole = (hole + 1u) & mask;
        }
        std::size_t slot = (hole + 1u) & mask;
        while (m_slots[slot] != k_missing_record)
        {
            const std::size_t home = static_cast<std::size_t>(hash(m_slots[slot])) & mask;
            if (((slot - home) & mask) >= ((slot - hole) & mask))
            {
                m_slots[hole] = m_slots[slot];
                hole = slot;
            }
            slot = (slot + 1u) & mask;
        }
        m_slots[hole] = k_missing_record;
    }

    template <class TMatch>
    [[nodiscard]] std::uint32_t find(const std::uint32_t hash, const TMatch& match) const noexcept
    {
        if (!m_slots.size())
        {
            return k_missing_record;
        }
        const std::size_t mask = m_slots.size() - 1u;
        std::size_t slot = static_cast<std::size_t>(hash) & mask;
        while (m_slots[slot] != k_missing_record)
        {
            const std::uint32_t ordinal = m_slots[slot];
            if (match(ordinal))
            {
                return ordinal;
            }
            slot = (slot + 1u) & mask;
        }
        return k_missing_record;
    }

private:
    template <class THash>
    void rebuild(const std::size_t count, const THash& hash) noexcept
    {
        if (m_slots.size())
        {
            std::fill_n(m_slots.data(), m_slots.size(), k_missing_record);
        }
        for (std::size_t i = 0u; i < count; ++i)
        {
            insert(static_cast<std::uint32_t>(i), hash(i));
        }
    }

    TPodVector<std::uint32_t> m_slots;
};

//  Live roles append freshly allocated monotonic entry keys. Replacement keeps
//  the key, ordered erasure keeps the sequence, and promotion/reconciliation
//  create fresh keys in record order. Search that sequence without an index.
template <class TRecords, class TKey, class TOrdinal>
[[nodiscard]] bool find_live_record(const TRecords& records, const TKey key, TOrdinal& ordinal) noexcept
{
    std::size_t begin = 0u, end = records.size();
    const auto wanted = key.query_value();
    while (begin < end)
    {
        const std::size_t middle = begin + ((end - begin) / 2u);
        const auto value = records[middle].entry.query_value();
        if (value < wanted)
        {
            begin = middle + 1u;
        }
        else if (value > wanted)
        {
            end = middle;
        }
        else
        {
            ordinal = static_cast<TOrdinal>(middle);
            return true;
        }
    }
    return false;
}

//  Baked records are immutable between loads. Four bytes per entry, with bounded
//  binary-search lookup and no allocation during any read.
class CBakedRecordIndex
{
public:
    void clear() noexcept { m_order.deallocate(); }

    template <class TRecords>
    [[nodiscard]] bool build(const TRecords& records) noexcept
    {
        TPodVector<std::uint32_t> order;
        if (records.size())
        {
            if ((records.size() >= k_missing_record) || !order.allocate(records.size()) || !order.set_size(records.size()))
            {
                return false;
            }
            for (std::size_t i = 0u; i < records.size(); ++i)
            {
                order[i] = static_cast<std::uint32_t>(i);
            }
            std::sort(order.data(), (order.data() + order.size()), [&](const std::uint32_t a, const std::uint32_t b) noexcept
                { return records[a].entry.query_value() < records[b].entry.query_value(); });
        }
        m_order = std::move(order);
        return true;
    }

    template <class TRecords, class TKey, class TOrdinal>
    [[nodiscard]] bool find(const TRecords& records, const TKey key, TOrdinal& ordinal) const noexcept
    {
        std::size_t begin = 0u, end = m_order.size();
        while (begin < end)
        {
            const std::size_t middle = begin + ((end - begin) / 2u);
            const std::uint32_t candidate = m_order[middle];
            const auto value = records[candidate].entry.query_value();
            if (value < key.query_value())
            {
                begin = middle + 1u;
            }
            else if (value > key.query_value())
            {
                end = middle;
            }
            else
            {
                ordinal = candidate;
                return true;
            }
        }
        return false;
    }

private:
    TPodVector<std::uint32_t> m_order;
};

}   // namespace schema

#endif // RECORD_INDEX_HPP_INCLUDED

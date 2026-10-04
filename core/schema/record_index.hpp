
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    record_index.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Live record lookup and baked ordinal indexes. Keys stay in their record tables.

//  Internal record lookup support. Baked-role headers include this to define
//  their private index storage; consumers should include the role header.

#pragma once

#ifndef RECORD_INDEX_HPP_INCLUDED
#define RECORD_INDEX_HPP_INCLUDED

#include "containers/TPodVector.hpp"

#include <algorithm>
#include <utility>

namespace schema
{

//==============================================================================
//  Live record lookup
//==============================================================================

//  Live roles append freshly allocated monotonic entry keys. Replacement keeps
//  the key, ordered erasure keeps the sequence, and promotion/reconciliation
//  create fresh keys in record order. Search that sequence without an index.
template<class TRecords, class TKey, class TOrdinal>
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

//==============================================================================
//  Baked record index
//==============================================================================

//  Baked records are immutable between loads. Four bytes per entry, with bounded
//  binary-search lookup and no allocation during any read.
class CBakedRecordIndex
{
public:
    void clear() noexcept { m_order.deallocate(); }

    template<class TRecords>
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

    template<class TRecords, class TKey, class TOrdinal>
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
    static constexpr std::uint32_t k_missing_record = UINT32_MAX;
    TPodVector<std::uint32_t> m_order;
};

}   // namespace schema

#endif // RECORD_INDEX_HPP_INCLUDED

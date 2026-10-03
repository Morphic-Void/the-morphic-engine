
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    type_compatibility.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Operation-local comparison of resolved type graphs.

//  Internal schema compatibility checks and their operation-local cache.
//  Included by role and remap implementations, not required for role queries.

#pragma once

#ifndef TYPE_COMPATIBILITY_HPP_INCLUDED
#define TYPE_COMPATIBILITY_HPP_INCLUDED

#include "schema/resolved_schema.hpp"
#include "containers/TPodVector.hpp"
#include "memory/memory_policies.hpp"

#include <algorithm>
#include <utility>

namespace schema
{

//==============================================================================
//  Compatibility cache storage
//==============================================================================

//  Compatibility-pair ordinals at at most half occupancy. Reserve may rehash;
//  insert/find/erase never allocate. No pointers into the pair table survive.
class COrdinalHashIndex
{
public:
    static constexpr std::uint32_t k_missing_record = UINT32_MAX;

    template<class THash>
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

    template<class THash>
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

    template<class TMatch>
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
    template<class THash>
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

//==============================================================================
//  Comparison policy and result
//==============================================================================

enum class ETypeMatch : std::uint8_t { representation, definition };
enum class ETypeMatchResult : std::uint8_t { match, mismatch, invalid_input, allocation_failed };

//==============================================================================
//  Type-graph comparison
//==============================================================================

//  Reuse within one synchronous operation against two unchanged resolved owners.
//  Only complete successful traversals and proven root mismatches are cached.
class CTypeCompatibility
{
public:
    CTypeCompatibility(const CResolvedSchema& source, const CResolvedSchema& destination, const ETypeMatch rule) noexcept
        : m_source(source), m_destination(destination), m_rule(rule) {}

    [[nodiscard]] ETypeMatchResult compare(const CSchemaIndex source, const CSchemaIndex destination) noexcept;
    [[nodiscard]] std::size_t cached_pair_count() const noexcept { return m_pairs.size(); }

private:
    struct SPair
    {
        CSchemaIndex source, destination, source_default, destination_default;
        ETypeMatchResult result{ ETypeMatchResult::match };
    };

    [[nodiscard]] static std::uint32_t hash(const SPair& pair) noexcept;
    [[nodiscard]] ETypeMatchResult add(const SPair pair) noexcept;
    [[nodiscard]] ETypeMatchResult inspect(const SPair pair) noexcept;
    [[nodiscard]] bool same_header(const SType& source, const SType& destination) const noexcept;

    const CResolvedSchema& m_source;
    const CResolvedSchema& m_destination;
    const ETypeMatch m_rule;
    TPodVector<SPair> m_pairs;
    COrdinalHashIndex m_index;
};

}   // namespace schema

#endif // TYPE_COMPATIBILITY_HPP_INCLUDED


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    type_compatibility.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Operation-local comparison of resolved type graphs.

#pragma once

#ifndef TYPE_COMPATIBILITY_HPP_INCLUDED
#define TYPE_COMPATIBILITY_HPP_INCLUDED

#include "schema/resolved_schema.hpp"
#include "schema/record_index.hpp"

namespace schema
{

enum class ETypeMatch : std::uint8_t { representation, definition };
enum class ETypeMatchResult : std::uint8_t { match, mismatch, invalid_input, allocation_failed };

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

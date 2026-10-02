
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_remap.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    02 Oct 26
//
//  Compatible direct-member remap plans and bounded current-view execution.

#pragma once

#ifndef DATA_REMAP_HPP_INCLUDED
#define DATA_REMAP_HPP_INCLUDED

#include "schema/resolved_schema.hpp"

namespace schema
{

enum class ERemapReason : std::uint8_t
{
    none = 0,
    invalid_input,
    unknown_type,
    invalid_range,
    overlap,
    allocation_failed
};

struct SRemapDiagnostic
{
    ERemapReason reason{ ERemapReason::none };
    std::size_t source{};
    CSchemaIndex source_member, destination_member;
};

struct SRemapSourceType
{
    const CResolvedSchema* schema{};
    CSchemaIndex type;
};

class CDataRemapPlan
{
public:
    CDataRemapPlan() noexcept = default;
    CDataRemapPlan(CDataRemapPlan&& source) noexcept;
    CDataRemapPlan& operator=(CDataRemapPlan&& source) noexcept;
    CDataRemapPlan(const CDataRemapPlan&) = delete;
    CDataRemapPlan& operator=(const CDataRemapPlan&) = delete;

    [[nodiscard]] bool initialise(const SRemapSourceType* const sources, const std::size_t source_count,
        const CResolvedSchema& destination_schema, const CSchemaIndex destination_type,
        SRemapDiagnostic& diagnostic) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_ready; }
    [[nodiscard]] std::size_t source_count() const noexcept { return m_sources.size(); }
    [[nodiscard]] std::size_t matched_member_count() const noexcept { return m_matched_members; }
    [[nodiscard]] std::size_t copy_range_count() const noexcept { return m_ranges.size(); }

    //  Copy the minimum number of complete records in the bounded views.
    //  Nonzero type views must contain an exact multiple of their type stride.
    //  Every view is checked before writing any destination byte.
    [[nodiscard]] bool execute(const CByteConstView* const sources, const std::size_t source_count, const CByteView destination) const noexcept;

private:
    enum class EKernel : std::uint8_t
    {
        contiguous = 0,
        contiguous_source,
        contiguous_destination,
        strided
    };
    struct SLayout
    {
        std::uint64_t size{}, stride{}, alignment{};
    };
    struct SRange
    {
        std::size_t source{};
        std::uint64_t source_offset{}, destination_offset{}, size{};
        EKernel kernel{ EKernel::strided };
        std::uint8_t fixed_size{};
    };

    TPodVector<SLayout> m_sources;
    TPodVector<SRange> m_ranges;
    SLayout m_destination;
    std::size_t m_matched_members{};
    bool m_ready{};
};

} // namespace schema

#endif // DATA_REMAP_HPP_INCLUDED

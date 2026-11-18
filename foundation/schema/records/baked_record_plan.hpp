
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_record_plan.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Internal structural plans for baked instance and bulk records.

#pragma once

#ifndef SCHEMA_BAKED_RECORD_PLAN_HPP_INCLUDED
#define SCHEMA_BAKED_RECORD_PLAN_HPP_INCLUDED

#include "schema/resolved_types.hpp"
#include "containers/TPodVector.hpp"

#include <cstddef>
#include <cstdint>

namespace schema::detail
{

struct SBakedInstanceRecord
{
    CBakedValueIndex entry, declaration;
    CTypesIndex type;
    std::uint32_t parent{ UINT32_MAX };
    std::uint32_t offset{}, extent{};
};

struct SBakedBulkRecord
{
    CBakedValueIndex entry;
    CTypesIndex type;
    std::uint32_t count{}, offset{}, extent{};
    std::uint32_t stride{};
};

struct SBakedInstancePlan
{
    TPodVector<SBakedInstanceRecord> records;
    std::size_t payload_size{};
};

struct SBakedBulkPlan
{
    TPodVector<SBakedBulkRecord> records;
    std::size_t payload_size{};
};

enum class EBakedRecordPlanReason : std::uint8_t
{
    none, invalid_input, missing_property, unknown_property, unknown_type,
    invalid_count, invalid_locator, invalid_range, overlap, invalid_declaration,
    allocation_failed
};

struct SBakedRecordPlanDiagnostic
{
    EBakedRecordPlanReason reason{};
    CBakedValueIndex occurrence;
};

//  Common load reasons map by meaning, independently of public enum ordinals.
//  The instance boundary handles invalid_declaration separately; bulk planning
//  does not produce that reason.
template<class TReason>
[[nodiscard]] static TReason baked_record_plan_reason(const EBakedRecordPlanReason reason) noexcept
{
    switch (reason)
    {
        case EBakedRecordPlanReason::none: return TReason::none;
        case EBakedRecordPlanReason::missing_property: return TReason::missing_property;
        case EBakedRecordPlanReason::unknown_property: return TReason::unknown_property;
        case EBakedRecordPlanReason::unknown_type: return TReason::unknown_type;
        case EBakedRecordPlanReason::invalid_count: return TReason::invalid_count;
        case EBakedRecordPlanReason::invalid_locator: return TReason::invalid_locator;
        case EBakedRecordPlanReason::invalid_range: return TReason::invalid_range;
        case EBakedRecordPlanReason::overlap: return TReason::overlap;
        case EBakedRecordPlanReason::allocation_failed: return TReason::allocation_failed;
        default: return TReason::invalid_input;
    }
}

//  Read structural metadata only: supplied_size is an extent, not payload proof.
//  Reset diagnostic on entry; replace output only on success, including an empty
//  plan for an absent/empty section. Failure preserves the previous output.
//  Records borrow identities from document/types. Both must outlive their use.
//  Scratch captures the current allocation context; moving a plan retains that
//  context, which must outlive its storage. No operation owner or quota is created.
[[nodiscard]] bool plan_baked_instances(const CBakedDocument& document, const CResolvedTypes& types,
    const std::size_t supplied_size, SBakedInstancePlan& output, SBakedRecordPlanDiagnostic& diagnostic) noexcept;
[[nodiscard]] bool plan_baked_bulk_data(const CBakedDocument& document, const CResolvedTypes& types,
    const std::size_t supplied_size, SBakedBulkPlan& output, SBakedRecordPlanDiagnostic& diagnostic) noexcept;

}   // namespace schema::detail

#endif // SCHEMA_BAKED_RECORD_PLAN_HPP_INCLUDED

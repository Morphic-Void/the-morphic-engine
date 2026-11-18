
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_reference_context.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private validated directory and bounded reference traversal for schema data.

#pragma once

#include "schema/data_block_view.hpp"
#include "schema/records/baked_record_plan.hpp"
#include "schema/values/reference_value.hpp"
#include "schema/types_wrappers.hpp"

namespace schema::detail
{

class CDataOperation;

[[nodiscard]] bool publish_data_block(const CBakedDataBlock& source, CDataOperation& operation,
    CBakedDataBlock& output) noexcept;

//  Sorted by ID; ordinal addresses the unchanged planner record order.
struct SDataCollectionLookup
{
    std::uint32_t id{}, ordinal{};
};

struct SDataReferenceSlot
{
    CTypesIndex type;
    std::uint32_t offset{}, depth{};
};

//  Visit all declared reference slots in one value, including fixed arrays.
//  Offset arithmetic stays wide until the resolved-layout bounds are checked.
using FReferenceSlot = bool (*)(void* const context, const SType& layout,
    const SDataReferenceSlot slot) noexcept;
[[nodiscard]] bool visit_reference_slots(const CResolvedTypes& types, const CTypesIndex type,
    const std::uint64_t offset, CDataOperation& operation, void* const context,
    const FReferenceSlot visit, const unsigned depth = 0u) noexcept;

//  Borrowed operation and its parent allocator must outlive this metadata.
//  Construct and load under the caller's CDataOperationScope; that scope need
//  only cover the bounded call, since retained owners capture their context.
struct SDataViewState
{
    explicit SDataViewState(CDataOperation& budget) noexcept : operation(budget) {}
    [[nodiscard]] bool load(const CBakedDataBlock& source, const bool inspect_source = true) noexcept;
    [[nodiscard]] bool load_bytes(const CByteConstView source, const bool inspect_source = true) noexcept;
    [[nodiscard]] bool references() noexcept;
    [[nodiscard]] bool instance_inheritance() noexcept;
    [[nodiscard]] bool text_id(const SType& type, const std::uint8_t* const bytes, SDataTextView& output) const noexcept;
    [[nodiscard]] bool span_id(const SType& type, const std::uint8_t* const bytes,
        std::uint32_t& collection, std::uint32_t& first, std::uint32_t& count, bool& absent) const noexcept;
    [[nodiscard]] std::uint32_t collection_index(const std::uint32_t id) const noexcept;
    static bool encode_default(void* const context, const SType& type, const SReferenceValue& value,
        std::uint8_t* const destination, EReason& reason) noexcept;

    CDataOperation& operation;
    CBakedTypes types;
    CBakedDocument document;
    CByteConstView file, instance_bytes, bulk_bytes;
    TPodVector<SBakedBulkRecord> collections;
    TPodVector<SBakedInstanceRecord> snapshots;
    TPodVector<SDataCollectionLookup> collection_lookup;
    TPodVector<std::uint32_t> retained_ids;
};

}   // namespace schema::detail

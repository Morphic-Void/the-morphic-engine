
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_bulk_plan.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Plan baked bulk records from structural metadata without binding an owner.

#include "schema/baked_record_plan.hpp"
#include "schema/data_block_format.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/data_storage_validation.hpp"

#include <utility>

namespace schema::detail
{

static bool fail_bulk_plan(SBakedRecordPlanDiagnostic& diagnostic, const EBakedRecordPlanReason reason,
    const CBakedValueIndex occurrence) noexcept
{
    diagnostic = { reason, occurrence };
    return false;
}

bool plan_baked_bulk_data(const CBakedDocument& document, const CResolvedTypes& types,
    const std::size_t supplied_size, SBakedBulkPlan& output, SBakedRecordPlanDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!document.is_ready() || !types.is_ready())
    {
        return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, {});
    }
    SDataBlockRoot metadata;
    SDataBlockDiagnostic root_error;
    const bool envelope = static_cast<bool>(document.object_child(document.root(), CStringView{ "fileType" }));
    if (!read_data_block_root(CDocumentRead{ document }, metadata, root_error, envelope))
    {
        return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, root_error.location.baked_value);
    }
    if (metadata.bulk_size && (metadata.bulk_bytes != supplied_size))
    {
        return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_range, metadata.bulk_size.baked);
    }
    if (!metadata.stripped)
    {
        return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, document.root());
    }
    SBakedBulkPlan staged;
    const CBakedValueIndex data_root = metadata.bulk_data.baked;
    std::uint64_t cursor{};
    for (CBakedValueIndex group = document.first_child(data_root); group.is_valid(); group = document.next_sibling(group))
    {
        if (!document.is_object_entry(group) || (document.value_type(group) != EBakedValueType::object))
        {
            return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, group);
        }
        const CTypesIndex type = types.find_type(document.name(group));
        SType layout;
        if (!type.is_valid() || !types.type(type, layout))
        {
            return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::unknown_type, group);
        }
        for (CBakedValueIndex entry = document.first_child(group); entry.is_valid(); entry = document.next_sibling(entry))
        {
            if (!document.is_object_entry(entry) || (document.value_type(entry) != EBakedValueType::object))
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, entry);
            }
            for (CBakedValueIndex property = document.first_child(entry); property.is_valid(); property = document.next_sibling(property))
            {
                const CStringView name = document.name(property);
                if (!data_entry_property(name, true, true))
                {
                    return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::unknown_property, property);
                }
            }
            const CBakedValueIndex locator = document.object_child(entry, CStringView{ "locator" });
            const CBakedValueIndex embedded = document.object_child(entry, CStringView{ "data" });
            if (!locator.is_valid() || (document.value_type(locator) != EBakedValueType::object))
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::missing_property, entry);
            }
            if (embedded.is_valid())
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_input, embedded);
            }
            SDataLocator parsed;
            SOccurrence failure;
            EBakedRecordPlanReason reason{};
            if (!read_data_locator(CDocumentRead{ document }, SOccurrence{ locator },
                true, layout.size, parsed, reason, failure))
            {
                return fail_bulk_plan(diagnostic, reason, failure.baked);
            }
            const CBakedValueIndex valid_node = parsed.valid_node.baked;
            const CBakedValueIndex count_node = parsed.count_node.baked;
            const CBakedValueIndex size_node = parsed.size_node.baked;
            const std::uint64_t raw_offset = parsed.offset;
            const std::uint64_t raw_count = parsed.count, raw_size = parsed.size;
            const bool prior_valid = parsed.valid;
            std::uint64_t count{};
            if (count_node.is_valid())
            {
                count = raw_count;
            }
            else if (size_node.is_valid() && (layout.size != 0u) && ((raw_size % layout.size) == 0u))
            {
                count = raw_size / layout.size;
            }
            std::uint64_t extent{};
            if (!data_extent(layout, count, extent))
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_count, locator);
            }
            if (size_node.is_valid() && (raw_size != extent))
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_range, size_node);
            }
            if (!prior_valid)
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::invalid_locator, valid_node);
            }
            const std::uint64_t offset = raw_offset;
            if (!check_data_range(offset, extent, layout.alignment, cursor, supplied_size, reason))
            {
                return fail_bulk_plan(diagnostic, reason, locator);
            }
            const std::uint64_t end = offset + extent;
            SBakedBulkRecord record;
            record.entry = entry;
            record.type = type;
            record.count = static_cast<std::uint32_t>(count);
            record.offset = static_cast<std::uint32_t>(offset);
            record.extent = static_cast<std::uint32_t>(extent);
            record.stride = static_cast<std::uint32_t>(layout.size);
            if (!staged.records.push_back(record))
            {
                return fail_bulk_plan(diagnostic, EBakedRecordPlanReason::allocation_failed, entry);
            }
            if ((extent != 0u) || (offset != 0u))
            {
                cursor = end;
            }
        }
    }
    staged.payload_size = static_cast<std::size_t>(cursor);
    output = std::move(staged);
    return true;
}

}   // namespace schema::detail

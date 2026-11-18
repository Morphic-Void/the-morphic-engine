
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    baked_instance_plan.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Plan baked instance structure and payload ranges before consumer validation.

#include "schema/baked_record_plan.hpp"
#include "schema/data_block_format.hpp"
#include "schema/data_document_validation.hpp"
#include "schema/data_storage_validation.hpp"
#include "schema/instance_selection.hpp"

#include <utility>

namespace schema::detail
{

struct SInstancePlanFrame
{
    CBakedValueIndex next;
    std::uint32_t parent{ UINT32_MAX };
};

static bool fail(SBakedRecordPlanDiagnostic& diagnostic, const EBakedRecordPlanReason reason,
    const CBakedValueIndex occurrence) noexcept
{
    diagnostic = { reason, occurrence };
    return false;
}

bool plan_baked_instances(const CBakedDocument& document, const CResolvedTypes& types,
    const std::size_t supplied_size, SBakedInstancePlan& output, SBakedRecordPlanDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!document.is_ready() || !types.is_ready())
    {
        return fail(diagnostic, EBakedRecordPlanReason::invalid_input, {});
    }
    SBakedInstancePlan staged;
    SDataBlockRoot metadata;
    SDataBlockDiagnostic root_error;
    const bool envelope = static_cast<bool>(document.object_child(document.root(), CStringView{ "fileType" }));
    if (!read_data_block_root(CDocumentRead{ document }, metadata, root_error, envelope))
    {
        return fail(diagnostic, EBakedRecordPlanReason::invalid_input, root_error.location.baked_value);
    }
    if (metadata.instance_size && (metadata.instance_bytes != supplied_size))
    {
        return fail(diagnostic, EBakedRecordPlanReason::invalid_range, metadata.instance_size.baked);
    }
    if (!metadata.stripped)
    {
        return fail(diagnostic, EBakedRecordPlanReason::invalid_input, document.root());
    }
    const CBakedValueIndex instances = metadata.instances.baked;
    if (!instances.is_valid())
    {
        output = std::move(staged);
        return true;
    }
    std::uint64_t cursor{};
    for (CBakedValueIndex group = document.first_child(instances); group.is_valid(); group = document.next_sibling(group))
    {
        if (!document.is_object_entry(group) || (document.value_type(group) != EBakedValueType::object))
        {
            return fail(diagnostic, EBakedRecordPlanReason::invalid_input, group);
        }
        const CTypesIndex type = types.find_type(document.name(group));
        SType layout;
        if (!type.is_valid() || !types.type(type, layout))
        {
            return fail(diagnostic, EBakedRecordPlanReason::unknown_type, group);
        }
        TPodVector<SInstancePlanFrame> frames;
        SInstancePlanFrame frame{ document.first_child(group), UINT32_MAX };
        while (frame.next.is_valid() || (frames.size() != 0u))
        {
            if (!frame.next.is_valid())
            {
                if (!frames.pop_back(frame))
                {
                    return fail(diagnostic, EBakedRecordPlanReason::invalid_input, group);
                }
                continue;
            }
            const CBakedValueIndex instance = frame.next;
            frame.next = document.next_sibling(instance);
            if (!document.is_object_entry(instance) || (document.value_type(instance) != EBakedValueType::object))
            {
                return fail(diagnostic, EBakedRecordPlanReason::invalid_input, instance);
            }
            for (CBakedValueIndex property = document.first_child(instance); property.is_valid(); property = document.next_sibling(property))
            {
                const CStringView name = document.name(property);
                if (!data_entry_property(name, false, true))
                {
                    return fail(diagnostic, EBakedRecordPlanReason::unknown_property, property);
                }
            }
            const CBakedValueIndex declaration = document.object_child(instance, CStringView{ "declaration" });
            const CBakedValueIndex specialisations = document.object_child(instance, CStringView{ "specialisation" });
            const CBakedValueIndex locator = document.object_child(instance, CStringView{ "locator" });
            SValueDiagnostic selection_error;
            const CDocumentRead selection_document{ document };
            if (!validate_selection(types, type, { &selection_document, SOccurrence{ declaration } }, selection_error))
            {
                return fail(diagnostic, value_failure(selection_error, EBakedRecordPlanReason::invalid_declaration),
                    (selection_error.occurrence ? selection_error.occurrence.baked : declaration));
            }
            if (specialisations.is_valid() && (document.value_type(specialisations) != EBakedValueType::object))
            {
                return fail(diagnostic, EBakedRecordPlanReason::invalid_input, specialisations);
            }
            if (!locator.is_valid() || (document.value_type(locator) != EBakedValueType::object))
            {
                return fail(diagnostic, EBakedRecordPlanReason::missing_property, instance);
            }
            SDataLocator parsed;
            SOccurrence failure;
            EBakedRecordPlanReason reason{};
            if (!read_data_locator(CDocumentRead{ document }, SOccurrence{ locator },
                false, layout.size, parsed, reason, failure))
            {
                return fail(diagnostic, reason, failure.baked);
            }
            const CBakedValueIndex valid_node = parsed.valid_node.baked;
            const std::uint64_t raw_offset = parsed.offset;
            const bool prior_valid = parsed.valid;
            if (!prior_valid)
            {
                return fail(diagnostic, EBakedRecordPlanReason::invalid_locator, valid_node);
            }
            const std::uint64_t offset = raw_offset;
            if (!check_data_range(offset, layout.size, layout.alignment, cursor, supplied_size, reason))
            {
                return fail(diagnostic, reason, locator);
            }
            const std::uint64_t end = offset + layout.size;
            if (staged.records.size() >= UINT32_MAX)
            {
                return fail(diagnostic, EBakedRecordPlanReason::invalid_range, instance);
            }
            SBakedInstanceRecord record;
            record.entry = instance;
            record.declaration = declaration;
            record.type = type;
            record.parent = frame.parent;
            record.offset = static_cast<std::uint32_t>(offset);
            record.extent = static_cast<std::uint32_t>(layout.size);
            if (!staged.records.push_back(record))
            {
                return fail(diagnostic, EBakedRecordPlanReason::allocation_failed, instance);
            }
            if ((layout.size != 0u) || (offset != 0u))
            {
                cursor = end;
            }
            const CBakedValueIndex child = document.first_child(specialisations);
            if (child.is_valid())
            {
                if (!frames.push_back(frame))
                {
                    return fail(diagnostic, EBakedRecordPlanReason::allocation_failed, specialisations);
                }
                frame = { child, static_cast<std::uint32_t>(staged.records.size() - 1u) };
            }
        }
    }
    staged.payload_size = static_cast<std::size_t>(cursor);
    output = std::move(staged);
    return true;
}

}   // namespace schema::detail

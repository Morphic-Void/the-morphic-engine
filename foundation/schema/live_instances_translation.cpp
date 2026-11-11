
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_translation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Instance promotion, selection copying and baked output.

#include "schema/live_instances.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/value_codec.hpp"
#include "schema/instance_selection.hpp"
#include "schema/data_reconstruction.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <utility>

namespace schema
{

//==============================================================================
//  Selection copying
//==============================================================================

CNodeKey CLiveInstances::output_declaration(CLiveDocument& target, const SRecord& record,
    const CResolvedTypes& types, EInstanceLoadReason& reason) const noexcept
{
    const detail::CDocumentRead document{ m_document };
    detail::SValueDiagnostic error;
    const auto result = detail::copy_selection(target, types, record.type,
        { &document, detail::SOccurrence{ record.declaration } }, CStringView{ "declaration" }, error);
    reason = error.reason == EReason::none ? EInstanceLoadReason::none :
        detail::value_failure(error, EInstanceLoadReason::invalid_declaration);
    return result;
}

//==============================================================================
//  Promotion
//==============================================================================

struct STranslatedInstanceGroup
{
    detail::SOccurrence entry;
    CNodeKey group;
    CTypesIndex type;
};

static void order_groups(TPodVector<STranslatedInstanceGroup>& groups) noexcept
{
    if (groups.size() > 1u)
    {
        std::sort(groups.data(), (groups.data() + groups.size()),
            [](const STranslatedInstanceGroup& a, const STranslatedInstanceGroup& b)
            {
                return a.entry.query_value() < b.entry.query_value();
            });
    }
}

bool CLiveInstances::promote_from(const CBakedInstances& source, SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedTypes* const types = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !types || source.m_binding.resolved() != types)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const CBakedValueIndex instances = source.m_document.object_child(source.m_document.root(), CStringView{ "instances" });
    const CNodeKey live_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    TPodVector<STranslatedInstanceGroup> groups;
    for (CBakedValueIndex group = source.m_document.first_child(instances); group; group = source.m_document.next_sibling(group))
    {
        const CStringView name = source.m_document.name(group);
        const CTypesIndex type = types->find_type(name);
        SType layout;
        if (!types->type(type, layout))
        {
            diagnostic.reason = EInstanceLoadReason::incompatible_schema;
            return false;
        }
        const CNodeKey translated = m_document.create_object(name);
        if (!document_translation::detail::CDocumentConstruction::append(m_document, live_instances, translated) ||
            !groups.push_back({ detail::SOccurrence{ group }, translated, type }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    order_groups(groups);
    std::size_t group_cursor{};
    if (!m_records.reserve(source.m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size() &&
        (!m_payload.allocate(source.m_payload.size(), k_max_alignment) || !m_payload.set_size(source.m_payload.size())))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size())
    {
        std::memcpy(m_payload.data(), source.m_payload.data(), source.m_payload.size());
    }
    for (std::size_t i = 0u; i < source.m_records.size(); ++i)
    {
        const CBakedInstances::SRecord& record = source.m_records[i];
        const CBakedValueIndex baked_group = source.m_document.parent((record.parent == CBakedInstances::k_no_parent) ?
            record.entry : source.m_records[record.parent].entry);
        CNodeKey group{};
        CTypesIndex type;
        if (record.parent == CBakedInstances::k_no_parent)
        {
            if (!find_near_record(groups, detail::SOccurrence{ baked_group }, group_cursor))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                return false;
            }
            group = groups[group_cursor].group;
            type = groups[group_cursor].type;
        }
        else
        {
            type = m_records[record.parent].type;
            group = m_document.object_child(m_records[record.parent].entry, CStringView{ "specialisation" });
            if (!group)
            {
                group = m_document.create_object(CStringView{ "specialisation" });
                if (!document_translation::detail::CDocumentConstruction::append(m_document, m_records[record.parent].entry, group))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
            }
        }
        const CNodeKey entry = m_document.create_object(source.m_document.name(record.entry));
        const CNodeKey locator = make_locator(m_document, record.offset, record.extent);
        SRecord promoted{ entry, {}, type, record.parent, record.offset, record.extent };
        const detail::CDocumentRead selection_source{ source.m_document };
        detail::SValueDiagnostic selection_error;
        promoted.declaration = detail::copy_selection(m_document, *types, type,
            { &selection_source, detail::SOccurrence{ record.declaration } }, CStringView{ "declaration" }, selection_error);
        if (selection_error.reason != EReason::none)
        {
            diagnostic.reason = detail::value_failure(selection_error, EInstanceLoadReason::invalid_declaration);
            return false;
        }
        if (!entry || !document_translation::detail::CDocumentConstruction::append(m_document, entry, locator) ||
            (promoted.declaration && !document_translation::detail::CDocumentConstruction::append(m_document, entry, promoted.declaration)) ||
            !document_translation::detail::CDocumentConstruction::append(m_document, group, entry) || !m_records.push_back(promoted))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        const auto children = source.m_document.object_child(record.entry, CStringView{ "specialisation" });
        if (children && !source.m_document.first_child(children) && !document_translation::detail::CDocumentConstruction::append(
            m_document, entry, m_document.create_object(CStringView{ "specialisation" })))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    return true;
}

bool CBakedInstances::promote(CLiveInstances& destination, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise_document(0u) || !staged.m_binding.bind(m_binding))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    staged.m_loaded = true;
    if (!staged.promote_from(*this, diagnostic)) return false;
    destination.take_from(staged);
    return true;
}

//==============================================================================
//  Output preparation and baking
//==============================================================================

struct SInstanceOutputRecord
{
    std::uint32_t source{}, offset{}, extent{};
};

struct SInstanceOutputFrame
{
    CNodeKey next, group;
    std::uint32_t parent{ UINT32_MAX };
};

bool CLiveInstances::prepare_output_to(CLiveDocument& document, CByteBuffer& payload,
    SInstanceDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic) const noexcept
{
    diagnostic = {};
    const CResolvedTypes* const source_schema = m_binding.resolved();
    if (!loaded_ready() || !source_schema || document.is_ready() || payload.is_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    detail::SValueDiagnostic validation;
    const auto checked = detail::check_selected_records(*source_schema, detail::CDocumentRead{ m_document }, m_records,
        m_payload.data(), [&](const std::size_t i) noexcept { return m_records[i].type; }, validation);
    if (checked != detail::EValueCheck::match)
    {
        diagnostic.reason = detail::value_failure(validation, checked == detail::EValueCheck::mismatch ?
            EInstanceLoadReason::inheritance_mismatch : EInstanceLoadReason::invalid_declaration);
        diagnostic.occurrence = detail::SInstanceHandleAccess::make(validation.occurrence);
        return false;
    }
    CLiveDocument staged_document;
    if (!staged_document.initialise() || !staged_document.set_root_type(ELiveValueType::object) ||
        !document_translation::detail::CDocumentConstruction::append(staged_document, staged_document.root(), staged_document.create_object(CStringView{ "instances" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey output_instances = staged_document.object_child(staged_document.root(), CStringView{ "instances" });
    if (
        !document_translation::detail::CDocumentConstruction::append(staged_document, staged_document.root(), staged_document.create_boolean(true, CStringView{ "stripped" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey source_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    TPodVector<SInstanceOutputRecord> output_records;
    std::uint32_t cursor{};
    for (CNodeKey source_group = m_document.first_child(source_instances); source_group; source_group = m_document.next_sibling(source_group))
    {
        const CStringView type_name = m_document.name(source_group);
        const CTypesIndex source_type = source_schema->find_type(type_name);
        SType layout;
        if (!source_schema->type(source_type, layout))
        {
            diagnostic.reason = EInstanceLoadReason::incompatible_schema;
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_group });
            return false;
        }
        const CNodeKey output_group = staged_document.create_object(type_name);
        if (!document_translation::detail::CDocumentConstruction::append(staged_document, output_instances, output_group))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        TPodVector<SInstanceOutputFrame> frames;
        SInstanceOutputFrame frame{ m_document.first_child(source_group), output_group, k_no_parent };
        while (frame.next || (frames.size() != 0u))
        {
            if (!frame.next)
            {
                if (!frames.pop_back(frame))
                {
                    diagnostic.reason = EInstanceLoadReason::invalid_input;
                    return false;
                }
                continue;
            }
            const CNodeKey source_entry = frame.next;
            frame.next = m_document.next_sibling(source_entry);
            const SRecord* record = nullptr;
            std::uint32_t record_index{};
            if (find_live_record(m_records, source_entry, record_index))
            {
                record = &m_records[record_index];
            }
            if (!record || (record->type != source_type) ||
                ((frame.parent == k_no_parent) ? (record->parent != k_no_parent) :
                    (record->parent != output_records[frame.parent].source)))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            if ((layout.size != record->extent) ||
                (layout.alignment == 0u) || (layout.alignment > k_max_alignment))
            {
                diagnostic.reason = EInstanceLoadReason::incompatible_schema;
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            std::uint32_t offset{};
            if (record->extent != 0u)
            {
                const auto alignment = static_cast<std::uint32_t>(layout.alignment);
                offset = (cursor + alignment - 1u) & ~(alignment - 1u);
                if ((offset > memory::k_byte_size_ceiling) ||
                    (record->extent > (memory::k_byte_size_ceiling - offset)))
                {
                    diagnostic.reason = EInstanceLoadReason::invalid_range;
                    diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                    return false;
                }
                cursor = offset + record->extent;
            }
            const CNodeKey output_entry = staged_document.create_object(m_document.name(source_entry));
            const CNodeKey locator = make_locator(staged_document, offset, record->extent);
            if (!output_entry || !document_translation::detail::CDocumentConstruction::append(staged_document, output_entry, locator))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            const CNodeKey declaration = output_declaration(staged_document, *record, *source_schema, diagnostic.reason);
            if (diagnostic.reason != EInstanceLoadReason::none)
            {
                diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source_entry });
                return false;
            }
            if (declaration && !document_translation::detail::CDocumentConstruction::append(staged_document, output_entry, declaration))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            if (!document_translation::detail::CDocumentConstruction::append(staged_document, frame.group, output_entry) ||
                !output_records.push_back({ record_index, offset, record->extent }))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            const CNodeKey source_children = m_document.object_child(source_entry, CStringView{ "specialisation" });
            if (source_children)
            {
                const CNodeKey output_children = staged_document.create_object(CStringView{ "specialisation" });
                if (!document_translation::detail::CDocumentConstruction::append(staged_document, output_entry, output_children) || !frames.push_back(frame))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
                frame = { m_document.first_child(source_children), output_children, static_cast<std::uint32_t>(output_records.size() - 1u) };
            }
        }
    }
    CByteBuffer staged_payload;
    std::uint8_t* payload_bytes{};
    if (block_diagnostic)
    {
        if (!detail::prepare_data_block_storage(staged_document, cursor, detail::EDataDomain::instances, staged_payload, payload_bytes, *block_diagnostic))
        {
            return false;
        }
    }
    else if ((cursor != 0u) && (!staged_payload.allocate(static_cast<std::size_t>(cursor), k_max_alignment) ||
        !staged_payload.set_size(static_cast<std::size_t>(cursor))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (!block_diagnostic) payload_bytes = staged_payload.data();
    std::size_t copied_end{};
    for (std::size_t i = 0u; i < output_records.size(); ++i)
    {
        const SInstanceOutputRecord& record = output_records[i];
        if (record.extent != 0u)
        {   //  Clear only newly created gaps; complete snapshots retain their bytes.
            if (record.offset > copied_end)
            {
                std::memset((payload_bytes + copied_end), 0, (record.offset - copied_end));
            }
            const SRecord& original = m_records[record.source];
            std::memcpy((payload_bytes + record.offset), (m_payload.data() + original.offset), record.extent);
            copied_end = static_cast<std::size_t>(record.offset) + record.extent;
        }
    }
    document = std::move(staged_document);
    payload = std::move(staged_payload);
    return true;
}

bool CLiveInstances::prepare_output(CLiveDocument& document, CByteBuffer& payload, SInstanceDiagnostic& diagnostic) const noexcept
{
    if (!loaded_ready())
    {
        diagnostic = { EInstanceLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, diagnostic);
}

bool CLiveInstances::bake(CBakedDataBlock& destination, SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return detail::bake_runtime_block(*this, destination, diagnostic,
        [&](CLiveDocument& document, CByteBuffer& storage) noexcept
        {
            return prepare_output_to(document, storage, diagnostic.role, &diagnostic.block);
        });
}

} // namespace schema

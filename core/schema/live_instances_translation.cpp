
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_translation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Instance promotion, binary reconciliation and baked output.

#include "schema/live_instances.hpp"
#include "schema/baked_data_block.hpp"
#include "schema/data_block_construction.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "schema/data_reconstruction.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_copy.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <utility>

namespace schema
{

//==============================================================================
//  Binary-authoritative declaration reconstruction
//==============================================================================

CNodeKey CLiveInstances::output_declaration(CLiveDocument& target, const SRecord& record,
    const CResolvedSchema& schema, const CSchemaIndex destination_type, EInstanceLoadReason& reason) const noexcept
{
    CByteBuffer defaults;
    const detail::CDocumentRead read{ target };
    detail::SValueDiagnostic error;
    const bool base = record.parent == k_no_parent;
    const std::uint8_t* baseline = nullptr;
    if (base)
    {
        if (record.extent && (!defaults.allocate(record.extent, k_max_alignment) || !defaults.set_size(record.extent)))
        {
            reason = EInstanceLoadReason::allocation_failed;
            return {};
        }
        if (!detail::construct_value(schema, read, destination_type, {}, defaults.data(), defaults.size(),
            detail::EConstructionMode::instance, error))
        {
            reason = detail::value_failure(error, EInstanceLoadReason::unrepresentable_value);
            return {};
        }
        baseline = defaults.data();
    }
    else
    {
        baseline = record.extent ? (m_payload.data() + m_records[record.parent].offset) : nullptr;
    }
    const std::uint8_t* const bytes = record.extent ? (m_payload.data() + record.offset) : nullptr;
    CNodeKey result;
    (void)detail::reconstruct_instance(target, schema, destination_type, bytes, baseline, record.extent, base, result, reason);
    return result;
}

//==============================================================================
//  Promotion
//==============================================================================

struct STranslatedInstanceGroup
{
    detail::SOccurrence entry;
    CNodeKey group;
    CSchemaIndex type;
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
    const CResolvedSchema* const original = source.m_binding.resolved();
    const CResolvedSchema* const target = m_binding.resolved();
    if (!source.loaded_ready() || !loaded_ready() || !original || !target)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CTypeCompatibility compatibility{ *original, *target, ETypeMatch::representation };
    const CBakedValueIndex instances = source.m_document.object_child(source.m_document.root(), CStringView{ "instances" });
    const CNodeKey live_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    TPodVector<STranslatedInstanceGroup> groups;
    for (CBakedValueIndex group = source.m_document.first_child(instances); group; group = source.m_document.next_sibling(group))
    {
        const CStringView name = source.m_document.name(group);
        const CSchemaIndex source_type = original->find_type(name);
        const CSchemaIndex destination_type = target->find_type(name);
        const auto match = compatibility.compare(source_type, destination_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EInstanceLoadReason::allocation_failed : EInstanceLoadReason::incompatible_schema;
            return false;
        }
        const CNodeKey translated = m_document.create_object(name);
        if (!document_translation::detail::CDocumentConstruction::append(m_document, live_instances, translated) ||
            !groups.push_back({ detail::SOccurrence{ group }, translated, destination_type }))
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
        CSchemaIndex type;
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
        SRecord promoted{ entry, {}, group, type, record.parent, record.offset, record.extent };
        promoted.declaration = output_declaration(m_document, promoted, *target, type, diagnostic.reason);
        if (diagnostic.reason != EInstanceLoadReason::none)
        {
            return false;
        }
        if (!entry || !document_translation::detail::CDocumentConstruction::append(m_document, entry, locator) ||
            (promoted.declaration && !document_translation::detail::CDocumentConstruction::append(m_document, entry, promoted.declaration)) ||
            !document_translation::detail::CDocumentConstruction::append(m_document, group, entry) || !m_records.push_back(promoted))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    return true;
}

bool CBakedInstances::promote(CLiveInstances& destination, CBakedSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (!staged.promote_from(*this, diagnostic))
    {
        diagnostic.occurrence = {};
        return false;
    }
    destination.take_from(staged);
    return true;
}

bool CBakedInstances::promote(CLiveInstances& destination, CLiveSchema& schema, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!loaded_ready() || destination.document_ready() || destination.m_binding.is_attached() || !schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveInstances staged;
    if (!staged.initialise(schema))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (!staged.promote_from(*this, diagnostic))
    {
        diagnostic.occurrence = {};
        return false;
    }
    destination.take_from(staged);
    return true;
}

//==============================================================================
//  Binary reconciliation
//==============================================================================

bool CLiveInstances::reconcile(SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveDocument document;
    TPodVector<SRecord> records;
    if (!document.initialise() || !document.set_root_type(ELiveValueType::object) ||
        !document_translation::detail::CDocumentConstruction::append(document, document.root(), document.create_object(CStringView{ "instances" })) ||
        !records.reserve(m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey root = document.object_child(document.root(), CStringView{ "instances" });
    const CNodeKey old_root = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    TPodVector<STranslatedInstanceGroup> groups;
    for (CNodeKey group = m_document.first_child(old_root); group; group = m_document.next_sibling(group))
    {
        const CNodeKey translated = document.create_object(m_document.name(group));
        if (!document_translation::detail::CDocumentConstruction::append(document, root, translated) || !groups.push_back({ detail::SOccurrence{ group }, translated, {} }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    order_groups(groups);
    std::size_t group_cursor{};
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        const SRecord& source = m_records[i];
        CNodeKey group;
        if (source.parent == k_no_parent)
        {
            if (!find_near_record(groups, detail::SOccurrence{ source.group }, group_cursor))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                return false;
            }
            group = groups[group_cursor].group;
        }
        else
        {
            const CNodeKey parent = records[source.parent].entry;
            group = document.object_child(parent, CStringView{ "specialisation" });
            if (!group)
            {
                group = document.create_object(CStringView{ "specialisation" });
                if (!document_translation::detail::CDocumentConstruction::append(document, parent, group))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
            }
        }
        const CNodeKey entry = document.create_object(m_document.name(source.entry));
        const CNodeKey locator = make_locator(document, source.offset, source.extent);
        const CNodeKey declaration = output_declaration(document, source, *schema, source.type, diagnostic.reason);
        if (diagnostic.reason != EInstanceLoadReason::none)
        {
            diagnostic.occurrence = detail::SInstanceHandleAccess::make(detail::SOccurrence{ source.entry });
            return false;
        }
        if (!entry || !document_translation::detail::CDocumentConstruction::append(document, entry, locator) || (declaration && !document_translation::detail::CDocumentConstruction::append(document, entry, declaration)) ||
            !document_translation::detail::CDocumentConstruction::append(document, group, entry) ||
            !records.push_back({ entry, declaration, group, source.type, source.parent, source.offset, source.extent }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    m_document = std::move(document);
    m_records = std::move(records);
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
    const CResolvedSchema& destination_schema, const EDataOutputForm form,
    SInstanceDiagnostic& diagnostic, SDataBlockDiagnostic* const block_diagnostic) const noexcept
{
    diagnostic = {};
    const CResolvedSchema* const source_schema = m_binding.resolved();
    if (!loaded_ready() || !source_schema || document.is_ready() || payload.is_ready() ||
        ((form != EDataOutputForm::embedded) && (form != EDataOutputForm::external) && (form != EDataOutputForm::stripped)))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
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
    if ((form == EDataOutputForm::stripped) &&
        !document_translation::detail::CDocumentConstruction::append(staged_document, staged_document.root(), staged_document.create_boolean(true, CStringView{ "stripped" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey source_instances = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    CTypeCompatibility compatibility{ *source_schema, destination_schema, ETypeMatch::representation };
    TPodVector<SInstanceOutputRecord> output_records;
    std::uint32_t cursor{};
    for (CNodeKey source_group = m_document.first_child(source_instances); source_group; source_group = m_document.next_sibling(source_group))
    {
        const CStringView type_name = m_document.name(source_group);
        const CSchemaIndex source_type = source_schema->find_type(type_name);
        const CSchemaIndex output_type = destination_schema.find_type(type_name);
        const auto match = compatibility.compare(source_type, output_type);
        if (match != ETypeMatchResult::match)
        {
            diagnostic.reason = (match == ETypeMatchResult::allocation_failed) ?
                EInstanceLoadReason::allocation_failed : EInstanceLoadReason::incompatible_schema;
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
            SType layout;
            if (!destination_schema.type(output_type, layout) || (layout.size != record->extent) ||
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
            const CNodeKey declaration = (form == EDataOutputForm::stripped) ? CNodeKey{} :
                output_declaration(staged_document, *record, destination_schema, output_type, diagnostic.reason);
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
            if (source_children && m_document.first_child(source_children))
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

bool CLiveInstances::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EInstanceLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

bool CLiveInstances::prepare_output(CLiveDocument& document, CByteBuffer& payload,
    CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    if (!destination_schema.resolved_ready())
    {
        diagnostic = { EInstanceLoadReason::invalid_input, {} };
        return false;
    }
    return prepare_output_to(document, payload, *destination_schema.resolved(), form, diagnostic);
}

template<class TSchema>
bool CLiveInstances::bake_to(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    TSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (block.is_ready() || payload.is_ready() || role.document_ready() ||
        role.m_binding.is_attached() || !destination_schema.resolved_ready())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    CLiveDocument prepared;
    CByteBuffer prepared_payload;
    if (!prepare_output(prepared, prepared_payload, destination_schema, form, diagnostic))
    {
        return false;
    }
    CBakedDocumentBlock prepared_block;
    if (!document_translation::bake(prepared, prepared_block))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    CBakedInstances prepared_role;
    if (!prepared_role.set_document(prepared_block.document()) ||
        !prepared_role.attach_schema(destination_schema, prepared_payload.const_view(), false, diagnostic))
    {
        if (diagnostic.reason == EInstanceLoadReason::none)
        {
            diagnostic.reason = EInstanceLoadReason::invalid_input;
        }
        diagnostic.occurrence = {};
        return false;
    }
    block = std::move(prepared_block);
    payload = std::move(prepared_payload);
    role.take_from(prepared_role);
    return true;
}

template<class TSchema>
bool CLiveInstances::bake_to(CBakedDataBlock& destination, TSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return detail::bake_data_block(*this, destination, schema, form, diagnostic,
        [&](CLiveDocument& document, CByteBuffer& storage) noexcept
        {
            return prepare_output_to(document, storage, *schema.resolved(), form, diagnostic.role, &diagnostic.block);
        });
}

bool CLiveInstances::bake(CBakedDataBlock& destination, CBakedSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return bake_to(destination, schema, form, diagnostic);
}

bool CLiveInstances::bake(CBakedDataBlock& destination, CLiveSchema& schema,
    const EDataOutputForm form, SInstancesBlockDiagnostic& diagnostic) const noexcept
{
    return bake_to(destination, schema, form, diagnostic);
}

bool CLiveInstances::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    CBakedSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

bool CLiveInstances::bake(CBakedDocumentBlock& block, CByteBuffer& payload, CBakedInstances& role,
    CLiveSchema& destination_schema, const EDataOutputForm form, SInstanceDiagnostic& diagnostic) const noexcept
{
    return bake_to(block, payload, role, destination_schema, form, diagnostic);
}

} // namespace schema

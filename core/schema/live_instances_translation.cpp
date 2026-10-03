
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_translation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Instance promotion, binary reconciliation and baked output.

#include "schema/live_instances.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_translation.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>
#include <utility>

namespace schema
{

constexpr unsigned k_max_depth = 256u;

//==============================================================================
//  Binary-authoritative declaration reconstruction
//==============================================================================

//  Compare meaningful encoded values; old declarations are not an input.
class CInstanceReconciler
{
public:
    CInstanceReconciler(CLiveDocument& document, const CResolvedSchema& schema,
        EInstanceLoadReason& reason) noexcept :
        m_document(document), m_schema(schema), m_reason(reason)
    {
    }

    [[nodiscard]] CNodeKey decode(const CSchemaIndex type, const std::uint8_t* const bytes,
        const std::uint8_t* const baseline, const CStringView name, const bool required = false,
        const unsigned depth = 0u) noexcept;

private:
    [[nodiscard]] CNodeKey decode_scalar(const CSchemaIndex type, const std::uint64_t bits,
        const unsigned width, const CStringView& name) noexcept;

    CLiveDocument& m_document;
    const CResolvedSchema& m_schema;
    EInstanceLoadReason& m_reason;
};

CNodeKey CInstanceReconciler::decode_scalar(const CSchemaIndex type, const std::uint64_t bits,
    const unsigned width, const CStringView& name) noexcept
{
    EScalarDecodeReason decoded;
    const CNodeKey value = decode_document_scalar(m_document, m_schema, type, bits, width, name, decoded);
    if (!value)
    {
        m_reason = (decoded == EScalarDecodeReason::allocation_failed) ?
            EInstanceLoadReason::allocation_failed : EInstanceLoadReason::unrepresentable_value;
    }
    return value;
}

CNodeKey CInstanceReconciler::decode(const CSchemaIndex type, const std::uint8_t* const bytes,
    const std::uint8_t* const baseline, const CStringView name, const bool required,
    const unsigned depth) noexcept
{
    SType layout;
    if ((depth >= k_max_depth) || !m_schema.type(type, layout))
    {
        m_reason = EInstanceLoadReason::unrepresentable_value;
        return {};
    }
    const std::size_t size = static_cast<std::size_t>(layout.size);
    const bool equal = detail::compare_encoded(m_schema, type, bytes, size, baseline, size);
    if (equal && !required)
    {
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        return decode_scalar(type, read_scalar_bits(bytes, size), static_cast<unsigned>(size * 8u), name);
    }
    const bool array = layout.category == ECategory::array;
    const CNodeKey result = array ? m_document.create_array(name) : m_document.create_object(name);
    if (!result)
    {
        m_reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    std::uint32_t count = equal ? 0u : layout.count;
    if (array && !equal)
    {
        SType element;
        (void)m_schema.type(layout.element_or_storage, element);
        while (count)
        {
            const std::size_t offset = static_cast<std::size_t>(layout.stride * (count - 1u));
            if (!detail::compare_encoded(m_schema, layout.element_or_storage,
                (bytes ? bytes + offset : nullptr), static_cast<std::size_t>(element.size),
                (baseline ? baseline + offset : nullptr), static_cast<std::size_t>(element.size)))
            {
                break;
            }
            --count;
        }
    }
    for (std::uint32_t i = 0u; (i < count) && (m_reason == EInstanceLoadReason::none); ++i)
    {
        CNodeKey child;
        if (layout.category == ECategory::bit_structure)
        {
            SField field;
            (void)m_schema.field(m_schema.field_at(type, i), field);
            const std::uint64_t actual = read_scalar_bits(bytes, size) & field.mask;
            if (actual != (read_scalar_bits(baseline, size) & field.mask))
            {
                child = decode_scalar(field.type, (actual >> field.shift), field.width, m_schema.name(field.name));
            }
        }
        else
        {
            CSchemaIndex child_type = layout.element_or_storage;
            std::size_t offset = static_cast<std::size_t>(layout.stride * i);
            CStringView child_name;
            if (!array)
            {
                SMember member;
                (void)m_schema.member(m_schema.member_at(type, i), member);
                child_type = member.type;
                offset = static_cast<std::size_t>(member.offset);
                child_name = m_schema.name(member.name);
            }
            child = decode(child_type,
                (bytes ? bytes + offset : nullptr), (baseline ? baseline + offset : nullptr),
                child_name, array, (depth + 1u));
        }
        if (child && !m_document.append_child(result, child).succeeded())
        {
            (void)m_document.erase(child);
            m_reason = EInstanceLoadReason::allocation_failed;
        }
    }
    if (m_reason != EInstanceLoadReason::none)
    {
        (void)m_document.erase(result);
        return {};
    }
    return result;
}

CNodeKey CLiveInstances::output_declaration(CLiveDocument& target, const SRecord& record,
    const CResolvedSchema& schema, const CSchemaIndex destination_type,
    EInstanceLoadReason& reason) const noexcept
{
    CByteBuffer defaults, reconstructed;
    if (record.extent && (!reconstructed.allocate(record.extent, 128u) || !reconstructed.set_size(record.extent)))
    {
        reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const detail::CDocumentRead read{ target };
    detail::SValueDiagnostic error;
    const bool base = record.parent == k_no_parent;
    const std::uint8_t* baseline = nullptr;
    if (base)
    {
        if (record.extent && (!defaults.allocate(record.extent, 128u) || !defaults.set_size(record.extent)))
        {
            reason = EInstanceLoadReason::allocation_failed;
            return {};
        }
        if (!detail::construct_value(schema, read, destination_type, {}, defaults.data(), defaults.size(),
            detail::EConstructionMode::instance, error))
        {
            reason = EInstanceLoadReason::unrepresentable_value;
            return {};
        }
        baseline = defaults.data();
    }
    else
    {
        baseline = record.extent ? (m_payload.data() + m_records[record.parent].offset) : nullptr;
    }
    const std::uint8_t* const bytes = record.extent ? (m_payload.data() + record.offset) : nullptr;
    CInstanceReconciler reconciler{ target, schema, reason };
    const CNodeKey result = reconciler.decode(destination_type, bytes, baseline, CStringView{ "declaration" });
    if (reason != EInstanceLoadReason::none)
    {
        return {};
    }
    //  Verify the generated declaration, including literals that cannot preserve raw encodings.
    const bool constructed = base ? detail::construct_value(schema, read, destination_type,
        detail::SOccurrence{ result }, reconstructed.data(), reconstructed.size(), detail::EConstructionMode::instance, error) :
        detail::construct_alternative(schema, read, destination_type, detail::SOccurrence{ result }, baseline,
            record.extent, reconstructed.data(), reconstructed.size(), error);
    if (!constructed || !detail::compare_encoded(schema, destination_type, bytes, record.extent,
        reconstructed.data(), reconstructed.size()))
    {
        if (result)
        {
            (void)target.erase(result);
        }
        reason = EInstanceLoadReason::unrepresentable_value;
        return {};
    }
    return result;
}

//==============================================================================
//  Promotion
//==============================================================================

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
        if (!append_node(m_document, live_instances, m_document.create_object(name)))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    if (!m_records.reserve(source.m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (source.m_payload.size() &&
        (!m_payload.allocate(source.m_payload.size(), 128u) || !m_payload.set_size(source.m_payload.size())))
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
        if (record.parent == CBakedInstances::k_no_parent)
        {
            group = m_document.object_child(live_instances, source.m_document.name(baked_group));
        }
        else
        {
            group = m_document.object_child(m_records[record.parent].entry, CStringView{ "specialisation" });
            if (!group)
            {
                group = m_document.create_object(CStringView{ "specialisation" });
                if (!append_node(m_document, m_records[record.parent].entry, group))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
            }
        }
        const CSchemaIndex type = (record.parent == CBakedInstances::k_no_parent) ?
            target->find_type(source.m_document.name(baked_group)) : m_records[record.parent].type;
        const CNodeKey entry = m_document.create_object(source.m_document.name(record.entry));
        const CNodeKey locator = make_locator(m_document, record.offset, record.extent);
        SRecord promoted{ entry, {}, group, type, record.parent, record.offset, record.extent };
        promoted.declaration = output_declaration(m_document, promoted, *target, type, diagnostic.reason);
        if (diagnostic.reason != EInstanceLoadReason::none)
        {
            return false;
        }
        if (!entry || !append_node(m_document, entry, locator) ||
            (promoted.declaration && !append_node(m_document, entry, promoted.declaration)) ||
            !append_node(m_document, group, entry) || !m_records.push_back(promoted))
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
        !append_node(document, document.root(), document.create_object(CStringView{ "instances" })) ||
        !records.reserve(m_records.size()))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey root = document.object_child(document.root(), CStringView{ "instances" });
    const CNodeKey old_root = detail::SInstanceHandleAccess::occurrence(instances_root()).live;
    for (CNodeKey group = m_document.first_child(old_root); group; group = m_document.next_sibling(group))
    {
        if (!append_node(document, root, document.create_object(m_document.name(group))))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
    }
    for (std::size_t i = 0u; i < m_records.size(); ++i)
    {
        const SRecord& source = m_records[i];
        CNodeKey group;
        if (source.parent == k_no_parent)
        {
            group = document.object_child(root, m_document.name(source.group));
        }
        else
        {
            const CNodeKey parent = records[source.parent].entry;
            group = document.object_child(parent, CStringView{ "specialisation" });
            if (!group)
            {
                group = document.create_object(CStringView{ "specialisation" });
                if (!append_node(document, parent, group))
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
        if (!entry || !append_node(document, entry, locator) || (declaration && !append_node(document, entry, declaration)) ||
            !append_node(document, group, entry) ||
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
    SInstanceDiagnostic& diagnostic) const noexcept
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
        !append_node(staged_document, staged_document.root(), staged_document.create_object(CStringView{ "instances" })))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const CNodeKey output_instances = staged_document.object_child(staged_document.root(), CStringView{ "instances" });
    if ((form == EDataOutputForm::stripped) &&
        !append_node(staged_document, staged_document.root(), staged_document.create_boolean(true, CStringView{ "stripped" })))
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
        if (!append_node(staged_document, output_instances, output_group))
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
                (layout.alignment == 0u) || (layout.alignment > 128u))
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
            if (!output_entry || !append_node(staged_document, output_entry, locator))
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
            if (declaration && !append_node(staged_document, output_entry, declaration))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            if (!append_node(staged_document, frame.group, output_entry) ||
                !output_records.push_back({ record_index, offset, record->extent }))
            {
                diagnostic.reason = EInstanceLoadReason::allocation_failed;
                return false;
            }
            const CNodeKey source_children = m_document.object_child(source_entry, CStringView{ "specialisation" });
            if (source_children && m_document.first_child(source_children))
            {
                const CNodeKey output_children = staged_document.create_object(CStringView{ "specialisation" });
                if (!append_node(staged_document, output_entry, output_children) || !frames.push_back(frame))
                {
                    diagnostic.reason = EInstanceLoadReason::allocation_failed;
                    return false;
                }
                frame = { m_document.first_child(source_children), output_children,
                    static_cast<std::uint32_t>(output_records.size() - 1u) };
            }
        }
    }
    CByteBuffer staged_payload;
    if ((cursor != 0u) && (!staged_payload.allocate(static_cast<std::size_t>(cursor), 128u) ||
        !staged_payload.set_size(static_cast<std::size_t>(cursor))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    for (std::size_t i = 0u; i < output_records.size(); ++i)
    {
        const SInstanceOutputRecord& record = output_records[i];
        if (record.extent != 0u)
        {
            const SRecord& original = m_records[record.source];
            std::memcpy((staged_payload.data() + record.offset), (m_payload.data() + original.offset), record.extent);
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
        !prepared_role.bind_schema(destination_schema) ||
        !prepared_role.load_supplied(prepared_payload.const_view(), false, diagnostic))
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

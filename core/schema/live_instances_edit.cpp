
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_edit.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Canonical capture and selected binary propagation for live instances.

#include "schema/live_instances.hpp"
#include "schema/value_codec.hpp"
#include "data_model/document_copy.hpp"
#include "debug/macros.hpp"
#include "memory/memory_policies.hpp"

#include <cstring>

namespace schema
{

//==============================================================================
//  Shared scalar decoding and selection lookup
//==============================================================================

constexpr unsigned k_max_depth = 256u;

[[nodiscard]] static detail::SOccurrence selected_child(const CResolvedSchema& schema,
    const detail::CDocumentRead& document, const CSchemaIndex type, const SType& layout,
    const detail::SOccurrence selection, const std::uint32_t ordinal,
    const bool singleton_element = false) noexcept
{
    if (!selection.is_valid())
    {
        return {};
    }
    if (singleton_element)
    {
        if (layout.category == ECategory::structure)
        {
            SMember member;
            return (schema.member(schema.member_at(type, ordinal), member) &&
                (schema.name(member.name) == document.name(selection))) ? selection : detail::SOccurrence{};
        }
        if (layout.category == ECategory::bit_structure)
        {
            SField field;
            return (schema.field(schema.field_at(type, ordinal), field) &&
                (schema.name(field.name) == document.name(selection))) ? selection : detail::SOccurrence{};
        }
        return {};
    }
    if ((ordinal == 0u) && detail::is_scalar_shorthand(schema, type, layout, document, selection))
    {
        return selection;
    }
    if (document.value_kind(selection) != EDocumentValueKind::object)
    {
        return {};
    }
    if (layout.category == ECategory::structure)
    {
        SMember member;
        return schema.member(schema.member_at(type, ordinal), member) ?
            document.object_child(selection, schema.name(member.name)) : detail::SOccurrence{};
    }
    if (layout.category == ECategory::bit_structure)
    {
        SField field;
        return schema.field(schema.field_at(type, ordinal), field) ?
            document.object_child(selection, schema.name(field.name)) : detail::SOccurrence{};
    }
    return {};
}

[[nodiscard]] static CNodeKey decode_scalar(CLiveDocument& target, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EInstanceLoadReason& error) noexcept
{
    EScalarDecodeReason decoded;
    const CNodeKey value = decode_document_scalar(target, schema, type, bits, width, name, decoded);
    if (!value)
    {
        error = (decoded == EScalarDecodeReason::allocation_failed) ?
            EInstanceLoadReason::allocation_failed : EInstanceLoadReason::invalid_declaration;
    }
    return value;
}

struct SResolvedSelectionStep
{
    EInstanceSelectionStepKind kind{};
    std::uint32_t ordinal{};
    CSchemaIndex child_type;
    CStringView name;
};

//==============================================================================
//  Selection decoding
//==============================================================================

//  Fixed inputs belong to one traversal; the recursive call carries only the
//  current value, naming, depth and discarded-selection branch.
class CInstanceSelectionDecoder
{
public:
    CInstanceSelectionDecoder(CLiveDocument& target, const CResolvedSchema& schema,
        const detail::CDocumentRead& document, const bool full, EInstanceLoadReason& error) noexcept :
        m_target(target), m_schema(schema), m_document(document), m_full(full), m_error(error) {}

    [[nodiscard]] CNodeKey decode(const CSchemaIndex type, const detail::SOccurrence selection,
        const std::uint8_t* const bytes, const CStringView& name, const bool singleton_element = false,
        const unsigned depth = 0u, const TPodVector<SResolvedSelectionStep>* const discarded_steps = nullptr) noexcept;

private:
    CLiveDocument& m_target;
    const CResolvedSchema& m_schema;
    const detail::CDocumentRead& m_document;
    const bool m_full;
    EInstanceLoadReason& m_error;
};

CNodeKey CInstanceSelectionDecoder::decode(const CSchemaIndex type, const detail::SOccurrence selection,
    const std::uint8_t* const bytes, const CStringView& name, const bool singleton_element,
    const unsigned depth, const TPodVector<SResolvedSelectionStep>* const discarded_steps) noexcept
{
    if (depth >= k_max_depth)
    {
        m_error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if (!m_full && !selection.is_valid())
    {
        return {};
    }

    //  This subtree is about to be replaced or removed. Preserve its selection
    //  shape without requiring its discarded binary values to have declarations.
    if (discarded_steps && (depth == discarded_steps->size()))
    {
        const CNodeKey copied = document_translation::copy_subtree(m_target, m_document, selection, name, depth);
        if (!copied)
        {
            m_error = EInstanceLoadReason::allocation_failed;
        }
        return copied;
    }
    SType layout;
    if (!m_schema.type(type, layout) || (layout.size && !bytes))
    {
        m_error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if (singleton_element && (layout.category != ECategory::structure) &&
        (layout.category != ECategory::bit_structure))
    {
        m_error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        const CNodeKey scalar = decode_scalar(m_target, m_schema, type,
            read_scalar_bits(bytes, static_cast<std::size_t>(layout.size)),
            static_cast<unsigned>(layout.size * 8u), name, m_error);
        if (!scalar && (m_error == EInstanceLoadReason::none))
        {
            m_error = EInstanceLoadReason::allocation_failed;
        }
        return scalar;
    }
    const EDocumentValueKind old_kind = m_full ? EDocumentValueKind::object : m_document.value_kind(selection);
    const bool shorthand = !m_full && !singleton_element && detail::is_scalar_shorthand(m_schema, type, layout, m_document, selection);
    if (!m_full)
    {
        if ((!singleton_element && (layout.category == ECategory::array) && (old_kind != EDocumentValueKind::array)) ||
            (!singleton_element && !shorthand && (layout.category != ECategory::array) &&
                (old_kind != EDocumentValueKind::object) && (old_kind != EDocumentValueKind::array)) ||
            (!singleton_element && (old_kind == EDocumentValueKind::array) &&
                (m_document.child_count(selection) > layout.count)))
        {
            m_error = EInstanceLoadReason::invalid_declaration;
            return {};
        }
        if (singleton_element && !
            ((layout.category == ECategory::structure) ? m_schema.find_member(type, m_document.name(selection)) :
                m_schema.find_field(type, m_document.name(selection))))
        {
            m_error = EInstanceLoadReason::invalid_declaration;
            return {};
        }
        if (!singleton_element && (old_kind == EDocumentValueKind::object))
        {
            for (detail::SOccurrence child = m_document.first_child(selection); child.is_valid();
                child = m_document.next_sibling(child))
            {
                if (!m_document.is_object_entry(child) ||
                    !(layout.category == ECategory::structure ?
                        m_schema.find_member(type, m_document.name(child)) :
                        m_schema.find_field(type, m_document.name(child))))
                {
                    m_error = EInstanceLoadReason::invalid_declaration;
                    return {};
                }
            }
        }
    }
    const bool positional = !m_full && !singleton_element && (old_kind == EDocumentValueKind::array);
    const CNodeKey result = ((layout.category == ECategory::array) || positional) ?
        m_target.create_array(name) : m_target.create_object(name);
    if (!result)
    {
        m_error = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const std::uint32_t count = m_full ? layout.count :
        (((layout.category == ECategory::array) || positional) ? m_document.child_count(selection) : layout.count);
    detail::SOccurrence cursor = (positional || (!m_full && (layout.category == ECategory::array))) ?
        m_document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child_selection = m_full ? detail::SOccurrence{} :
            (cursor.is_valid() ? cursor : selected_child(m_schema, m_document, type, layout, selection, i, singleton_element));
        if (cursor.is_valid())
        {
            cursor = m_document.next_sibling(cursor);
        }
        if (!m_full && !child_selection.is_valid())
        {
            continue;
        }
        CNodeKey child;
        const TPodVector<SResolvedSelectionStep>* const child_discarded =
            (discarded_steps && ((*discarded_steps)[depth].ordinal == i)) ? discarded_steps : nullptr;
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!m_schema.member(m_schema.member_at(type, i), member))
            {
                m_error = EInstanceLoadReason::invalid_declaration;
                break;
            }
            child = decode(member.type, child_selection,
                (bytes ? (bytes + member.offset) : nullptr),
                (positional ? CStringView{} : m_schema.name(member.name)),
                (positional && child_selection.is_valid() && m_document.is_object_entry(child_selection)), (depth + 1u), child_discarded);
        }
        else if (layout.category == ECategory::array)
        {
            child = decode(layout.element_or_storage, child_selection,
                (bytes ? (bytes + (layout.stride * i)) : nullptr), {},
                (child_selection.is_valid() && m_document.is_object_entry(child_selection)), (depth + 1u), child_discarded);
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!m_schema.field(m_schema.field_at(type, i), field))
            {
                m_error = EInstanceLoadReason::invalid_declaration;
                break;
            }
            const std::uint64_t word = read_scalar_bits(bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t raw = (word & field.mask) >> field.shift;
            const CStringView child_name = positional ? CStringView{} : m_schema.name(field.name);
            child = child_discarded ? document_translation::copy_subtree(m_target, m_document, child_selection, child_name, (depth + 1u)) :
                decode_scalar(m_target, m_schema, field.type, raw, field.width, child_name, m_error);
        }
        if (!child || !m_target.append_child(result, child).succeeded())
        {
            if (child && m_target.is_detached(child))
            {
                (void)m_target.erase(child);
            }
            (void)m_target.erase(result);
            if (m_error == EInstanceLoadReason::none)
            {
                m_error = EInstanceLoadReason::allocation_failed;
            }
            return {};
        }
    }
    if (m_error != EInstanceLoadReason::none)
    {
        (void)m_target.erase(result);
        return {};
    }
    return result;
}

//==============================================================================
//  Selected binary propagation
//==============================================================================

[[nodiscard]] static bool overlay_selection(const CResolvedSchema& schema, const CSchemaIndex type,
    const detail::CDocumentRead& document, const detail::SOccurrence selection,
    const std::uint8_t* const selected_bytes, std::uint8_t* const inherited_bytes,
    const bool singleton_element = false, const unsigned depth = 0u) noexcept
{
    if (!selection.is_valid())
    {
        return true;
    }
    if (depth >= k_max_depth)
    {
        return false;
    }
    SType layout;
    if (!schema.type(type, layout))
    {
        return false;
    }
    if (singleton_element && (layout.category != ECategory::structure) &&
        (layout.category != ECategory::bit_structure))
    {
        return false;
    }
    if (singleton_element && !
        ((layout.category == ECategory::structure) ? schema.find_member(type, document.name(selection)) :
            schema.find_field(type, document.name(selection))))
    {
        return false;
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        if (layout.size)
        {
            std::memcpy(inherited_bytes, selected_bytes, static_cast<std::size_t>(layout.size));
        }
        return true;
    }
    const std::uint32_t count = !singleton_element && ((layout.category == ECategory::array) ||
        (document.value_kind(selection) == EDocumentValueKind::array)) ? document.child_count(selection) : layout.count;
    detail::SOccurrence cursor = !singleton_element && (document.value_kind(selection) == EDocumentValueKind::array) ?
        document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child = cursor.is_valid() ? cursor :
            selected_child(schema, document, type, layout, selection, i, singleton_element);
        if (cursor.is_valid())
        {
            cursor = document.next_sibling(cursor);
        }
        if (!child.is_valid())
        {
            continue;
        }
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member) ||
                !overlay_selection(schema, member.type, document, child,
                    (selected_bytes ? (selected_bytes + member.offset) : nullptr),
                    (inherited_bytes ? (inherited_bytes + member.offset) : nullptr),
                    (!singleton_element && (document.value_kind(selection) == EDocumentValueKind::array) &&
                        document.is_object_entry(child)), (depth + 1u)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::array)
        {
            if (!overlay_selection(schema, layout.element_or_storage, document, child,
                (selected_bytes ? (selected_bytes + (layout.stride * i)) : nullptr),
                (inherited_bytes ? (inherited_bytes + (layout.stride * i)) : nullptr),
                document.is_object_entry(child), (depth + 1u)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                return false;
            }
            const std::uint64_t old_word = read_scalar_bits(selected_bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t new_word = read_scalar_bits(inherited_bytes, static_cast<std::size_t>(layout.size));
            write_scalar_bits(inherited_bytes, static_cast<std::size_t>(layout.size),
                ((new_word & ~field.mask) | (old_word & field.mask)));
        }
    }
    return true;
}

struct SStagedEdit
{
    std::uint32_t index{}, offset{};
    CNodeKey declaration;
};

[[nodiscard]] static bool overlay_except(const CResolvedSchema& schema, const CSchemaIndex type,
    const detail::CDocumentRead& document, const detail::SOccurrence selection,
    const std::uint8_t* const old_bytes, std::uint8_t* const new_bytes,
    const TPodVector<SResolvedSelectionStep>& path, const std::size_t depth,
    const bool singleton_element = false) noexcept
{
    if (!selection.is_valid())
    {
        return true;
    }
    if (depth >= path.size())
    {
        return overlay_selection(schema, type, document, selection, old_bytes, new_bytes, singleton_element);
    }
    SType layout;
    if (!schema.type(type, layout) || (layout.category == ECategory::primitive) ||
        (layout.category == ECategory::enumeration))
    {
        return false;
    }
    if (singleton_element && !
        ((layout.category == ECategory::structure) ? schema.find_member(type, document.name(selection)) :
            ((layout.category == ECategory::bit_structure) ? schema.find_field(type, document.name(selection)) :
                CSchemaIndex{})))
    {
        return false;
    }
    const bool positional = !singleton_element &&
        (document.value_kind(selection) == EDocumentValueKind::array);
    const std::uint32_t count = positional ? document.child_count(selection) : layout.count;
    detail::SOccurrence cursor = positional ? document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child = cursor.is_valid() ? cursor :
            selected_child(schema, document, type, layout, selection, i, singleton_element);
        if (cursor.is_valid())
        {
            cursor = document.next_sibling(cursor);
        }
        if (!child.is_valid())
        {
            continue;
        }
        if ((i == path[depth].ordinal) && ((depth + 1u) == path.size()))
        {
            continue;
        }
        const bool descend = (i == path[depth].ordinal);
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member))
            {
                return false;
            }
            const std::uint8_t* const source = old_bytes ? old_bytes + member.offset : nullptr;
            std::uint8_t* const destination = new_bytes ? new_bytes + member.offset : nullptr;
            const bool child_singleton = positional && document.is_object_entry(child);
            if (!(descend ? overlay_except(schema, member.type, document, child, source, destination,
                path, (depth + 1u), child_singleton) :
                overlay_selection(schema, member.type, document, child, source, destination, child_singleton)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::array)
        {
            const std::uint8_t* const source = old_bytes ? (old_bytes + (layout.stride * i)) : nullptr;
            std::uint8_t* const destination = new_bytes ? (new_bytes + (layout.stride * i)) : nullptr;
            const bool child_singleton = document.is_object_entry(child);
            if (!(descend ? overlay_except(schema, layout.element_or_storage, document, child, source, destination,
                path, (depth + 1u), child_singleton) :
                overlay_selection(schema, layout.element_or_storage, document, child, source, destination, child_singleton)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                return false;
            }
            const std::uint64_t before = read_scalar_bits(old_bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t after = read_scalar_bits(new_bytes, static_cast<std::size_t>(layout.size));
            write_scalar_bits(new_bytes, static_cast<std::size_t>(layout.size),
                ((after & ~field.mask) | (before & field.mask)));
        }
    }
    return true;
}

//==============================================================================
//  Selection paths and document edits
//==============================================================================

[[nodiscard]] static bool resolve_steps(const CResolvedSchema& schema, const CSchemaIndex root,
    const SInstanceSelectionStep* const steps, const std::size_t count,
    TPodVector<SResolvedSelectionStep>& resolved, EInstanceLoadReason& error) noexcept
{
    error = EInstanceLoadReason::invalid_declaration;
    CSchemaIndex current = root;
    for (std::size_t i = 0u; i < count; ++i)
    {
        SType layout;
        if (!schema.type(current, layout))
        {
            return false;
        }
        SResolvedSelectionStep step;
        step.kind = steps[i].kind;
        if ((layout.category == ECategory::array) && (steps[i].kind == EInstanceSelectionStepKind::element))
        {
            if (steps[i].element >= layout.count)
            {
                return false;
            }
            step.ordinal = steps[i].element;
            step.child_type = layout.element_or_storage;
        }
        else if ((layout.category == ECategory::structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CSchemaIndex member_index = schema.find_member(current, steps[i].member);
            SMember member;
            if (!member_index || !schema.member(member_index, member))
            {
                return false;
            }
            step.child_type = member.type;
            step.name = schema.name(member.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.member_at(current, ordinal) == member_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else if ((layout.category == ECategory::bit_structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CSchemaIndex field_index = schema.find_field(current, steps[i].member);
            SField field;
            if (!field_index || !schema.field(field_index, field))
            {
                return false;
            }
            step.child_type = field.type;
            step.name = schema.name(field.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.field_at(current, ordinal) == field_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else
        {
            return false;
        }
        if (!resolved.push_back(step))
        {
            error = EInstanceLoadReason::allocation_failed;
            return false;
        }
        current = step.child_type;
    }
    error = EInstanceLoadReason::none;
    return true;
}

[[nodiscard]] static CNodeKey new_container(CLiveDocument& document, const SType& layout, const CStringView& name) noexcept
{
    return layout.category == ECategory::array ? document.create_array(name) :
        (((layout.category == ECategory::structure) || (layout.category == ECategory::bit_structure)) ?
            document.create_object(name) : CNodeKey{});
}

[[nodiscard]] static CNodeKey child_at(CLiveDocument& document, const CNodeKey parent, const std::uint32_t ordinal) noexcept
{
    CNodeKey child = document.first_child(parent);
    for (std::uint32_t i = 0u; (i < ordinal) && child; ++i)
    {
        child = document.next_sibling(child);
    }
    return child;
}

[[nodiscard]] static CNodeKey convert_positional(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const CNodeKey positional) noexcept
{
    SType layout;
    if (!schema.type(type, layout) || ((layout.category != ECategory::structure) && (layout.category != ECategory::bit_structure)))
    {
        return {};
    }
    const CNodeKey named = document.create_object(document.name(positional));
    if (!named)
    {
        return {};
    }
    const detail::CDocumentRead query{ document };
    CNodeKey source_child = document.first_child(positional);
    for (std::uint32_t i = 0u; source_child; ++i)
    {
        CStringView name;
        if (i >= layout.count)
        {
            (void)document.erase(named);
            return {};
        }
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member))
            {
                (void)document.erase(named);
                return {};
            }
            name = schema.name(member.name);
        }
        else
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                (void)document.erase(named);
                return {};
            }
            name = schema.name(field.name);
        }
        const CNodeKey copied = document_translation::copy_subtree(document, query, detail::SOccurrence{ source_child }, name);
        if (!copied || !document.append_child(named, copied).succeeded())
        {
            if (copied && document.is_detached(copied))
            {
                (void)document.erase(copied);
            }
            (void)document.erase(named);
            return {};
        }
        source_child = document.next_sibling(source_child);
    }
    return named;
}

[[nodiscard]] static bool edit_selection_tree(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex root_type, CNodeKey& root, const TPodVector<SResolvedSelectionStep>& steps,
    CNodeKey replacement, const bool remove) noexcept
{
    if (steps.size() == 0u)
    {
        if (remove)
        {
            return false;
        }
        if (root)
        {
            (void)document.erase(root);
        }
        if (!document.set_name(replacement, CStringView{ "declaration" }))
        {
            return false;
        }
        root = replacement;
        return true;
    }
    if (!root)
    {
        if (remove)
        {
            return false;
        }
        SType type;
        if (!schema.type(root_type, type))
        {
            return false;
        }
        root = new_container(document, type, CStringView{ "declaration" });
        if (!root)
        {
            return false;
        }
    }
    CNodeKey current = root;
    CSchemaIndex current_type = root_type;
    for (std::size_t depth = 0u; depth < steps.size(); ++depth)
    {
        const SResolvedSelectionStep& step = steps[depth];
        SType type;
        if (!schema.type(current_type, type))
        {
            return false;
        }
        if ((step.kind == EInstanceSelectionStepKind::member) &&
            (document.value_type(current) == ELiveValueType::array))
        {
            const CNodeKey converted = convert_positional(document, schema, current_type, current);
            if (!converted)
            {
                return false;
            }
            const CNodeKey old_payload = document.detach_payload(current);
            if (!old_payload)
            {
                (void)document.erase(converted);
                return false;
            }
            if (!document.set_name(converted, CStringView{}) ||
                !document.attach_payload(current, converted).is_valid())
            {
                (void)document.attach_payload(current, old_payload);
                if (document.contains(converted) && document.is_detached(converted))
                {
                    (void)document.erase(converted);
                }
                return false;
            }
            (void)document.erase(old_payload);
        }
        const bool array = step.kind == EInstanceSelectionStepKind::element;
        if (array ? (document.value_type(current) != ELiveValueType::array) :
            (document.value_type(current) != ELiveValueType::object))
        {
            return false;
        }
        const std::uint32_t count = document.child_count(current);
        if (array && (step.ordinal > count))
        {
            return false;
        }
        CNodeKey child = array ? child_at(document, current, step.ordinal) :
            document.object_child(current, step.name);
        const bool last = depth + 1u == steps.size();
        if (last)
        {
            if (remove)
            {
                if (!child || (array && ((step.ordinal + 1u) != count)))
                {
                    return false;
                }
                return document.erase(child);
            }
            if (child)
            {
                const CNodeKey old_payload = document.detach_payload(child);
                if (!old_payload)
                {
                    return false;
                }
                if (!document.set_name(replacement, CStringView{}) ||
                    !document.attach_payload(child, replacement).is_valid())
                {
                    (void)document.attach_payload(child, old_payload);
                    return false;
                }
                (void)document.erase(old_payload);
            }
            else
            {
                if (!array && !document.set_name(replacement, step.name))
                {
                    return false;
                }
                if (!document.append_child(current, replacement).succeeded())
                {
                    return false;
                }
            }
            return true;
        }
        if (!child)
        {
            if (remove)
            {
                return false;
            }
            SType child_layout;
            if (!schema.type(step.child_type, child_layout))
            {
                return false;
            }
            child = new_container(document, child_layout, (array ? CStringView{} : step.name));
            if (!child || !document.append_child(current, child).succeeded())
            {
                if (child && document.is_detached(child))
                {
                    (void)document.erase(child);
                }
                return false;
            }
        }
        current = child;
        current_type = step.child_type;
    }
    return false;
}

//==============================================================================
//  Staged edit publication
//==============================================================================

bool CLiveInstances::edit_existing(const std::uint32_t index, const CNodeKey selection,
    const CByteConstView& complete, const bool full_declaration,
    SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || (index >= m_records.size()) ||
        (m_records[index].extent && (!complete.is_ready() || (complete.size() < m_records[index].extent))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    TPodVector<SStagedEdit> changes;
    TPodVector<std::uint32_t> staged_indices;
    const std::size_t candidates = m_records.size() - index;
    if (!staged_indices.allocate(candidates) || !staged_indices.set_size(candidates))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    std::fill_n(staged_indices.data(), candidates, k_no_parent);
    std::uint32_t total{};
    for (std::uint32_t i = index; i < m_records.size(); ++i)
    {
        const std::uint32_t parent = m_records[i].parent;
        if ((i != index) && ((parent == k_no_parent) || (parent < index) ||
            (staged_indices[parent - index] == k_no_parent)))
        {
            continue;
        }
        staged_indices[i - index] = static_cast<std::uint32_t>(changes.size());
        total = (total + 127u) & ~std::uint32_t{ 127u };
        if ((total > memory::k_byte_size_ceiling) ||
            (m_records[i].extent > memory::k_byte_size_ceiling - total) ||
            !changes.push_back({ i, total, {} }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        total += m_records[i].extent;
    }
    CByteBuffer staged;
    if (total && (!staged.allocate(static_cast<std::size_t>(total), 128u) ||
        !staged.set_size(static_cast<std::size_t>(total))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const detail::CDocumentRead document{ m_document };
    const auto cleanup = [&]() noexcept
    {
        for (std::size_t i = 0u; i < changes.size(); ++i)
        {
            const CNodeKey node = changes[i].declaration;
            if (node && m_document.contains(node) && m_document.is_detached(node))
            {
                (void)m_document.erase(node);
            }
        }
    };
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        SStagedEdit& change = changes[j];
        const SRecord& record = m_records[change.index];
        std::uint8_t* const target = record.extent ? (staged.data() + change.offset) : nullptr;
        if (change.index == index)
        {
            if (record.extent)
            {
                std::memcpy(target, complete.data(), record.extent);
            }
        }
        else
        {
            const SStagedEdit* const parent = &changes[staged_indices[record.parent - index]];
            if (record.extent)
            {
                std::memcpy(target, (staged.data() + parent->offset), record.extent);
            }
            if (!overlay_selection(*schema, record.type, document,
                detail::SOccurrence{ record.declaration },
                (record.extent ? (m_payload.data() + record.offset) : nullptr), target))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_declaration;
                cleanup();
                return false;
            }
        }
        const CNodeKey selected = change.index == index ? selection : record.declaration;
        EInstanceLoadReason error{ EInstanceLoadReason::none };
        CInstanceSelectionDecoder decoder{ m_document, *schema, document,
            ((change.index == index) && full_declaration), error };
        change.declaration = decoder.decode(record.type, detail::SOccurrence{ selected },
            target, CStringView{ "declaration" });
        if (error != EInstanceLoadReason::none)
        {
            diagnostic.reason = error;
            cleanup();
            return false;
        }
    }
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        const SStagedEdit& change = changes[j];
        SRecord& record = m_records[change.index];
        const CNodeKey old = record.declaration;
        if ((old && !m_document.detach(old)) ||
            (change.declaration && !m_document.append_child(record.entry, change.declaration).succeeded()))
        {
            disable();
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            cleanup();
            return false;
        }
        record.declaration = change.declaration;
        if (old)
        {
            (void)m_document.erase(old);
        }
    }
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        const SStagedEdit& change = changes[j];
        const SRecord& record = m_records[change.index];
        if (record.extent)
        {
            std::memcpy((m_payload.data() + record.offset), (staged.data() + change.offset), record.extent);
        }
    }
    return true;
}

//==============================================================================
//  Capture
//==============================================================================

CInstanceHandle CLiveInstances::capture_base(const CStringView& type, const CStringView& name,
    const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || type.empty() || name.empty())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    CByteBuffer type_storage, name_storage;
    CStringView stable_type, stable_name;
    if (!document_translation::stabilise_document_name(type, type_storage, stable_type) ||
        !document_translation::stabilise_document_name(name, name_storage, stable_name))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(stable_type);
    SType layout;
    if (!type_index || !schema->type(type_index, layout))
    {
        diagnostic.reason = EInstanceLoadReason::unknown_type;
        return {};
    }
    if (layout.size && (!complete.is_ready() || (complete.size() < layout.size) ||
        (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return {};
    }
    const CInstanceHandle existing = find_base(stable_type, stable_name);
    std::uint32_t index{};
    if (record_index(existing, index))
    {
        return edit_existing(index, {}, complete, true, diagnostic) ? existing : CInstanceHandle{};
    }
    EInstanceLoadReason error{ EInstanceLoadReason::none };
    const detail::CDocumentRead document{ m_document };
    CInstanceSelectionDecoder decoder{ m_document, *schema, document, true, error };
    const CNodeKey declaration = decoder.decode(type_index, {}, complete.data(), CStringView{ "declaration" });
    if (error != EInstanceLoadReason::none)
    {
        diagnostic.reason = error;
        return {};
    }
    const CInstanceHandle result = append_instance(type_index, k_no_parent, stable_type, stable_name, document,
        detail::SOccurrence{ declaration }, complete, diagnostic);
    if (declaration)
    {
        (void)m_document.erase(declaration);
    }
    return result;
}

bool CLiveInstances::capture_specialisation(const CInstanceHandle instance,
    const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const SRecord& record = m_records[index];
    SType layout;
    if (!m_binding.resolved()->type(record.type, layout) ||
        (record.extent && (!complete.is_ready() || (complete.size() < record.extent) ||
         (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u)))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return false;
    }
    CByteBuffer captured;
    if (record.extent && (!captured.allocate(record.extent, 128u) || !captured.set_size(record.extent)))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (record.extent)
    {
        std::memcpy(captured.data(), (m_payload.data() + record.offset), record.extent);
    }
    const detail::CDocumentRead document{ m_document };
    if (!overlay_selection(*m_binding.resolved(), record.type, document,
        detail::SOccurrence{ record.declaration }, complete.data(), captured.data()))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    return edit_existing(index, record.declaration, captured.const_view(), false, diagnostic);
}

//==============================================================================
//  Selection editing
//==============================================================================

bool CLiveInstances::set_selection(const CInstanceHandle instance,
    const SInstanceSelectionStep* const steps, const std::size_t step_count,
    const CInstanceDocumentQuery& source, const CInstanceHandle value,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    const detail::SOccurrence source_value = detail::SInstanceHandleAccess::occurrence(value);
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent) ||
        (step_count && !steps) || !source.m_query.contains(source_value))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    const SRecord& record = m_records[index];
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(*schema, record.type, steps, step_count, resolved, diagnostic.reason))
    {
        return false;
    }
    const detail::CDocumentRead local{ m_document };
    CNodeKey replacement = document_translation::copy_subtree(m_document, source.m_query, source_value,
        (step_count ? CStringView{} : CStringView{ "declaration" }));
    EInstanceLoadReason decode_error{ EInstanceLoadReason::none };
    CInstanceSelectionDecoder decoder{ m_document, *schema, local, false, decode_error };
    CNodeKey staged = (step_count && record.declaration) ? decoder.decode(record.type,
        detail::SOccurrence{ record.declaration }, (record.extent ? (m_payload.data() + record.offset) : nullptr),
        CStringView{ "declaration" }, false, 0u, &resolved) : CNodeKey{};
    if (!replacement || (step_count && record.declaration && !staged))
    {
        if (replacement)
        {
            (void)m_document.erase(replacement);
        }
        if (staged)
        {
            (void)m_document.erase(staged);
        }
        diagnostic.reason = decode_error == EInstanceLoadReason::none ?
            EInstanceLoadReason::allocation_failed : decode_error;
        return false;
    }
    if (!edit_selection_tree(m_document, *schema, record.type, staged, resolved, replacement, false))
    {
        if (staged)
        {
            (void)m_document.erase(staged);
        }
        if (replacement && m_document.contains(replacement) && m_document.is_detached(replacement))
        {
            (void)m_document.erase(replacement);
        }
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    CByteBuffer encoded;
    if (record.extent && (!encoded.allocate(record.extent, 128u) || !encoded.set_size(record.extent)))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const SRecord& parent = m_records[record.parent];
    detail::SValueDiagnostic error;
    const bool constructed = detail::construct_alternative(*schema, local, record.type,
        detail::SOccurrence{ staged }, (parent.extent ? (m_payload.data() + parent.offset) : nullptr),
        parent.extent, encoded.data(), encoded.size(), error);
    if (!constructed)
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    if (step_count && !overlay_except(*schema, record.type, local,
        detail::SOccurrence{ record.declaration },
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        encoded.data(), resolved, 0u))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const bool applied = edit_existing(index, staged, encoded.const_view(), false, diagnostic);
    (void)m_document.erase(staged);
    return applied;
}

bool CLiveInstances::remove_selection(const CInstanceHandle instance,
    const SInstanceSelectionStep* const steps, const std::size_t step_count,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent) ||
        !step_count || !steps || !m_records[index].declaration)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    const SRecord& record = m_records[index];
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(*schema, record.type, steps, step_count, resolved, diagnostic.reason))
    {
        return false;
    }
    const detail::CDocumentRead local{ m_document };
    EInstanceLoadReason decode_error{ EInstanceLoadReason::none };
    CInstanceSelectionDecoder decoder{ m_document, *schema, local, false, decode_error };
    CNodeKey staged = decoder.decode(record.type, detail::SOccurrence{ record.declaration },
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        CStringView{ "declaration" }, false, 0u, &resolved);
    if (!staged)
    {
        diagnostic.reason = decode_error == EInstanceLoadReason::none ?
            EInstanceLoadReason::allocation_failed : decode_error;
        return false;
    }
    if (!edit_selection_tree(m_document, *schema, record.type, staged, resolved, {}, true))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    CByteBuffer encoded;
    if (record.extent && (!encoded.allocate(record.extent, 128u) || !encoded.set_size(record.extent)))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const SRecord& parent = m_records[record.parent];
    detail::SValueDiagnostic error;
    if (!detail::construct_alternative(*schema, local, record.type,
        detail::SOccurrence{ staged }, (parent.extent ? (m_payload.data() + parent.offset) : nullptr),
        parent.extent, encoded.data(), encoded.size(), error))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    if (!overlay_except(*schema, record.type, local,
        detail::SOccurrence{ record.declaration },
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        encoded.data(), resolved, 0u))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const bool applied = edit_existing(index, staged, encoded.const_view(), false, diagnostic);
    (void)m_document.erase(staged);
    return applied;
}

} // namespace schema

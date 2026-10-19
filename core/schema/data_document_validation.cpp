
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_document_validation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Prepared data admission without payload construction.

#include "schema/data_document_validation.hpp"
#include "schema/value_codec.hpp"
#include "memory/memory_policies.hpp"

namespace schema::detail
{

bool data_entry_property(const CStringView& name, const bool bulk, const bool stored) noexcept
{
    return (stored && (name == CStringView{ "locator" })) || (bulk ? (name == CStringView{ "data" }) :
        ((name == CStringView{ "declaration" }) || (name == CStringView{ "specialisation" })));
}

bool align_data_offset(const std::uint64_t position, const std::uint64_t alignment, std::uint64_t& result) noexcept
{
    if ((alignment == 0u) || ((alignment & (alignment - 1u)) != 0u) || (position > (UINT64_MAX - (alignment - 1u))))
    {
        return false;
    }
    result = (position + alignment - 1u) & ~(alignment - 1u);
    return result <= memory::k_byte_size_ceiling;
}

bool data_extent(const SType& layout, const std::uint64_t count, std::uint64_t& extent) noexcept
{
    if ((count == 0u) || (count > UINT32_MAX) ||
        ((layout.size != 0u) && (count > (memory::k_byte_size_ceiling / layout.size))))
    {
        return false;
    }
    extent = layout.size * count;
    return true;
}

[[nodiscard]] static bool fail(SDataValidationDiagnostic& diagnostic, const EDataValidationReason reason,
    const SOccurrence occurrence) noexcept
{
    diagnostic = { reason, occurrence };
    return false;
}

[[nodiscard]] static bool validate_value(const CDocumentRead& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const SOccurrence value, const SOccurrence entry, const bool bulk,
    SDataValidationDiagnostic& diagnostic) noexcept
{
    SValueDiagnostic error;
    if (validate_declaration(schema, document, type, value,
        (bulk ? EConstructionMode::complete_bulk_data : EConstructionMode::instance), error,
        (bulk && document.is_object_entry(value))))
    {
        return true;
    }
    const EDataValidationReason reason = (error.reason == EReason::storage_limit) ? EDataValidationReason::storage_limit :
        ((error.reason == EReason::allocation_failed) ? EDataValidationReason::allocation_failed : EDataValidationReason::invalid_value);
    return fail(diagnostic, reason, (error.occurrence ? error.occurrence : entry));
}

bool validate_data_document(const CDocumentRead& document, const CResolvedSchema& schema,
    const bool bulk, SDataValidationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const SOccurrence root = document.root();
    if (!document.is_ready() || (document.value_kind(root) != EDocumentValueKind::object))
    {
        return fail(diagnostic, EDataValidationReason::invalid_input, root);
    }
    const CStringView section_name{ bulk ? "data" : "instances" };
    for (SOccurrence property = document.first_child(root); property; property = document.next_sibling(property))
    {
        if (!(document.name(property) == section_name))
        {
            return fail(diagnostic, EDataValidationReason::unknown_property, property);
        }
    }
    const SOccurrence section = document.object_child(root, section_name);
    if (!section)
    {
        return true;
    }
    if (document.value_kind(section) != EDocumentValueKind::object)
    {
        return fail(diagnostic, EDataValidationReason::invalid_input, section);
    }

    //  Only pending siblings are stacked, so memory follows hierarchy depth,
    //  not breadth. Group type lookup is reused through every descendant.
    TPodVector<SOccurrence> parents;
    std::uint64_t cursor{}, entries{};
    for (SOccurrence group = document.first_child(section); group; group = document.next_sibling(group))
    {
        if (!document.is_object_entry(group) || (document.value_kind(group) != EDocumentValueKind::object))
        {
            return fail(diagnostic, EDataValidationReason::invalid_input, group);
        }
        const CSchemaIndex type = schema.find_type(document.name(group));
        SType layout;
        if (!schema.type(type, layout))
        {
            return fail(diagnostic, EDataValidationReason::unknown_type, group);
        }
        SOccurrence next = document.first_child(group);
        while (next || parents.size())
        {
            if (!next)
            {
                (void)parents.pop_back(next);
                continue;
            }
            const SOccurrence entry = next;
            next = document.next_sibling(entry);
            if (!document.is_object_entry(entry) || (document.value_kind(entry) != EDocumentValueKind::object))
            {
                return fail(diagnostic, EDataValidationReason::invalid_input, entry);
            }
            for (SOccurrence property = document.first_child(entry); property; property = document.next_sibling(property))
            {
                if (!data_entry_property(document.name(property), bulk, false))
                {
                    return fail(diagnostic, EDataValidationReason::unknown_property, property);
                }
            }
            const SOccurrence declaration = document.object_child(entry, CStringView{ bulk ? "data" : "declaration" });
            const SOccurrence children = bulk ? SOccurrence{} : document.object_child(entry, CStringView{ "specialisation" });
            if (children && (document.value_kind(children) != EDocumentValueKind::object))
            {
                return fail(diagnostic, EDataValidationReason::invalid_input, children);
            }
            if (bulk && !declaration)
            {
                return fail(diagnostic, EDataValidationReason::missing_property, entry);
            }
            if (bulk && (document.value_kind(declaration) != EDocumentValueKind::array))
            {
                return fail(diagnostic, EDataValidationReason::invalid_input, declaration);
            }
            const std::uint64_t count = bulk ? document.child_count(declaration) : 1u;
            std::uint64_t extent{}, offset{};
            if (count == 0u)
            {
                return fail(diagnostic, EDataValidationReason::invalid_count, declaration);
            }
            if (!data_extent(layout, count, extent) || !align_data_offset(cursor, layout.alignment, offset) ||
                (extent > (memory::k_byte_size_ceiling - offset)) || (entries >= UINT32_MAX))
            {
                return fail(diagnostic, EDataValidationReason::storage_limit, entry);
            }
            ++entries;
            cursor = offset + extent;
            if (bulk)
            {
                for (SOccurrence value = document.first_child(declaration); value; value = document.next_sibling(value))
                {
                    if (document.is_object_entry(value) &&
                        !((layout.category == ECategory::structure) ? schema.find_member(type, document.name(value)) :
                            ((layout.category == ECategory::bit_structure) ? schema.find_field(type, document.name(value)) : CSchemaIndex{})))
                    {
                        return fail(diagnostic, EDataValidationReason::invalid_input, value);
                    }
                    if (!validate_value(document, schema, type, value, entry, true, diagnostic))
                    {
                        return false;
                    }
                }
            }
            else if (!validate_value(document, schema, type, declaration, entry, false, diagnostic))
            {
                return false;
            }
            const SOccurrence child = document.first_child(children);
            if (child)
            {
                if (parents.size() >= (document_limits::k_max_recursion_depth - 1u))
                {
                    return fail(diagnostic, EDataValidationReason::storage_limit, children);
                }
                if (!parents.push_back(next))
                {
                    return fail(diagnostic, EDataValidationReason::allocation_failed, children);
                }
                next = child;
            }
        }
    }
    return true;
}

}   // namespace schema::detail

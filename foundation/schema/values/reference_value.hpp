
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reference_value.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Logical reference grammar shared by type defaults and typed value writers.

#pragma once

#include "schema/resolved_types.hpp"

namespace schema
{

namespace detail
{

//  Inspect only physical member type slots. Arbitrary decoration/default data
//  may also contain properties named "kind" without declaring a reference.
[[nodiscard]] inline bool has_reference_declarations(const CDocumentRead& document) noexcept
{
    const SOccurrence types = document.object_child(document.root(), CStringView{ "types" });
    const SOccurrence structures = document.object_child(types, CStringView{ "structures" });
    for (SOccurrence structure = document.first_child(structures); structure; structure = document.next_sibling(structure))
    {
        const SOccurrence members = document.object_child(structure, CStringView{ "members" });
        for (SOccurrence member = document.first_child(members); member; member = document.next_sibling(member))
        {
            SOccurrence type = document.object_child(member, CStringView{ "type" });
            while (document.value_kind(type) == EDocumentValueKind::object)
            {
                const CStringView kind = document.string_value(document.object_child(type, CStringView{ "kind" }));
                if ((kind == CStringView{ "text" }) || (kind == CStringView{ "span" }))
                {
                    return true;
                }
                type = document.object_child(type, CStringView{ "element" });
            }
        }
    }
    return false;
}

struct SReferenceValue
{
    CStringView text, collection;
    std::uint32_t first{}, count{};
    bool absent{}, targeted{};
};

//  Explicit synchronous destination context, supplied only by schema-aware
//  construction. Ordinary raw value writers have no reference authority.
struct SReferenceEncoding
{
    void* context{};
    bool (*encode)(void* const context, const SType& type, const SReferenceValue& value,
        std::uint8_t* const destination, EReason& reason) noexcept{};
};

struct SReferenceDecoding
{
    void* context{};
    CNodeKey (*decode)(void* const context, CLiveDocument& document, const CTypesIndex type,
        const std::uint8_t* const bytes, const CStringView& name, EReason& reason) noexcept{};
    const SReferenceEncoding* encoding{};
};

[[nodiscard]] inline bool reference_unsigned(const CDocumentRead& document, const SOccurrence value,
    std::uint64_t& output) noexcept
{
    if (document.unsigned_integer_value(value, output))
    {
        return true;
    }
    std::int64_t number{};
    if (!document.signed_integer_value(value, number) || (number < 0))
    {
        return false;
    }
    output = static_cast<std::uint64_t>(number);
    return true;
}

[[nodiscard]] inline bool reference_marker(const CDocumentRead& document, const SOccurrence value,
    const char* const name) noexcept
{
    bool marker{};
    return (document.value_kind(value) == EDocumentValueKind::object) && (document.child_count(value) == 1u) &&
        document.boolean_value(document.object_child(value, CStringView{ name }), marker) && marker;
}

//  Validate the logical descriptor only. Target existence/type/bounds require
//  the owning collection context and are checked even for targeted empty spans.
[[nodiscard]] inline bool read_reference_value(const CDocumentRead& document, const SOccurrence value,
    const ECategory category, const bool nullable, SReferenceValue& output, const bool named_marker = false) noexcept
{
    SReferenceValue result;
    const bool named_element = named_marker && document.is_object_entry(value) &&
        (document.value_kind(document.parent(value)) == EDocumentValueKind::array);
    if (named_element)
    {
        //  The document parser stores a single-property object in an array as
        //  a named entry. Recognise only the two atomic reference markers.
        bool marker{};
        if (!document.boolean_value(value, marker) || !marker)
        {
            return false;
        }
        if (nullable && (document.name(value) == CStringView{ "absent" })) result.absent = true;
        else if ((category != ECategory::span) || !(document.name(value) == CStringView{ "empty" }))
        {
            return false;
        }
    }
    else if ((category == ECategory::text) && (document.value_kind(value) == EDocumentValueKind::string))
    {
        result.text = document.string_value(value);
    }
    else if (nullable && reference_marker(document, value, "absent"))
    {
        result.absent = true;
    }
    else if ((category == ECategory::span) && reference_marker(document, value, "empty"))
    {
        //  The default descriptor is the canonical unbound present-empty range.
    }
    else if ((category == ECategory::span) && (document.value_kind(value) == EDocumentValueKind::object) &&
        (document.child_count(value) == 3u))
    {
        const SOccurrence collection = document.object_child(value, CStringView{ "collection" });
        const SOccurrence first = document.object_child(value, CStringView{ "first" });
        const SOccurrence count = document.object_child(value, CStringView{ "count" });
        std::uint64_t start{}, length{};
        const bool has_start = reference_unsigned(document, first, start);
        const bool has_length = reference_unsigned(document, count, length);
        if ((document.value_kind(collection) != EDocumentValueKind::string) || !has_start || !has_length ||
            (start > INT32_MAX) || (length > UINT64_C(0x80000000)))
        {
            return false;
        }
        result.collection = document.string_value(collection);
        if (!result.collection.length()) return false;
        result.first = static_cast<std::uint32_t>(start);
        result.count = static_cast<std::uint32_t>(length);
        result.targeted = true;
    }
    else return false;
    output = result;
    return true;
}

}   // namespace detail
}   // namespace schema

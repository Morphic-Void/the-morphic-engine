
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Copy document values without interpreting schema declarations.

#include "schema/document_copy.hpp"
#include "data_model/live_document.hpp"

#include <cstring>

namespace schema
{

[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept
{
    if (source.empty())
    {
        return false;
    }
    if (source.length() == 0u)
    {
        copied = CStringView{ "" };
        return true;
    }
    if (!storage.allocate(source.length(), 1u) || !storage.set_size(source.length()))
    {
        return false;
    }
    std::memcpy(storage.data(), source.string(), source.length());
    copied = CStringView{ storage.data(), source.length() };
    return true;
}

[[nodiscard]] CNodeKey copy_document_value(CLiveDocument& target, const detail::CDocumentRead& source,
    const detail::SOccurrence value, const CStringView& name, const unsigned depth) noexcept
{
    if ((depth >= 256u) || !source.contains(value))
    {
        return {};
    }
    CNodeKey copied;
    switch (source.value_kind(value))
    {
        case EDocumentValueKind::empty: copied = target.create_empty(name); break;
        case EDocumentValueKind::null_value: copied = target.create_null(name); break;
        case EDocumentValueKind::boolean:
        {
            bool v{};
            if (!source.boolean_value(value, v))
            {
                return {};
            }
            copied = target.create_boolean(v, name);
            break;
        }
        case EDocumentValueKind::integer:
        {
            CIntegerMetadata metadata;
            if (!source.integer_metadata(value, metadata))
            {
                return {};
            }
            std::int64_t signed_value{};
            std::uint64_t unsigned_value{};
            copied = source.signed_integer_value(value, signed_value) ?
                target.create_signed_integer(signed_value, metadata, name) :
                (source.unsigned_integer_value(value, unsigned_value) ?
                    target.create_unsigned_integer(unsigned_value, metadata, name) : CNodeKey{});
            break;
        }
        case EDocumentValueKind::floating_point:
        {
            double v{};
            if (!source.floating_point_value(value, v))
            {
                return {};
            }
            copied = target.create_floating_point(v, name);
            break;
        }
        case EDocumentValueKind::string:
            copied = target.create_string(source.string_value(value), name);
            break;
        case EDocumentValueKind::array: copied = target.create_array(name); break;
        case EDocumentValueKind::object: copied = target.create_object(name); break;
        default: return {};
    }
    if (!copied)
    {
        return {};
    }
    if (source.suppresses_newline_escaping(value) &&
        !target.set_newline_escaping_suppressed(copied, true))
    {
        (void)target.erase(copied);
        return {};
    }
    for (detail::SOccurrence child = source.first_child(value); child.is_valid(); child = source.next_sibling(child))
    {
        const CNodeKey item = copy_document_value(target, source, child,
            (source.is_object_entry(child) ? source.name(child) : CStringView{}), (depth + 1u));
        if (!item || !target.append_child(copied, item).succeeded())
        {
            if (item && target.is_detached(item))
            {
                (void)target.erase(item);
            }
            (void)target.erase(copied);
            return {};
        }
    }
    return copied;
}

}   // namespace schema

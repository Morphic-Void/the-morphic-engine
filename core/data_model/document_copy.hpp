
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Value construction and bounded subtree copying into live documents.

#pragma once

#ifndef DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED
#define DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED

#include "data_model/live_document.hpp"
#include "data_model/baked_document_format.hpp"
#include "containers/ByteBuffers.hpp"

#include <type_traits>

namespace document_translation
{

//  Stabilise caller-supplied names before an operation can grow their document.
[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept;

namespace detail
{

//  Native documents expose value_type; representation-neutral readers expose
//  value_kind. Resolve this distinction at compile time, without a runtime adapter.
template<class TSource, class TValue>
[[nodiscard]] auto copy_value_kind(const TSource& source, const TValue value) noexcept -> decltype(source.value_kind(value))
{
    return source.value_kind(value);
}

template<class TSource, class TValue>
[[nodiscard]] auto copy_value_kind(const TSource& source, const TValue value) noexcept -> decltype(source.value_type(value))
{
    return source.value_type(value);
}

//  Create one detached value, leaving container children to the caller's walk.
//  Integer metadata and string formatting are shared by promotion and copying;
//  the supplied name determines entry identity, independently of the source name.
template<class TSource, class TValue>
[[nodiscard]] CNodeKey create_value_copy(CLiveDocument& target, const TSource& source,
    const TValue value, const CStringView& name) noexcept
{
    using TValueKind = decltype(copy_value_kind(source, value));
    const TValueKind kind = copy_value_kind(source, value);
    if constexpr (!std::is_same_v<TValueKind, EBakedValueType>)
    {
        if (kind == TValueKind::empty)
        {
            return target.create_empty(name);
        }
    }
    switch (kind)
    {
        case TValueKind::null_value: return target.create_null(name);
        case TValueKind::boolean:
        {
            bool result{};
            return source.boolean_value(value, result) ? target.create_boolean(result, name) : CNodeKey{};
        }
        case TValueKind::integer:
        {
            CIntegerMetadata metadata;
            if (!source.integer_metadata(value, metadata))
            {
                return {};
            }
            if (metadata.domain == EIntegerDomain::unsigned_value)
            {
                std::uint64_t result{};
                return source.unsigned_integer_value(value, result) ?
                    target.create_unsigned_integer(result, metadata, name) : CNodeKey{};
            }
            std::int64_t result{};
            return source.signed_integer_value(value, result) ?
                target.create_signed_integer(result, metadata, name) : CNodeKey{};
        }
        case TValueKind::floating_point:
        {
            double result{};
            return source.floating_point_value(value, result) ? target.create_floating_point(result, name) : CNodeKey{};
        }
        case TValueKind::string:
        {
            const CNodeKey copied = target.create_string(source.string_value(value), name);
            if (copied && source.suppresses_newline_escaping(value) &&
                !target.set_newline_escaping_suppressed(copied, true))
            {
                (void)target.erase(copied);
                return {};
            }
            return copied;
        }
        case TValueKind::array: return target.create_array(name);
        case TValueKind::object: return target.create_object(name);
        default: return {};
    }
}

}   // namespace detail

//  Sources provide the native document queries (or equivalent read-adapter
//  queries) with a matching value identity. Copy a detached subtree, including
//  within the same live document. Erase partial nodes on failure. Depth is shared
//  with the caller's enclosing traversal and is bounded at 256.
template<class TSource, class TValue>
[[nodiscard]] CNodeKey copy_subtree(CLiveDocument& target, const TSource& source,
    const TValue value, const CStringView& name, const unsigned depth = 0u) noexcept
{
    if ((depth >= 256u) || !source.contains(value))
    {
        return {};
    }
    const CNodeKey copied = detail::create_value_copy(target, source, value, name);
    if (!copied)
    {
        return {};
    }
    for (TValue child = source.first_child(value); child.is_valid(); child = source.next_sibling(child))
    {
        const CNodeKey item = copy_subtree(target, source, child,
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

}   // namespace document_translation

#endif // DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED

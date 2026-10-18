
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_copy.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Value construction and bounded subtree copying into live documents.

//  Public subtree-copy helper. Templates require live construction definitions;
//  the detail helpers are shared internally with document promotion.

#pragma once

#ifndef DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED
#define DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED

#include "data_model/live_document.hpp"
#include "data_model/baked_document_format.hpp"
#include "containers/ByteBuffers.hpp"

#include <type_traits>

namespace document_translation
{

//==============================================================================
//  Name stabilisation
//==============================================================================

//  Stabilise caller-supplied names before an operation can grow their document.
[[nodiscard]] bool stabilise_document_name(const CStringView& source, CByteBuffer& storage, CStringView& copied) noexcept;

//==============================================================================
//  Internal value construction
//==============================================================================

namespace detail
{

//  Internal construction only: callers establish unique child names and disjoint
//  freshly built subtrees. Public editing and arbitrary reader adapters stay checked.
class CDocumentConstruction
{
public:
    [[nodiscard]] static bool append(CLiveDocument& target, const CNodeKey parent, const CNodeKey child) noexcept;
    [[nodiscard]] static CPropertyNameId name(CLiveDocument& target, const CStringView& text) noexcept;
    [[nodiscard]] static CPropertyNameId import_name(CLiveDocument& target, const CStringView& text) noexcept;
    [[nodiscard]] static CStringValueId import_string(CLiveDocument& target, const CStringView& text) noexcept;
    [[nodiscard]] static CNodeKey create(CLiveDocument& target, const ELiveValueType kind,
        const std::uint64_t bits, const CIntegerMetadata metadata, const CPropertyNameId name,
        const bool object_entry) noexcept;
};

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

struct SCopiedValue
{
    ELiveValueType kind{ ELiveValueType::invalid };
    std::uint64_t bits{};
    CIntegerMetadata metadata;
};

//  One scalar reader for checked adapters and native copies. Integer intent and
//  finite-float admission remain identical to the public live constructors.
template<class TSource, class TValue>
[[nodiscard]] bool read_copied_value(const TSource& source, const TValue value, SCopiedValue& copied) noexcept
{
    using TKind = decltype(copy_value_kind(source, value));
    const TKind kind = copy_value_kind(source, value);
    if constexpr (!std::is_same_v<TKind, EBakedValueType>)
    {
        if (kind == TKind::empty) copied.kind = ELiveValueType::empty;
    }
    switch (kind)
    {
        case TKind::null_value:
        {
            copied.kind = ELiveValueType::null_value;
            break;
        }
        case TKind::boolean:
        {
            bool decoded{};
            if (!source.boolean_value(value, decoded))
            {
                return false;
            }
            copied.bits = decoded ? 1u : 0u;
            copied.kind = ELiveValueType::boolean;
            break;
        }
        case TKind::integer:
        {
            if (!source.integer_metadata(value, copied.metadata))
            {
                return false;
            }
            if (copied.metadata.domain == EIntegerDomain::unsigned_value)
            {
                if (!source.unsigned_integer_value(value, copied.bits) ||
                    !live_integer_metadata_matches_unsigned(copied.bits, copied.metadata))
                {
                    return false;
                }
            }
            else
            {
                std::int64_t decoded{};
                if (!source.signed_integer_value(value, decoded) ||
                    !live_integer_metadata_matches_signed(decoded, copied.metadata))
                {
                    return false;
                }
                copied.bits = live_signed_integer_bits(decoded);
            }
            copied.kind = ELiveValueType::integer;
            break;
        }
        case TKind::floating_point:
        {
            double decoded{};
            if (!source.floating_point_value(value, decoded) || !live_floating_point_is_finite(decoded))
            {
                return false;
            }
            copied.bits = live_floating_point_bits(decoded);
            copied.kind = ELiveValueType::floating_point;
            break;
        }
        case TKind::string:
        {
            copied.kind = ELiveValueType::string;
            break;
        }
        case TKind::array:
        {
            copied.kind = ELiveValueType::array;
            break;
        }
        case TKind::object:
        {
            copied.kind = ELiveValueType::object;
            break;
        }
        default:
        {
            break;
        }
    }
    return copied.kind != ELiveValueType::invalid;
}

//  Create one detached value, leaving container children to the caller's walk.
//  Arbitrary readers supply text through the ordinary checked admission path.
template<class TSource, class TValue>
[[nodiscard]] CNodeKey create_value_copy(CLiveDocument& target, const TSource& source, const TValue value, const CStringView& name) noexcept
{
    SCopiedValue copied;
    if (!target.is_ready() || !read_copied_value(source, value, copied))
    {
        return {};
    }
    const CNodeKey result = (copied.kind == ELiveValueType::string) ? target.create_string(source.string_value(value), name) :
        CDocumentConstruction::create(target, copied.kind, copied.bits, copied.metadata,
            CDocumentConstruction::name(target, name), !name.empty());
    if (result && (copied.kind == ELiveValueType::string) && source.suppresses_newline_escaping(value))
    {
        (void)target.set_newline_escaping_suppressed(result, true);
    }
    return result;
}

//  Native copying reuses same-document IDs; other sources are interned once per
//  distinct ID. The operation-local maps contain only encountered IDs.
template<class TSource>
class CNativeValueCopier
{
public:
    CNativeValueCopier(CLiveDocument& target, const TSource& source) noexcept : m_target(target), m_source(source)
    {
        if constexpr (std::is_same_v<TSource, CLiveDocument>)
        {
            m_shared_ids = &target == &source;
        }
    }

    //  Whole baked promotion has an empty destination. Rank-order admission has
    //  no sorted-index shifts, and gives both documents the same domain IDs.
    [[nodiscard]] bool preload_strings() noexcept
    {
        for (std::uint32_t rank = 1u; rank <= m_source.property_name_count(); ++rank)
        {
            const auto id = m_source.property_name_id_at_rank(rank);
            if (CDocumentConstruction::import_name(m_target, m_source.property_name(id)) != id)
            {
                return false;
            }
        }
        for (std::uint32_t rank = 1u; rank <= m_source.string_value_count(); ++rank)
        {
            const auto id = m_source.string_value_id_at_rank(rank);
            if (CDocumentConstruction::import_string(m_target, m_source.string_value(id)) != id)
            {
                return false;
            }
        }
        m_shared_ids = true;
        return true;
    }

    template<class TValue>
    [[nodiscard]] CNodeKey copy(const TValue value, const CStringView& override_name, const bool root) noexcept
    {
        const bool named = root ? !override_name.empty() : m_source.is_object_entry(value);
        const CPropertyNameId source_name = m_source.name_id(value);
        const CPropertyNameId name = root ? CDocumentConstruction::name(m_target, override_name) :
            (m_shared_ids ? source_name : translate(source_name, m_names,
                [&]()
                {
                    return CDocumentConstruction::import_name(m_target, m_source.property_name(source_name));
                }));
        if (!name)
        {
            return {};
        }
        SCopiedValue data;
        if (!read_copied_value(m_source, value, data))
        {
            return {};
        }
        if (data.kind == ELiveValueType::string)
        {
            const CStringValueId source_id = m_source.string_value_id(value);
            const CStringValueId id = m_shared_ids ? source_id : translate(source_id, m_strings,
                [&]()
                {
                    return CDocumentConstruction::import_string(m_target, m_source.string_value(source_id));
                });
            if (!id)
            {
                return {};
            }
            data.bits = id.query_value();
        }
        const CNodeKey copied = CDocumentConstruction::create(m_target, data.kind, data.bits, data.metadata, name, named);
        if (copied && (data.kind == ELiveValueType::string) && m_source.suppresses_newline_escaping(value))
        {
            (void)m_target.set_newline_escaping_suppressed(copied, true);
        }
        return copied;
    }

private:
    struct SStringKey
    {
        std::uint32_t id;
        [[nodiscard]] std::int32_t relationship(const SStringKey other) const noexcept
        {
            return (id < other.id) ? -1 : ((id > other.id) ? 1 : 0);
        }
    };
    template<class TId>
    struct SStringMap
    {
        TPodOrderedSlots<TId, SStringKey> ids;
        std::int32_t cursor{ -1 };
    };

    template<class TId, class TImport>
    [[nodiscard]] static TId translate(const TId source, SStringMap<TId>& map, TImport import) noexcept
    {
        if (!source)
        {
            return {};
        }
        if (source.is_empty())
        {
            return source;
        }
        const SStringKey key{ source.query_value() };
        //  Repeated and adjacent references are common during document traversal.
        const auto found = [&](const std::int32_t slot) noexcept
        {
            const SStringKey* const candidate = map.ids.key_at_slot(slot);
            if (!candidate || (candidate->id != key.id))
            {
                return false;
            }
            map.cursor = slot;
            return true;
        };
        if (found(map.cursor) || found(map.ids.next_live(map.cursor)) || found(map.ids.prev_live(map.cursor)))
        {
            return *map.ids.get_slot(map.cursor);
        }
        map.cursor = map.ids.find_index(key);
        if (map.cursor >= 0)
        {
            return *map.ids.get_slot(map.cursor);
        }
        if (!map.ids.is_ready() && !map.ids.initialise(1u))
        {
            return {};
        }
        const TId copied = import();
        if (!copied)
        {
            return {};
        }
        map.cursor = map.ids.insert(key, copied);
        return (map.cursor >= 0) ? copied : TId{};
    }

    CLiveDocument& m_target;
    const TSource& m_source;
    bool m_shared_ids{ false };
    SStringMap<CPropertyNameId> m_names;
    SStringMap<CStringValueId> m_strings;
};

//  One traversal for checked readers and internal invariant-preserving copies.
template<bool Constructed, class TSource, class TValue, class TCreate>
[[nodiscard]] CNodeKey copy_subtree_impl(CLiveDocument& target, const TSource& source,
    const TValue value, const CStringView& name, const unsigned depth, TCreate& create, const bool root) noexcept
{
    if (!target.is_ready() || (depth >= 256u) || !source.contains(value))
    {
        return {};
    }
    const CNodeKey copied = create(value, name, root);
    if (!copied)
    {
        return {};
    }
    for (TValue child = source.first_child(value); child.is_valid(); child = source.next_sibling(child))
    {
        const CNodeKey item = copy_subtree_impl<Constructed>(target, source, child,
            (source.is_object_entry(child) ? source.name(child) : CStringView{}), (depth + 1u), create, false);
        const bool attached = item && (Constructed ? CDocumentConstruction::append(target, copied, item) :
            target.append_child(copied, item).succeeded());
        if (!attached)
        {
            if (item && target.is_detached(item)) (void)target.erase(item);
            (void)target.erase(copied);
            return {};
        }
    }
    return copied;
}

//  Internal adapters must preserve the native document's names and topology.
template<class TSource, class TValue>
[[nodiscard]] CNodeKey copy_valid_subtree(CLiveDocument& target, const TSource& source,
    const TValue value, const CStringView& name, const unsigned depth = 0u) noexcept
{
    auto create = [&](const TValue item, const CStringView& item_name, bool) noexcept
    {
        return create_value_copy(target, source, item, item_name);
    };
    return copy_subtree_impl<true>(target, source, value, name, depth, create, true);
}

}   // namespace detail

//==============================================================================
//  Subtree copying
//==============================================================================

//  Copy a detached subtree, including within the same live document. Native
//  sources preserve established invariants; arbitrary readers remain checked.
//  Partial nodes are erased on failure. The enclosing depth is bounded at 256.
template<class TSource, class TValue>
[[nodiscard]] CNodeKey copy_subtree(CLiveDocument& target, const TSource& source,
    const TValue value, const CStringView& name, const unsigned depth = 0u) noexcept
{
    if constexpr (std::is_same_v<TSource, CLiveDocument> || std::is_same_v<TSource, CBakedDocument>)
    {
        if (!source.is_ready() || !target.is_ready())
        {
            return {};
        }
        detail::CNativeValueCopier<TSource> copier{ target, source };
        auto create = [&](const TValue item, const CStringView& item_name, const bool root) noexcept
        {
            return copier.copy(item, item_name, root);
        };
        return detail::copy_subtree_impl<true>(target, source, value, name, depth, create, true);
    }
    else
    {
        auto create = [&](const TValue item, const CStringView& item_name, bool) noexcept
        {
            return detail::create_value_copy(target, source, item, item_name);
        };
        return detail::copy_subtree_impl<false>(target, source, value, name, depth, create, true);
    }
}

}   // namespace document_translation

#endif // DATA_MODEL_DOCUMENT_COPY_HPP_INCLUDED

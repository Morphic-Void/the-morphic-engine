
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    type_compatibility.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    03 Oct 26
//
//  Compare resolved type graphs with an operation-local compatibility cache.

#include "schema/type_compatibility.hpp"

#include <cstring>

namespace schema
{

[[nodiscard]] static bool same_scalar(const SScalar& a, const SScalar& b) noexcept
{
    if (a.kind != b.kind)
    {
        return false;
    }
    if (a.kind == EScalar::signed_integer)
    {
        return a.value.signed_value == b.value.signed_value;
    }
    if (a.kind == EScalar::floating_point)
    {
        std::uint64_t left{}, right{};
        std::memcpy(&left, &a.value.floating_value, sizeof(left));
        std::memcpy(&right, &b.value.floating_value, sizeof(right));
        return left == right;
    }
    return a.value.unsigned_value == b.value.unsigned_value;
}

std::uint32_t CTypeCompatibility::hash(const SPair& pair) noexcept
{
    const std::uint64_t types = (static_cast<std::uint64_t>(pair.source.query_value()) << 32u) | pair.destination.query_value();
    const std::uint64_t defaults = (static_cast<std::uint64_t>(pair.source_default.query_value()) << 32u) | pair.destination_default.query_value();
    return COrdinalHashIndex::hash_key(COrdinalHashIndex::hash_key(types) ^ defaults);
}

ETypeMatchResult CTypeCompatibility::add(const SPair pair) noexcept
{
    if ((&m_source == &m_destination) && (pair.source == pair.destination) && (pair.source_default == pair.destination_default))
    {
        return ETypeMatchResult::match;
    }
    const std::uint32_t key = hash(pair);
    const std::uint32_t found = m_index.find(key, [&](const std::uint32_t i) noexcept
    {
        const SPair& p = m_pairs[i];
        return (p.source == pair.source) && (p.destination == pair.destination) &&
            (p.source_default == pair.source_default) && (p.destination_default == pair.destination_default);
    });
    if (found != COrdinalHashIndex::k_missing_record)
    {
        return m_pairs[found].result;
    }
    if (!m_index.reserve((m_pairs.size() + 1u), m_pairs.size(),
            [&](const std::size_t i) noexcept { return hash(m_pairs[i]); }) || !m_pairs.push_back(pair))
    {
        return ETypeMatchResult::allocation_failed;
    }
    m_index.insert(static_cast<std::uint32_t>(m_pairs.size() - 1u), key);
    return ETypeMatchResult::match;
}

bool CTypeCompatibility::same_header(const SType& a, const SType& b) const noexcept
{
    return (a.category == b.category) && (a.primitive == b.primitive) &&
        (a.size == b.size) && (a.alignment == b.alignment) && (a.stride == b.stride) &&
        (a.count == b.count) && (a.gaps == b.gaps) &&
        ((m_rule == ETypeMatch::representation) || (a.internal == b.internal)) &&
        (a.named_components == b.named_components) &&
        (m_source.name(a.name) == m_destination.name(b.name)) &&
        (a.element_or_storage.is_valid() == b.element_or_storage.is_valid());
}

bool CTypeCompatibility::same_decorations(const CTypesIndex source, const CTypesIndex destination) const noexcept
{
    if (m_rule != ETypeMatch::definition)
    {
        return true;
    }
    SDecoration a, b, found;
    bool left = m_source.first_decoration(source, a);
    bool right = m_destination.first_decoration(destination, b);
    while (left && right)
    {
        if (!m_destination.find_decoration(destination, a.key, found) || !(a.value == found.value))
        {
            return false;
        }
        left = m_source.next_decoration(a.source, a);
        right = m_destination.next_decoration(b.source, b);
    }

    //  Both walks must exhaust together, but their key order is immaterial.
    return left == right;
}

ETypeMatchResult CTypeCompatibility::inspect(const SPair pair) noexcept
{
    SType a, b;
    SDefault da, db;
    if (!m_source.type(pair.source, a) || !m_destination.type(pair.destination, b))
    {
        return inspect_declaration(pair);
    }
    if ((m_rule != ETypeMatch::representation) &&
         (!m_source.default_value(pair.source, pair.source_default, da) ||
          !m_destination.default_value(pair.destination, pair.destination_default, db)))
    {
        return ETypeMatchResult::invalid_input;
    }
    if (!same_header(a, b) || !same_decorations(pair.source, pair.destination) || ((m_rule != ETypeMatch::representation) &&
        ((da.kind != db.kind) || ((da.kind == EDefault::scalar) && !same_scalar(da.scalar, db.scalar)))))
    {
        return ETypeMatchResult::mismatch;
    }
    if (a.element_or_storage)
    {
        const auto result = add({ a.element_or_storage, b.element_or_storage, {}, {} });
        if (result != ETypeMatchResult::match)
        {
            return result;
        }
    }
    if ((m_rule != ETypeMatch::representation) && (a.category == ECategory::array))
    {
        const std::uint32_t supplied = (da.supplied_count > db.supplied_count) ? da.supplied_count : db.supplied_count;
        for (std::uint32_t i = 0u; i < supplied; ++i)
        {
            CTypesIndex ad, bd;
            if (!m_source.default_element(pair.source, pair.source_default, i, ad) ||
                !m_destination.default_element(pair.destination, pair.destination_default, i, bd))
            {
                return ETypeMatchResult::invalid_input;
            }
            const auto result = add({ a.element_or_storage, b.element_or_storage, ad, bd });
            if (result != ETypeMatchResult::match)
            {
                return result;
            }
        }
    }
    const bool has_children =
        (a.category == ECategory::structure) ||
        (a.category == ECategory::enumeration) ||
        (a.category == ECategory::bit_structure);
    for (std::uint32_t i = 0u; i < (has_children ? a.count : 0u); ++i)
    {
        if (a.category == ECategory::structure)
        {
            SMember am, bm;
            if (!m_source.member(m_source.member_at(pair.source, i), am) ||
                !m_destination.member(m_destination.member_at(pair.destination, i), bm))
            {
                return ETypeMatchResult::invalid_input;
            }
            if (!(m_source.name(am.name) == m_destination.name(bm.name)) ||
                (am.offset != bm.offset) || (am.size != bm.size) ||
                !same_decorations(m_source.member_at(pair.source, i), m_destination.member_at(pair.destination, i)))
            {
                return ETypeMatchResult::mismatch;
            }
            const auto result = add({ am.type, bm.type, ((m_rule != ETypeMatch::representation) ? am.default_description : CTypesIndex{}),
                ((m_rule != ETypeMatch::representation) ? bm.default_description : CTypesIndex{}) });
            if (result != ETypeMatchResult::match)
            {
                return result;
            }
        }
        else if (a.category == ECategory::enumeration)
        {
            SLabel al, bl;
            if (!m_source.label(m_source.label_at(pair.source, i), al) ||
                !m_destination.label(m_destination.label_at(pair.destination, i), bl))
            {
                return ETypeMatchResult::invalid_input;
            }
            if (!(m_source.name(al.name) == m_destination.name(bl.name)) || !same_scalar(al.value, bl.value))
            {
                return ETypeMatchResult::mismatch;
            }
        }
        else
        {
            SField af, bf;
            if (!m_source.field(m_source.field_at(pair.source, i), af) ||
                !m_destination.field(m_destination.field_at(pair.destination, i), bf))
            {
                return ETypeMatchResult::invalid_input;
            }
            if (!(m_source.name(af.name) == m_destination.name(bf.name)) || (af.mask != bf.mask) ||
                (af.shift != bf.shift) || (af.width != bf.width) ||
                (af.interpretation != bf.interpretation) ||
                (af.signed_value != bf.signed_value) || (af.primitive != bf.primitive) ||
                !same_decorations(m_source.field_at(pair.source, i), m_destination.field_at(pair.destination, i)))
            {
                return ETypeMatchResult::mismatch;
            }
            const auto result = add({ af.type, bf.type, ((m_rule != ETypeMatch::representation) ? af.default_description : CTypesIndex{}),
                ((m_rule != ETypeMatch::representation) ? bf.default_description : CTypesIndex{}) });
            if (result != ETypeMatchResult::match)
            {
                return result;
            }
        }
    }
    return ETypeMatchResult::match;
}

ETypeMatchResult CTypeCompatibility::inspect_declaration(const SPair pair) noexcept
{
    SDeclaration a, b;
    if (!m_source.declaration(pair.source, a) || !m_destination.declaration(pair.destination, b))
    {
        return ETypeMatchResult::invalid_input;
    }
    if ((a.category != b.category) || !(m_source.name(a.name) == m_destination.name(b.name)) ||
        !same_decorations(pair.source, pair.destination))
    {
        return ETypeMatchResult::mismatch;
    }
    if (a.category == EDeclarationCategory::association)
    {
        SAssociation left, right;
        if (!m_source.association(pair.source, left) || !m_destination.association(pair.destination, right))
        {
            return ETypeMatchResult::invalid_input;
        }
        if (left.target.is_valid() != right.target.is_valid())
        {
            return ETypeMatchResult::mismatch;
        }
        return left.target ? add({ left.target, right.target, {}, {} }) : ETypeMatchResult::match;
    }
    SInterface left, right;
    if (!m_source.interface_info(pair.source, left) || !m_destination.interface_info(pair.destination, right))
    {
        return ETypeMatchResult::invalid_input;
    }
    if (left.count != right.count)
    {
        return ETypeMatchResult::mismatch;
    }
    for (std::uint32_t i = 0u; i < left.count; ++i)
    {
        const CTypesIndex ai = m_source.interface_member_at(pair.source, i);
        const CTypesIndex bi = m_destination.interface_member_at(pair.destination, i);
        SInterfaceMember am, bm;
        if (!m_source.interface_member(ai, am) || !m_destination.interface_member(bi, bm))
        {
            return ETypeMatchResult::invalid_input;
        }
        if (!(m_source.name(am.name) == m_destination.name(bm.name)) || (am.count != bm.count) || !same_decorations(ai, bi))
        {
            return ETypeMatchResult::mismatch;
        }
        const auto result = add({ am.declaration, bm.declaration, {}, {} });
        if (result != ETypeMatchResult::match)
        {
            return result;
        }
    }
    return ETypeMatchResult::match;
}

ETypeMatchResult CTypeCompatibility::compare_declaration(const CTypesIndex source, const CTypesIndex destination) noexcept
{
    SDeclaration a, b;
    if (!m_source.declaration(source, a) || !m_destination.declaration(destination, b))
    {
        return ETypeMatchResult::invalid_input;
    }
    if ((a.category != b.category) || !(m_source.name(a.name) == m_destination.name(b.name)))
    {
        return ETypeMatchResult::mismatch;
    }
    if ((a.category != EDeclarationCategory::association) && (a.category != EDeclarationCategory::interface_type))
    {
        return compare(source, destination);
    }
    return compare_graph(source, destination);
}

ETypeMatchResult CTypeCompatibility::compare(const CTypesIndex source, const CTypesIndex destination) noexcept
{
    SType a, b;
    if (!m_source.is_ready() || !m_destination.is_ready() ||
        !m_source.type(source, a) || !m_destination.type(destination, b))
    {
        return ETypeMatchResult::invalid_input;
    }
    if (!same_header(a, b))
    {
        return ETypeMatchResult::mismatch;
    }
    return compare_graph(source, destination);
}

ETypeMatchResult CTypeCompatibility::compare_graph(const CTypesIndex source, const CTypesIndex destination) noexcept
{
    const std::size_t first = m_pairs.size();
    ETypeMatchResult result = add({ source, destination, {}, {} });
    for (std::size_t next = first; (result == ETypeMatchResult::match) && (next < m_pairs.size()); ++next)
    {
        result = inspect(m_pairs[next]);
    }
    if (result != ETypeMatchResult::match)
    {
        //  A seen pair is not proof of compatibility. Discard this traversal's
        //  unfinished pairs; retain only a proven mismatch for its root.
        const std::size_t keep = first + (((result == ETypeMatchResult::mismatch) && (m_pairs.size() > first)) ? 1u : 0u);
        while (m_pairs.size() > keep)
        {
            m_index.erase(static_cast<std::uint32_t>(m_pairs.size() - 1u),
                [&](const std::size_t i) noexcept { return hash(m_pairs[i]); });
            (void)m_pairs.discard_back();
        }
        if (keep > first)
        {
            m_pairs[first].result = result;
        }
    }
    return result;
}

}   // namespace schema

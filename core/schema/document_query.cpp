
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_query.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Read-only adapter over existing live and baked document queries.

#include "schema/document_query.hpp"
#include "data_model/live_document.hpp"

namespace schema
{

//==============================================================================
//  Internal document read adapter
//==============================================================================

namespace detail
{

CDocumentRead::CDocumentRead(const CLiveDocument& document) noexcept : m_live(&document), m_backing(EBacking::live)
{
}

CDocumentRead::CDocumentRead(const CBakedDocument& document) noexcept : m_baked(document), m_backing(EBacking::baked)
{
}

bool CDocumentRead::is_ready() const noexcept
{
    return (m_backing == EBacking::live) ? m_live->is_ready() : ((m_backing == EBacking::baked) && m_baked.is_ready());
}

bool CDocumentRead::live_value(const SOccurrence value) const noexcept
{
    return (m_backing == EBacking::live) && value.live.is_valid() && !value.baked.is_valid();
}

bool CDocumentRead::baked_value(const SOccurrence value) const noexcept
{
    return (m_backing == EBacking::baked) && value.baked.is_valid() && !value.live.is_valid();
}

SOccurrence CDocumentRead::root() const noexcept
{
    if (!is_ready())
    {
        return {};
    }
    return (m_backing == EBacking::live) ? SOccurrence{ m_live->root() } : SOccurrence{ m_baked.root() };
}

bool CDocumentRead::contains(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->contains(value.live) : (baked_value(value) && m_baked.contains(value.baked));
}

EDocumentValueKind CDocumentRead::value_kind(const SOccurrence value) const noexcept
{
    if (live_value(value))
    {
        switch (m_live->value_type(value.live))
        {
            case ELiveValueType::empty: return EDocumentValueKind::empty;
            case ELiveValueType::null_value: return EDocumentValueKind::null_value;
            case ELiveValueType::boolean: return EDocumentValueKind::boolean;
            case ELiveValueType::integer: return EDocumentValueKind::integer;
            case ELiveValueType::floating_point: return EDocumentValueKind::floating_point;
            case ELiveValueType::string: return EDocumentValueKind::string;
            case ELiveValueType::array: return EDocumentValueKind::array;
            case ELiveValueType::object: return EDocumentValueKind::object;
            default: return EDocumentValueKind::invalid;
        }
    }
    if (baked_value(value))
    {
        switch (m_baked.value_type(value.baked))
        {
            case EBakedValueType::null_value: return EDocumentValueKind::null_value;
            case EBakedValueType::boolean: return EDocumentValueKind::boolean;
            case EBakedValueType::integer: return EDocumentValueKind::integer;
            case EBakedValueType::floating_point: return EDocumentValueKind::floating_point;
            case EBakedValueType::string: return EDocumentValueKind::string;
            case EBakedValueType::array: return EDocumentValueKind::array;
            case EBakedValueType::object: return EDocumentValueKind::object;
            default: return EDocumentValueKind::invalid;
        }
    }
    return EDocumentValueKind::invalid;
}

bool CDocumentRead::is_object_entry(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->is_object_entry(value.live) :
        (baked_value(value) && m_baked.is_object_entry(value.baked));
}

bool CDocumentRead::suppresses_newline_escaping(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->suppresses_newline_escaping(value.live) :
        (baked_value(value) && m_baked.suppresses_newline_escaping(value.baked));
}

CPropertyNameId CDocumentRead::name_id(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->name_id(value.live) :
        (baked_value(value) ? m_baked.name_id(value.baked) : CPropertyNameId{});
}

CStringView CDocumentRead::name(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->name(value.live) :
        (baked_value(value) ? m_baked.name(value.baked) : CStringView{});
}

CStringView CDocumentRead::property_name(const CPropertyNameId id) const noexcept
{
    return (m_backing == EBacking::live) ? m_live->property_name(id) :
        ((m_backing == EBacking::baked) ? m_baked.property_name(id) : CStringView{});
}

SOccurrence CDocumentRead::parent(const SOccurrence value) const noexcept
{
    return live_value(value) ? SOccurrence{ m_live->parent(value.live) } :
        (baked_value(value) ? SOccurrence{ m_baked.parent(value.baked) } : SOccurrence{});
}

SOccurrence CDocumentRead::first_child(const SOccurrence value) const noexcept
{
    return live_value(value) ? SOccurrence{ m_live->first_child(value.live) } :
        (baked_value(value) ? SOccurrence{ m_baked.first_child(value.baked) } : SOccurrence{});
}

SOccurrence CDocumentRead::next_sibling(const SOccurrence value) const noexcept
{
    return live_value(value) ? SOccurrence{ m_live->next_sibling(value.live) } :
        (baked_value(value) ? SOccurrence{ m_baked.next_sibling(value.baked) } : SOccurrence{});
}

std::uint32_t CDocumentRead::child_count(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->child_count(value.live) :
        (baked_value(value) ? m_baked.child_count(value.baked) : 0u);
}

SOccurrence CDocumentRead::array_at(const SOccurrence array, const std::uint32_t ordinal) const noexcept
{
    if (baked_value(array))
    {
        return SOccurrence{ m_baked.array_at(array.baked, ordinal) };
    }
    if (!live_value(array) || (m_live->value_type(array.live) != ELiveValueType::array) ||
        (ordinal >= m_live->child_count(array.live)))
    {
        return {};
    }
    CNodeKey element = m_live->first_child(array.live);
    for (std::uint32_t index = 0u; index < ordinal; ++index)
    {
        element = m_live->next_sibling(element);
    }
    return SOccurrence{ element };
}

SOccurrence CDocumentRead::object_child(const SOccurrence object, const CStringView& name) const noexcept
{
    return live_value(object) ? SOccurrence{ m_live->object_child(object.live, name) } :
        (baked_value(object) ? SOccurrence{ m_baked.object_child(object.baked, name) } : SOccurrence{});
}

bool CDocumentRead::boolean_value(const SOccurrence value, bool& result) const noexcept
{
    return live_value(value) ? m_live->boolean_value(value.live, result) :
        (baked_value(value) && m_baked.boolean_value(value.baked, result));
}

bool CDocumentRead::signed_integer_value(const SOccurrence value, std::int64_t& result) const noexcept
{
    return live_value(value) ? m_live->signed_integer_value(value.live, result) :
        (baked_value(value) && m_baked.signed_integer_value(value.baked, result));
}

bool CDocumentRead::unsigned_integer_value(const SOccurrence value, std::uint64_t& result) const noexcept
{
    return live_value(value) ? m_live->unsigned_integer_value(value.live, result) :
        (baked_value(value) && m_baked.unsigned_integer_value(value.baked, result));
}

bool CDocumentRead::integer_metadata(const SOccurrence value, CIntegerMetadata& result) const noexcept
{
    return live_value(value) ? m_live->integer_metadata(value.live, result) :
        (baked_value(value) && m_baked.integer_metadata(value.baked, result));
}

bool CDocumentRead::floating_point_value(const SOccurrence value, double& result) const noexcept
{
    return live_value(value) ? m_live->floating_point_value(value.live, result) :
        (baked_value(value) && m_baked.floating_point_value(value.baked, result));
}

CStringView CDocumentRead::string_value(const SOccurrence value) const noexcept
{
    return live_value(value) ? m_live->string_value(value.live) :
        (baked_value(value) ? m_baked.string_value(value.baked) : CStringView{});
}

}   // namespace detail

}   // namespace schema

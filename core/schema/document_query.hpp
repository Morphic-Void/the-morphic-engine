
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_query.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Representation-neutral, read-only document queries for schema roles.

#pragma once

#ifndef SCHEMA_DOCUMENT_QUERY_HPP_INCLUDED
#define SCHEMA_DOCUMENT_QUERY_HPP_INCLUDED

#include "data_model/baked_document.hpp"

class CLiveDocument;

namespace schema
{

class CBakedSchema;
class CLiveSchema;
class CBakedInstances;
class CLiveInstances;
class CBakedBulkData;
class CLiveBulkData;
class CSchemaHandle;

//==============================================================================
//  Document value kinds
//==============================================================================

enum class EDocumentValueKind : std::uint8_t
{
    invalid = 0u,
    empty,
    null_value,
    boolean,
    integer,
    floating_point,
    string,
    array,
    object
};

//==============================================================================
//  Internal document identities and read adapter
//==============================================================================

namespace detail
{

//  A valid occurrence has exactly one identity; a default occurrence has
//  neither. The live key is never narrowed to a baked index.
struct SOccurrence
{
    CNodeKey live;
    CBakedValueIndex baked;

    constexpr SOccurrence() noexcept = default;
    explicit constexpr SOccurrence(const CNodeKey key) noexcept : live(key) {}
    explicit constexpr SOccurrence(const CBakedValueIndex index) noexcept : baked(index) {}

    [[nodiscard]] constexpr bool is_valid() const noexcept { return live.is_valid() != baked.is_valid(); }
    [[nodiscard]] constexpr bool is_live() const noexcept { return live.is_valid() && !baked.is_valid(); }
    [[nodiscard]] constexpr bool is_baked() const noexcept { return baked.is_valid() && !live.is_valid(); }
    [[nodiscard]] constexpr std::uint64_t query_value() const noexcept
    {
        return is_live() ? live.query_value() : (is_baked() ? baked.query_value() : 0u);
    }
};

[[nodiscard]] constexpr bool operator==(const SOccurrence lhs, const SOccurrence rhs) noexcept
{
    return (lhs.live == rhs.live) && (lhs.baked == rhs.baked);
}

[[nodiscard]] constexpr bool operator!=(const SOccurrence lhs, const SOccurrence rhs) noexcept
{
    return !(lhs == rhs);
}

//  Resolver and role adapters use this bridge; raw identities stay out of the
//  public schema-handle construction surface.
struct SSchemaHandleAccess
{
    [[nodiscard]] static constexpr CSchemaHandle make(const SOccurrence occurrence) noexcept;
    [[nodiscard]] static constexpr SOccurrence occurrence(const CSchemaHandle handle) noexcept;
};

class CDocumentRead
{
public:
    CDocumentRead() noexcept = default;
    explicit CDocumentRead(const CLiveDocument& document) noexcept;
    explicit CDocumentRead(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] SOccurrence root() const noexcept;
    [[nodiscard]] bool contains(const SOccurrence value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const SOccurrence value) const noexcept;
    [[nodiscard]] bool is_object_entry(const SOccurrence value) const noexcept;
    [[nodiscard]] bool suppresses_newline_escaping(const SOccurrence value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const SOccurrence value) const noexcept;
    [[nodiscard]] CStringView name(const SOccurrence value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] SOccurrence parent(const SOccurrence value) const noexcept;
    [[nodiscard]] SOccurrence first_child(const SOccurrence value) const noexcept;
    [[nodiscard]] SOccurrence next_sibling(const SOccurrence value) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const SOccurrence value) const noexcept;
    [[nodiscard]] SOccurrence array_at(const SOccurrence array, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] SOccurrence object_child(const SOccurrence object, const CStringView& name) const noexcept;
    [[nodiscard]] bool boolean_value(const SOccurrence value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const SOccurrence value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const SOccurrence value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool integer_metadata(const SOccurrence value, CIntegerMetadata& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const SOccurrence value, double& result) const noexcept;
    [[nodiscard]] CStringView string_value(const SOccurrence value) const noexcept;

private:
    enum class EBacking : std::uint8_t { none, live, baked };

    [[nodiscard]] bool live_value(const SOccurrence value) const noexcept;
    [[nodiscard]] bool baked_value(const SOccurrence value) const noexcept;

    const CLiveDocument* m_live{ nullptr };
    CBakedDocument m_baked; //  Copy of the non-owning view, never a caller-view pointer.
    EBacking m_backing{ EBacking::none };
};

}   // namespace detail

//==============================================================================
//  Schema document handle
//==============================================================================

class CSchemaHandle
{
public:
    constexpr CSchemaHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CSchemaHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend struct detail::SSchemaHandleAccess;
    friend class CBakedSchema;
    friend class CLiveSchema;
    detail::SOccurrence m_occurrence;
};

[[nodiscard]] constexpr bool operator==(const CSchemaHandle lhs, const CSchemaHandle rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CSchemaHandle lhs, const CSchemaHandle rhs) noexcept;

constexpr CSchemaHandle detail::SSchemaHandleAccess::make(const detail::SOccurrence occurrence) noexcept
{
    return CSchemaHandle{ occurrence };
}

constexpr detail::SOccurrence detail::SSchemaHandleAccess::occurrence(const CSchemaHandle handle) noexcept
{
    return handle.m_occurrence;
}

[[nodiscard]] constexpr bool operator==(const CSchemaHandle lhs, const CSchemaHandle rhs) noexcept
{
    return detail::SSchemaHandleAccess::occurrence(lhs) == detail::SSchemaHandleAccess::occurrence(rhs);
}

[[nodiscard]] constexpr bool operator!=(const CSchemaHandle lhs, const CSchemaHandle rhs) noexcept
{
    return !(lhs == rhs);
}

//==============================================================================
//  Schema document query
//==============================================================================

//  Borrowed schema-role read view. A baked query retains its view object by
//  value; the caller keeps the block bytes alive. A live query borrows its
//  document. Handles are meaningful only with their originating document.
class CSchemaDocumentQuery
{
public:
    CSchemaDocumentQuery() noexcept = default;
    explicit CSchemaDocumentQuery(const CLiveDocument& document) noexcept;
    explicit CSchemaDocumentQuery(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] CSchemaHandle root() const noexcept;
    [[nodiscard]] bool contains(const CSchemaHandle value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CSchemaHandle object_child(const CSchemaHandle object, const CStringView& name) const noexcept;
    [[nodiscard]] CSchemaHandle parent(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CSchemaHandle first_child(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CSchemaHandle next_sibling(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CSchemaHandle array_at(const CSchemaHandle value, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CSchemaHandle value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CStringView name(const CSchemaHandle value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CSchemaHandle value) const noexcept;
    [[nodiscard]] bool boolean_value(const CSchemaHandle value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CSchemaHandle value, double& result) const noexcept;

private:
    detail::CDocumentRead m_query;
};

//==============================================================================
//  Instance and bulk document handles
//==============================================================================

class CInstanceHandle
{
public:
    constexpr CInstanceHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CInstanceHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend class CBakedInstances;
    friend class CLiveInstances;
    detail::SOccurrence m_occurrence;
};

class CBulkHandle
{
public:
    constexpr CBulkHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CBulkHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend class CBakedBulkData;
    friend class CLiveBulkData;
    detail::SOccurrence m_occurrence;
};

}   // namespace schema

#endif // SCHEMA_DOCUMENT_QUERY_HPP_INCLUDED

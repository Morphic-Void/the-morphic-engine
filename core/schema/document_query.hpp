
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

//==============================================================================
//  Role and handle declarations
//==============================================================================

class CBakedSchema;
class CLiveSchema;
class CBakedInstances;
class CLiveInstances;
class CBakedBulkData;
class CLiveBulkData;
class CSchemaHandle;
class CInstanceHandle;
class CBulkHandle;

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

//  Resolver and role adapters use these bridges; raw identities stay out of the
//  public schema-handle construction surface.
struct SSchemaHandleAccess
{
    [[nodiscard]] static constexpr CSchemaHandle make(const SOccurrence occurrence) noexcept;
    [[nodiscard]] static constexpr SOccurrence occurrence(const CSchemaHandle handle) noexcept;
};

struct SInstanceHandleAccess
{
    [[nodiscard]] static constexpr CInstanceHandle make(const SOccurrence occurrence) noexcept;
    [[nodiscard]] static constexpr SOccurrence occurrence(const CInstanceHandle handle) noexcept;
};

struct SBulkHandleAccess
{
    [[nodiscard]] static constexpr CBulkHandle make(const SOccurrence occurrence) noexcept;
    [[nodiscard]] static constexpr SOccurrence occurrence(const CBulkHandle handle) noexcept;
};

//  One read adapter for either document representation.
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
//  Instance document handle
//==============================================================================

class CInstanceHandle
{
public:
    constexpr CInstanceHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CInstanceHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend struct detail::SInstanceHandleAccess;
    friend class CBakedInstances;
    friend class CLiveInstances;
    detail::SOccurrence m_occurrence;
};

[[nodiscard]] constexpr bool operator==(const CInstanceHandle lhs, const CInstanceHandle rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CInstanceHandle lhs, const CInstanceHandle rhs) noexcept;

constexpr CInstanceHandle detail::SInstanceHandleAccess::make(const detail::SOccurrence occurrence) noexcept
{
    return CInstanceHandle{ occurrence };
}

constexpr detail::SOccurrence detail::SInstanceHandleAccess::occurrence(const CInstanceHandle handle) noexcept
{
    return handle.m_occurrence;
}

[[nodiscard]] constexpr bool operator==(const CInstanceHandle lhs, const CInstanceHandle rhs) noexcept
{
    return detail::SInstanceHandleAccess::occurrence(lhs) == detail::SInstanceHandleAccess::occurrence(rhs);
}

[[nodiscard]] constexpr bool operator!=(const CInstanceHandle lhs, const CInstanceHandle rhs) noexcept
{
    return !(lhs == rhs);
}

//==============================================================================
//  Instance document query
//==============================================================================

//  Read-only original-root query with instance-specific handles. Baked backing
//  is borrowed; handles and returned strings require their originating document.
class CInstanceDocumentQuery
{
public:
    CInstanceDocumentQuery() noexcept = default;
    explicit CInstanceDocumentQuery(const CLiveDocument& document) noexcept;
    explicit CInstanceDocumentQuery(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] CInstanceHandle root() const noexcept;
    [[nodiscard]] bool contains(const CInstanceHandle value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CInstanceHandle object_child(const CInstanceHandle object, const CStringView& name) const noexcept;
    [[nodiscard]] CInstanceHandle parent(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CInstanceHandle first_child(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CInstanceHandle next_sibling(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CInstanceHandle array_at(const CInstanceHandle value, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CInstanceHandle value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CStringView name(const CInstanceHandle value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CInstanceHandle value) const noexcept;
    [[nodiscard]] bool boolean_value(const CInstanceHandle value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CInstanceHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CInstanceHandle value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CInstanceHandle value, double& result) const noexcept;

private:
    detail::CDocumentRead m_query;
};

//==============================================================================
//  Bulk document handle
//==============================================================================

class CBulkHandle
{
public:
    constexpr CBulkHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CBulkHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend struct detail::SBulkHandleAccess;
    friend class CBakedBulkData;
    friend class CLiveBulkData;
    detail::SOccurrence m_occurrence;
};

[[nodiscard]] constexpr bool operator==(const CBulkHandle lhs, const CBulkHandle rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CBulkHandle lhs, const CBulkHandle rhs) noexcept;

constexpr CBulkHandle detail::SBulkHandleAccess::make(const detail::SOccurrence occurrence) noexcept
{
    return CBulkHandle{ occurrence };
}

constexpr detail::SOccurrence detail::SBulkHandleAccess::occurrence(const CBulkHandle handle) noexcept
{
    return handle.m_occurrence;
}

[[nodiscard]] constexpr bool operator==(const CBulkHandle lhs, const CBulkHandle rhs) noexcept
{
    return detail::SBulkHandleAccess::occurrence(lhs) == detail::SBulkHandleAccess::occurrence(rhs);
}

[[nodiscard]] constexpr bool operator!=(const CBulkHandle lhs, const CBulkHandle rhs) noexcept
{
    return !(lhs == rhs);
}

//==============================================================================
//  Bulk document query
//==============================================================================

//  Read-only original-root query with bulk-specific handles. Baked backing is
//  borrowed; handles and returned strings require their originating document.
class CBulkDocumentQuery
{
public:
    CBulkDocumentQuery() noexcept = default;
    explicit CBulkDocumentQuery(const CLiveDocument& document) noexcept;
    explicit CBulkDocumentQuery(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] CBulkHandle root() const noexcept;
    [[nodiscard]] bool contains(const CBulkHandle value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const CBulkHandle value) const noexcept;
    [[nodiscard]] CBulkHandle object_child(const CBulkHandle object, const CStringView& name) const noexcept;
    [[nodiscard]] CBulkHandle parent(const CBulkHandle value) const noexcept;
    [[nodiscard]] CBulkHandle first_child(const CBulkHandle value) const noexcept;
    [[nodiscard]] CBulkHandle next_sibling(const CBulkHandle value) const noexcept;
    [[nodiscard]] CBulkHandle array_at(const CBulkHandle value, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CBulkHandle value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CBulkHandle value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const CBulkHandle value) const noexcept;
    [[nodiscard]] CStringView name(const CBulkHandle value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CBulkHandle value) const noexcept;
    [[nodiscard]] bool boolean_value(const CBulkHandle value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CBulkHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CBulkHandle value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CBulkHandle value, double& result) const noexcept;

private:
    friend class CLiveBulkData;
    detail::CDocumentRead m_query;
};

}   // namespace schema

#endif // SCHEMA_DOCUMENT_QUERY_HPP_INCLUDED

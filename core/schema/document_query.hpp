
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_query.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    29 Sep 26
//
//  Representation-neutral, read-only document queries for schema roles.

//  Shared public handles and read-only queries, normally included by role headers.
//  The detail identities and read adapter support schema implementation only.

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
class CSchemaDocumentQuery;
class CInstanceDocumentQuery;
class CBulkDataDocumentQuery;
class CResolvedSchema;
class CSchemaHandle;
class CInstanceHandle;
class CBulkDataHandle;

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
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
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

struct SBulkDataHandleAccess
{
    [[nodiscard]] static constexpr CBulkDataHandle make(const SOccurrence occurrence) noexcept;
    [[nodiscard]] static constexpr SOccurrence occurrence(const CBulkDataHandle handle) noexcept;
};

class CDocumentRead;

struct SDocumentQueryAccess
{
    [[nodiscard]] static const CDocumentRead& read(const CSchemaDocumentQuery& query) noexcept;
    [[nodiscard]] static const CDocumentRead& read(const CInstanceDocumentQuery& query) noexcept;
    [[nodiscard]] static const CDocumentRead& read(const CBulkDataDocumentQuery& query) noexcept;
    [[nodiscard]] static bool backs(const CResolvedSchema& schema, const CLiveDocument& document) noexcept;
};

//  One read adapter for either document representation.
class CDocumentRead
{
public:
    CDocumentRead() noexcept = default;
    explicit CDocumentRead(const CLiveDocument& document) noexcept;
    explicit CDocumentRead(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool backs(const CLiveDocument& document) const noexcept { return m_live == &document; }
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] SOccurrence root() const noexcept;
    [[nodiscard]] bool contains(const SOccurrence value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const SOccurrence value) const noexcept;
    [[nodiscard]] bool is_object_entry(const SOccurrence value) const noexcept;
    [[nodiscard]] bool suppresses_newline_escaping(const SOccurrence value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const SOccurrence value) const noexcept;
    [[nodiscard]] CStringView name(const SOccurrence value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CPropertyNameId find_property_name_id(const CStringView& name) const noexcept;
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
    [[nodiscard]] CPropertyNameId find_property_name_id(const CStringView& name) const noexcept;
    [[nodiscard]] CStringView string_value(const CSchemaHandle value) const noexcept;
    [[nodiscard]] bool boolean_value(const CSchemaHandle value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool integer_metadata(const CSchemaHandle value, CIntegerMetadata& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CSchemaHandle value, double& result) const noexcept;
    [[nodiscard]] bool suppresses_newline_escaping(const CSchemaHandle value) const noexcept;

private:
    friend struct detail::SDocumentQueryAccess;
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
    friend class CLiveInstances;
    friend struct detail::SDocumentQueryAccess;
    detail::CDocumentRead m_query;
};

//==============================================================================
//  Bulk data document handle
//==============================================================================

class CBulkDataHandle
{
public:
    constexpr CBulkDataHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_occurrence.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }

private:
    explicit constexpr CBulkDataHandle(const detail::SOccurrence occurrence) noexcept : m_occurrence(occurrence) {}
    friend struct detail::SBulkDataHandleAccess;
    friend class CBakedBulkData;
    friend class CLiveBulkData;
    detail::SOccurrence m_occurrence;
};

[[nodiscard]] constexpr bool operator==(const CBulkDataHandle lhs, const CBulkDataHandle rhs) noexcept;
[[nodiscard]] constexpr bool operator!=(const CBulkDataHandle lhs, const CBulkDataHandle rhs) noexcept;

constexpr CBulkDataHandle detail::SBulkDataHandleAccess::make(const detail::SOccurrence occurrence) noexcept
{
    return CBulkDataHandle{ occurrence };
}

constexpr detail::SOccurrence detail::SBulkDataHandleAccess::occurrence(const CBulkDataHandle handle) noexcept
{
    return handle.m_occurrence;
}

[[nodiscard]] constexpr bool operator==(const CBulkDataHandle lhs, const CBulkDataHandle rhs) noexcept
{
    return detail::SBulkDataHandleAccess::occurrence(lhs) == detail::SBulkDataHandleAccess::occurrence(rhs);
}

[[nodiscard]] constexpr bool operator!=(const CBulkDataHandle lhs, const CBulkDataHandle rhs) noexcept
{
    return !(lhs == rhs);
}

//==============================================================================
//  Bulk data document query
//==============================================================================

//  Read-only original-root query with bulk data handles. Baked backing is
//  borrowed; handles and returned strings require their originating document.
class CBulkDataDocumentQuery
{
public:
    CBulkDataDocumentQuery() noexcept = default;
    explicit CBulkDataDocumentQuery(const CLiveDocument& document) noexcept;
    explicit CBulkDataDocumentQuery(const CBakedDocument& document) noexcept;

    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] CBulkDataHandle root() const noexcept;
    [[nodiscard]] bool contains(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] EDocumentValueKind value_kind(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CBulkDataHandle object_child(const CBulkDataHandle object, const CStringView& name) const noexcept;
    [[nodiscard]] CBulkDataHandle parent(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CBulkDataHandle first_child(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CBulkDataHandle next_sibling(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CBulkDataHandle array_at(const CBulkDataHandle value, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] std::uint32_t child_count(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] bool is_object_entry(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CPropertyNameId name_id(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CStringView name(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] CStringView property_name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] CStringView string_value(const CBulkDataHandle value) const noexcept;
    [[nodiscard]] bool boolean_value(const CBulkDataHandle value, bool& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CBulkDataHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CBulkDataHandle value, std::uint64_t& result) const noexcept;
    [[nodiscard]] bool floating_point_value(const CBulkDataHandle value, double& result) const noexcept;

private:
    friend class CLiveBulkData;
    friend struct detail::SDocumentQueryAccess;
    detail::CDocumentRead m_query;
};

//==============================================================================
//  CSchemaDocumentQuery out of class function bodies
//==============================================================================

inline CSchemaDocumentQuery::CSchemaDocumentQuery(const CLiveDocument& document) noexcept : m_query(document)
{
}

inline CSchemaDocumentQuery::CSchemaDocumentQuery(const CBakedDocument& document) noexcept : m_query(document)
{
}

[[nodiscard]] inline bool CSchemaDocumentQuery::is_ready() const noexcept
{
    return m_query.is_ready();
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::root() const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.root());
}

[[nodiscard]] inline bool CSchemaDocumentQuery::contains(const CSchemaHandle value) const noexcept
{
    return m_query.contains(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline EDocumentValueKind CSchemaDocumentQuery::value_kind(const CSchemaHandle value) const noexcept
{
    return m_query.value_kind(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::object_child(const CSchemaHandle object, const CStringView& name) const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.object_child(detail::SSchemaHandleAccess::occurrence(object), name));
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::parent(const CSchemaHandle value) const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.parent(detail::SSchemaHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::first_child(const CSchemaHandle value) const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.first_child(detail::SSchemaHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::next_sibling(const CSchemaHandle value) const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.next_sibling(detail::SSchemaHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CSchemaHandle CSchemaDocumentQuery::array_at(const CSchemaHandle value, const std::uint32_t ordinal) const noexcept
{
    return detail::SSchemaHandleAccess::make(m_query.array_at(detail::SSchemaHandleAccess::occurrence(value), ordinal));
}

[[nodiscard]] inline std::uint32_t CSchemaDocumentQuery::child_count(const CSchemaHandle value) const noexcept
{
    return m_query.child_count(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CSchemaDocumentQuery::is_object_entry(const CSchemaHandle value) const noexcept
{
    return m_query.is_object_entry(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline CPropertyNameId CSchemaDocumentQuery::name_id(const CSchemaHandle value) const noexcept
{
    return m_query.name_id(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CSchemaDocumentQuery::name(const CSchemaHandle value) const noexcept
{
    return m_query.name(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CSchemaDocumentQuery::property_name(const CPropertyNameId id) const noexcept
{
    return m_query.property_name(id);
}

[[nodiscard]] inline CPropertyNameId CSchemaDocumentQuery::find_property_name_id(const CStringView& name) const noexcept
{
    return m_query.find_property_name_id(name);
}

[[nodiscard]] inline CStringView CSchemaDocumentQuery::string_value(const CSchemaHandle value) const noexcept
{
    return m_query.string_value(detail::SSchemaHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CSchemaDocumentQuery::boolean_value(const CSchemaHandle value, bool& result) const noexcept
{
    return m_query.boolean_value(detail::SSchemaHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CSchemaDocumentQuery::signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept
{
    return m_query.signed_integer_value(detail::SSchemaHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CSchemaDocumentQuery::unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept
{
    return m_query.unsigned_integer_value(detail::SSchemaHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CSchemaDocumentQuery::floating_point_value(const CSchemaHandle value, double& result) const noexcept
{
    return m_query.floating_point_value(detail::SSchemaHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CSchemaDocumentQuery::integer_metadata(const CSchemaHandle value, CIntegerMetadata& result) const noexcept
{
    return m_query.integer_metadata(detail::SSchemaHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CSchemaDocumentQuery::suppresses_newline_escaping(const CSchemaHandle value) const noexcept
{
    return m_query.suppresses_newline_escaping(detail::SSchemaHandleAccess::occurrence(value));
}

//==============================================================================
//  CInstanceDocumentQuery out of class function bodies
//==============================================================================

inline CInstanceDocumentQuery::CInstanceDocumentQuery(const CLiveDocument& document) noexcept : m_query(document)
{
}

inline CInstanceDocumentQuery::CInstanceDocumentQuery(const CBakedDocument& document) noexcept : m_query(document)
{
}

[[nodiscard]] inline bool CInstanceDocumentQuery::is_ready() const noexcept
{
    return m_query.is_ready();
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::root() const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.root());
}

[[nodiscard]] inline bool CInstanceDocumentQuery::contains(const CInstanceHandle value) const noexcept
{
    return m_query.contains(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline EDocumentValueKind CInstanceDocumentQuery::value_kind(const CInstanceHandle value) const noexcept
{
    return m_query.value_kind(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::object_child(const CInstanceHandle object, const CStringView& name) const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.object_child(detail::SInstanceHandleAccess::occurrence(object), name));
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::parent(const CInstanceHandle value) const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.parent(detail::SInstanceHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::first_child(const CInstanceHandle value) const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.first_child(detail::SInstanceHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::next_sibling(const CInstanceHandle value) const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.next_sibling(detail::SInstanceHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CInstanceHandle CInstanceDocumentQuery::array_at(const CInstanceHandle value, const std::uint32_t ordinal) const noexcept
{
    return detail::SInstanceHandleAccess::make(m_query.array_at(detail::SInstanceHandleAccess::occurrence(value), ordinal));
}

[[nodiscard]] inline std::uint32_t CInstanceDocumentQuery::child_count(const CInstanceHandle value) const noexcept
{
    return m_query.child_count(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CInstanceDocumentQuery::is_object_entry(const CInstanceHandle value) const noexcept
{
    return m_query.is_object_entry(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline CPropertyNameId CInstanceDocumentQuery::name_id(const CInstanceHandle value) const noexcept
{
    return m_query.name_id(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CInstanceDocumentQuery::name(const CInstanceHandle value) const noexcept
{
    return m_query.name(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CInstanceDocumentQuery::property_name(const CPropertyNameId id) const noexcept
{
    return m_query.property_name(id);
}

[[nodiscard]] inline CStringView CInstanceDocumentQuery::string_value(const CInstanceHandle value) const noexcept
{
    return m_query.string_value(detail::SInstanceHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CInstanceDocumentQuery::boolean_value(const CInstanceHandle value, bool& result) const noexcept
{
    return m_query.boolean_value(detail::SInstanceHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CInstanceDocumentQuery::signed_integer_value(const CInstanceHandle value, std::int64_t& result) const noexcept
{
    return m_query.signed_integer_value(detail::SInstanceHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CInstanceDocumentQuery::unsigned_integer_value(const CInstanceHandle value, std::uint64_t& result) const noexcept
{
    return m_query.unsigned_integer_value(detail::SInstanceHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CInstanceDocumentQuery::floating_point_value(const CInstanceHandle value, double& result) const noexcept
{
    return m_query.floating_point_value(detail::SInstanceHandleAccess::occurrence(value), result);
}

//==============================================================================
//  CBulkDataDocumentQuery out of class function bodies
//==============================================================================

inline CBulkDataDocumentQuery::CBulkDataDocumentQuery(const CLiveDocument& document) noexcept : m_query(document)
{
}

inline CBulkDataDocumentQuery::CBulkDataDocumentQuery(const CBakedDocument& document) noexcept : m_query(document)
{
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::is_ready() const noexcept
{
    return m_query.is_ready();
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::root() const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.root());
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::contains(const CBulkDataHandle value) const noexcept
{
    return m_query.contains(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline EDocumentValueKind CBulkDataDocumentQuery::value_kind(const CBulkDataHandle value) const noexcept
{
    return m_query.value_kind(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::object_child(const CBulkDataHandle object, const CStringView& name) const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.object_child(detail::SBulkDataHandleAccess::occurrence(object), name));
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::parent(const CBulkDataHandle value) const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.parent(detail::SBulkDataHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::first_child(const CBulkDataHandle value) const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.first_child(detail::SBulkDataHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::next_sibling(const CBulkDataHandle value) const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.next_sibling(detail::SBulkDataHandleAccess::occurrence(value)));
}

[[nodiscard]] inline CBulkDataHandle CBulkDataDocumentQuery::array_at(const CBulkDataHandle value, const std::uint32_t ordinal) const noexcept
{
    return detail::SBulkDataHandleAccess::make(m_query.array_at(detail::SBulkDataHandleAccess::occurrence(value), ordinal));
}

[[nodiscard]] inline std::uint32_t CBulkDataDocumentQuery::child_count(const CBulkDataHandle value) const noexcept
{
    return m_query.child_count(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::is_object_entry(const CBulkDataHandle value) const noexcept
{
    return m_query.is_object_entry(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline CPropertyNameId CBulkDataDocumentQuery::name_id(const CBulkDataHandle value) const noexcept
{
    return m_query.name_id(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CBulkDataDocumentQuery::name(const CBulkDataHandle value) const noexcept
{
    return m_query.name(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline CStringView CBulkDataDocumentQuery::property_name(const CPropertyNameId id) const noexcept
{
    return m_query.property_name(id);
}

[[nodiscard]] inline CStringView CBulkDataDocumentQuery::string_value(const CBulkDataHandle value) const noexcept
{
    return m_query.string_value(detail::SBulkDataHandleAccess::occurrence(value));
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::boolean_value(const CBulkDataHandle value, bool& result) const noexcept
{
    return m_query.boolean_value(detail::SBulkDataHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::signed_integer_value(const CBulkDataHandle value, std::int64_t& result) const noexcept
{
    return m_query.signed_integer_value(detail::SBulkDataHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::unsigned_integer_value(const CBulkDataHandle value, std::uint64_t& result) const noexcept
{
    return m_query.unsigned_integer_value(detail::SBulkDataHandleAccess::occurrence(value), result);
}

[[nodiscard]] inline bool CBulkDataDocumentQuery::floating_point_value(const CBulkDataHandle value, double& result) const noexcept
{
    return m_query.floating_point_value(detail::SBulkDataHandleAccess::occurrence(value), result);
}

}   // namespace schema

#endif // SCHEMA_DOCUMENT_QUERY_HPP_INCLUDED

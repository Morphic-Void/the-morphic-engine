
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Binary-free aggregate authoring, explicit validation and runtime construction.

#pragma once

#ifndef SCHEMA_SCHEMA_HPP_INCLUDED
#define SCHEMA_SCHEMA_HPP_INCLUDED

#include "schema/types_wrappers.hpp"
#include "schema/baked_instances.hpp"
#include "schema/baked_bulk_data.hpp"
#include "schema/data_block.hpp"

namespace schema
{

class CSchema;
class CSchemaDocumentQuery;

namespace detail
{
struct STypesCatalogue;
class CSchemaConstruction;
}

class CSchemaHandle
{
public:
    constexpr CSchemaHandle() noexcept = default;
    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value.is_valid(); }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr bool operator==(const CSchemaHandle other) const noexcept { return m_value == other.m_value; }
    [[nodiscard]] constexpr bool operator!=(const CSchemaHandle other) const noexcept { return !(*this == other); }

private:
    friend class CSchema;
    friend class detail::CSchemaConstruction;
    friend class CSchemaDocumentQuery;
    explicit constexpr CSchemaHandle(const detail::SOccurrence value) noexcept : m_value(value) {}
    detail::SOccurrence m_value;
};

//  Queries borrow the original live owner or baked backing. Handles are aggregate
//  occurrences, distinct from the types/instances/bulk role handle families.
class CSchemaDocumentQuery
{
public:
    CSchemaDocumentQuery() noexcept = default;
    explicit CSchemaDocumentQuery(const CLiveDocument& document) noexcept : m_read(document) {}
    explicit CSchemaDocumentQuery(const CBakedDocument& document) noexcept : m_read(document) {}

    [[nodiscard]] bool is_ready() const noexcept { return m_read.is_ready(); }
    [[nodiscard]] CSchemaHandle root() const noexcept { return CSchemaHandle{ m_read.root() }; }
    [[nodiscard]] bool contains(const CSchemaHandle value) const noexcept { return m_read.contains(value.m_value); }
    [[nodiscard]] EDocumentValueKind value_kind(const CSchemaHandle value) const noexcept { return m_read.value_kind(value.m_value); }
    [[nodiscard]] CSchemaHandle object_child(const CSchemaHandle value, const CStringView& name) const noexcept
    {
        return CSchemaHandle{ m_read.object_child(value.m_value, name) };
    }
    [[nodiscard]] CSchemaHandle parent(const CSchemaHandle value) const noexcept { return CSchemaHandle{ m_read.parent(value.m_value) }; }
    [[nodiscard]] CSchemaHandle first_child(const CSchemaHandle value) const noexcept { return CSchemaHandle{ m_read.first_child(value.m_value) }; }
    [[nodiscard]] CSchemaHandle next_sibling(const CSchemaHandle value) const noexcept { return CSchemaHandle{ m_read.next_sibling(value.m_value) }; }
    [[nodiscard]] CSchemaHandle array_at(const CSchemaHandle value, const std::uint32_t index) const noexcept
    {
        return CSchemaHandle{ m_read.array_at(value.m_value, index) };
    }
    [[nodiscard]] std::uint32_t child_count(const CSchemaHandle value) const noexcept { return m_read.child_count(value.m_value); }
    [[nodiscard]] CStringView name(const CSchemaHandle value) const noexcept { return m_read.name(value.m_value); }
    [[nodiscard]] CStringView string_value(const CSchemaHandle value) const noexcept { return m_read.string_value(value.m_value); }
    [[nodiscard]] bool boolean_value(const CSchemaHandle value, bool& result) const noexcept { return m_read.boolean_value(value.m_value, result); }
    [[nodiscard]] bool signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept { return m_read.signed_integer_value(value.m_value, result); }
    [[nodiscard]] bool unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept { return m_read.unsigned_integer_value(value.m_value, result); }
    [[nodiscard]] bool floating_point_value(const CSchemaHandle value, double& result) const noexcept { return m_read.floating_point_value(value.m_value, result); }
    [[nodiscard]] bool integer_metadata(const CSchemaHandle value, CIntegerMetadata& result) const noexcept
    {
        return m_read.integer_metadata(value.m_value, result);
    }
    [[nodiscard]] bool suppresses_newline_escaping(const CSchemaHandle value) const noexcept
    {
        return m_read.suppresses_newline_escaping(value.m_value);
    }

private:
    friend class CSchema;
    detail::CDocumentRead m_read;
};

enum class ESchemaStage : std::uint8_t { none = 0u, structure, types, instances, bulk_data, copying, saving, merging, materialisation };
enum class ESchemaSource : std::uint8_t { none = 0u, types, instances, bulk_data };
enum class ESchemaReason : std::uint8_t
{
    none = 0u,
    invalid_source,
    unknown_property,
    missing_types,
    invalid_types,
    invalid_data,
    incompatible_definition,
    unestablished_compatibility,
    conflicting_value,
    allocation_failed,
    storage_limit,
    construction_failed
};

struct SSchemaDiagnostic
{
    ESchemaStage stage{ ESchemaStage::none };
    ESchemaReason reason{ ESchemaReason::none };
    //  Identifies the input carrying first when composition admission fails.
    ESchemaSource source{ ESchemaSource::none };
    CSchemaHandle first, second;
    SDiagnostic types;
    SInstanceDiagnostic instances;
    SBulkDataDiagnostic bulk_data;
    SDataBlockDiagnostic block;
};

class CSchema
{
public:
    CSchema() noexcept = default;
    CSchema(const CSchema&) = delete;
    CSchema& operator=(const CSchema&) = delete;
    CSchema(CSchema&&) noexcept = default;
    CSchema& operator=(CSchema&&) noexcept = default;

    [[nodiscard]] bool initialise(const std::size_t capacity = 0u) noexcept;
    //  Structural admission permits incomplete authoring content. Copy/compose/merge
    //  stage replacement atomically and permit source/destination aliasing.
    [[nodiscard]] bool try_adopt(CLiveDocument&& document, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool copy_from(const CSchemaDocumentQuery& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool compose_from(const CSchemaDocumentQuery& types, const CSchemaDocumentQuery& instances,
        const CSchemaDocumentQuery& bulk_data, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool merge_from(const CSchemaDocumentQuery& first, const CSchemaDocumentQuery& second, SSchemaDiagnostic& diagnostic) noexcept;
    //  Full validation and runtime construction require contained types; no fallback.
    [[nodiscard]] bool validate(SSchemaDiagnostic& diagnostic) const noexcept;
    //  Generic document save checks admission only. All output owners must be empty.
    [[nodiscard]] bool save(CBakedDocumentBlock& destination, SSchemaDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool materialise(CLiveTypes& types, class CLiveInstances& instances,
        class CLiveBulkData& bulk_data, SSchemaDiagnostic& diagnostic) const noexcept;
    [[nodiscard]] bool bake(class CBakedDataBlock& destination, SSchemaDiagnostic& diagnostic) const noexcept;
    //  Replace this aggregate with only the source role's section; do not merge.
    //  Data imports require usable binary, reconstruct literals without minimising
    //  selections and never include linked types. The source remains unchanged.
    [[nodiscard]] bool import_from(const CLiveTypes& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool import_from(const CBakedTypes& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool import_from(const class CLiveInstances& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool import_from(const CBakedInstances& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool import_from(const class CLiveBulkData& source, SSchemaDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool import_from(const CBakedBulkData& source, SSchemaDiagnostic& diagnostic) noexcept;
    void clear() noexcept { m_document = {}; }

    [[nodiscard]] bool document_ready() const noexcept { return m_document.is_ready(); }
    [[nodiscard]] CSchemaDocumentQuery document_query() const noexcept { return CSchemaDocumentQuery{ m_document }; }
    [[nodiscard]] CTypesDocumentQuery types_query() const noexcept { return CTypesDocumentQuery{ m_document }; }
    [[nodiscard]] CInstanceDocumentQuery instances_query() const noexcept { return CInstanceDocumentQuery{ m_document }; }
    [[nodiscard]] CBulkDataDocumentQuery bulk_data_query() const noexcept { return CBulkDataDocumentQuery{ m_document }; }
    [[nodiscard]] const CLiveDocument& document() const noexcept { return m_document; }
    [[nodiscard]] CLiveDocument& edit() noexcept { return m_document; }

private:
    [[nodiscard]] static bool admit(const detail::CDocumentRead& source, SSchemaDiagnostic& diagnostic,
        detail::STypesCatalogue* catalogue = nullptr) noexcept;
    [[nodiscard]] bool import_types(const CTypesDocumentQuery& source, SSchemaDiagnostic& diagnostic) noexcept;
    template<class TSource>
    [[nodiscard]] bool import_instances(const TSource& source, SSchemaDiagnostic& diagnostic) noexcept;
    template<class TSource>
    [[nodiscard]] bool import_bulk(const TSource& source, SSchemaDiagnostic& diagnostic) noexcept;
    CLiveDocument m_document;
};

}   // namespace schema

#endif // SCHEMA_SCHEMA_HPP_INCLUDED

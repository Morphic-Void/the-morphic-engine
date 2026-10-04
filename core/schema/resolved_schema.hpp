
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resolved_schema.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Move-only resolved schema ownership and read-only runtime observations.

//  Public entry point for schema resolution, observations and C++ generation.
//  Role headers include it; direct resolver consumers may include it alone.

#pragma once

#ifndef RESOLVED_SCHEMA_HPP_INCLUDED
#define RESOLVED_SCHEMA_HPP_INCLUDED

#include "containers/TPodVector.hpp"
#include "schema/document_query.hpp"

namespace schema
{

class CResolvedSchema;
class CResolver;

//==============================================================================
//  Layout limits
//==============================================================================

//  Maximum supported type alignment and base alignment of instance/bulk buffers.
constexpr std::uint32_t k_max_alignment = 128u;

//==============================================================================
//  CSchemaIndex
//==============================================================================

class CSchemaIndex
{
public:
    constexpr CSchemaIndex() noexcept = default;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != 0u; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept { return m_value; }

private:
    explicit constexpr CSchemaIndex(const std::uint32_t value) noexcept : m_value(value) {}

    std::uint32_t m_value{};
    friend class CResolvedSchema;
    friend class CResolver;
};

constexpr bool operator==(const CSchemaIndex a, const CSchemaIndex b) noexcept
{
    return a.query_value() == b.query_value();
}

constexpr bool operator!=(const CSchemaIndex a, const CSchemaIndex b) noexcept
{
    return !(a == b);
}

//==============================================================================
//  Value observations and diagnostics
//==============================================================================

enum class ECategory : std::uint8_t
{
    primitive = 0,
    enumeration,
    structure,
    array,
    bit_structure
};

enum class EPrimitive : std::uint8_t
{
    none = 0,
    i8,
    i16,
    i32,
    i64,
    u8,
    u16,
    u32,
    u64,
    f16,
    f32,
    f64,
    b8
};

enum class EInterpretation : std::uint8_t
{
    ordinary = 0,
    unorm,
    snorm
};

enum class EScalar : std::uint8_t
{
    signed_integer = 0,
    unsigned_integer,
    floating_point,
    half,
    boolean
};

enum class EDefault : std::uint8_t
{
    scalar = 0,
    array,
    structure
};

enum class EStage : std::uint8_t
{
    none = 0,
    declarations,
    references,
    layout,
    defaults,
    generation
};

enum class EReason : std::uint8_t
{
    none = 0,
    invalid_input,
    unknown_property,
    missing_property,
    unknown_type,
    duplicate_declaration,
    invalid_identifier,
    cycle,
    invalid_default,
    invalid_range,
    invalid_layout,
    unsupported_feature,
    storage_limit,
    allocation_failed,
    translation_failed
};

union SScalarValue
{
    std::uint64_t unsigned_value{};
    std::int64_t signed_value;
    double floating_value;
};

struct SScalar
{
    SScalarValue value;
    EScalar kind{ EScalar::unsigned_integer };
};

//  Document handles remain scoped to the caller's input. Diagnostics retain
//  locations but no document storage after resolution failure.
struct SDiagnostic
{
    EReason reason{ EReason::none };
    EStage stage{ EStage::none };
    CSchemaHandle occurrence, enclosing_type, enclosing_member, related;
    bool ranges_available{};
    std::uint64_t range_begin{}, range_end{}, related_begin{}, related_end{};
};

//  Value observations, not the private storage ABI. Wrong-category queries fail.
struct SType
{
    ECategory category{};
    EPrimitive primitive{};
    CPropertyNameId name;
    CSchemaHandle source;
    std::uint64_t size{}, alignment{}, stride{};
    std::uint32_t count{};
    CSchemaIndex element_or_storage;
    bool gaps{}, internal{}, named_components{};
};

struct SMember
{
    CPropertyNameId name;
    CSchemaHandle source;
    CSchemaIndex type, default_description;
    std::uint64_t offset{}, size{};
};

struct SLabel
{
    CPropertyNameId name;
    CSchemaHandle source;
    SScalar value;
};

struct SField
{
    CPropertyNameId name;
    CSchemaHandle source;
    CSchemaIndex type, default_description;
    std::uint64_t mask{};
    std::uint8_t shift{}, width{};
    bool signed_value{};
    EInterpretation interpretation{};
    EPrimitive primitive{};
};

struct SDefault
{
    EDefault kind{};
    CSchemaHandle source;
    SScalar scalar;
    std::uint32_t supplied_count{};
};

struct SRecordSizes
{
    std::size_t type, member, label, field, default_value, mapping;
};

//==============================================================================
//  CResolvedSchema
//==============================================================================

class CResolvedSchema
{
public:
    //  Lifetime and resolution
    CResolvedSchema() noexcept = default;
    CResolvedSchema(CResolvedSchema&& source) noexcept;
    CResolvedSchema& operator=(CResolvedSchema&& source) noexcept;
    CResolvedSchema(const CResolvedSchema&) = delete;
    CResolvedSchema& operator=(const CResolvedSchema&) = delete;
    [[nodiscard]] bool resolve(const CBakedDocument& document, SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(const CLiveDocument& document, SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(const CSchemaDocumentQuery& document, SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_ready; }

    [[nodiscard]] CStringView name(const CPropertyNameId id) const noexcept;

    //  Type catalogue and checked record access
    [[nodiscard]] CSchemaIndex find_type(const CStringView& name) const noexcept;
    [[nodiscard]] std::uint32_t definition_count() const noexcept;
    [[nodiscard]] CSchemaIndex definition_at(const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] bool type(const CSchemaIndex index, SType& result) const noexcept;
    [[nodiscard]] CSchemaIndex member_at(const CSchemaIndex type, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CSchemaIndex find_member(const CSchemaIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] bool member(const CSchemaIndex index, SMember& result) const noexcept;
    [[nodiscard]] CSchemaIndex label_at(const CSchemaIndex type, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CSchemaIndex find_label(const CSchemaIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] CSchemaIndex first_label_for_value(const CSchemaIndex type, const SScalar& value) const noexcept;
    [[nodiscard]] bool label(const CSchemaIndex index, SLabel& result) const noexcept;
    [[nodiscard]] CSchemaIndex field_at(const CSchemaIndex type, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CSchemaIndex find_field(const CSchemaIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] bool field(const CSchemaIndex index, SField& result) const noexcept;

    //  Defaults
    //  Zero description requests implicit defaults; compounds refer callers to
    //  member/element types. No array extent is materialised into default storage.
    [[nodiscard]] bool default_value(const CSchemaIndex type, const CSchemaIndex description, SDefault& result) const noexcept;
    [[nodiscard]] bool default_element(const CSchemaIndex type, const CSchemaIndex description,
        const std::uint32_t element_ordinal, CSchemaIndex& result) const noexcept;

    //  Document correspondence and implementation measurements
    [[nodiscard]] CSchemaIndex map_occurrence(const CSchemaHandle occurrence) const noexcept;
    [[nodiscard]] static SRecordSizes record_sizes() noexcept;

private:
    friend class CLiveSchema;
    void rebind_live_document(const CLiveDocument& document) noexcept;

    struct STypeRecord
    {
        std::uint32_t size{}, stride{};
        CSchemaIndex related;
        std::uint32_t first{}, count{};
        CPropertyNameId name;
        CSchemaHandle source;
        ECategory category{};
        EPrimitive primitive{};
        std::uint8_t alignment_log2{}, control{};

        static constexpr std::uint8_t k_gaps = 1u, k_internal = 2u, k_named_components = 4u;
        static constexpr unsigned k_state_shift = 3u;
        static constexpr std::uint8_t k_state_mask = 3u << k_state_shift;

        [[nodiscard]] constexpr std::uint32_t alignment() const noexcept;
        [[nodiscard]] bool flag(const std::uint8_t mask) const noexcept { return (control & mask) != 0u; }
        void set_flag(const std::uint8_t mask, const bool value) noexcept;
        [[nodiscard]] std::uint8_t state() const noexcept { return (control & k_state_mask) >> k_state_shift; }
        void set_state(const std::uint8_t value) noexcept;
    };

    struct SMemberRecord
    {
        std::uint32_t offset{}, size{};
        CSchemaIndex type, default_description;
        CPropertyNameId name;
        CSchemaHandle source;
    };

    //  Transient ranges borrow the owner's existing vectors. Clear, move and
    //  re-resolution invalidate operation access; nothing is stored per type.
    template<class T> struct SRecordRange
    {
        std::uint32_t first{}, count{};

        [[nodiscard]] const T& at(const TPodVector<T>& records, const std::uint32_t ordinal) const noexcept;
    };

    struct SDefaultRecord
    {
        SScalar scalar;
        CSchemaHandle source;
        CSchemaIndex type;
        std::uint32_t first{}, count{};
    };

    struct SMapping
    {
        CSchemaHandle source;
        CSchemaIndex target;
    };

    static_assert(sizeof(CSchemaHandle) == 16u);
    static_assert(sizeof(STypeRecord) == 48u);
    static_assert(sizeof(SMemberRecord) == 40u);
    static_assert(sizeof(SLabel) == 40u);
    static_assert(sizeof(SField) == 48u);
    static_assert(sizeof(SDefaultRecord) == 48u);
    static_assert(sizeof(SMapping) == 24u);
    static_assert(sizeof(SRecordRange<SMemberRecord>) == 8u);

    static CSchemaIndex index(const std::uint32_t kind, const std::uint32_t ordinal) noexcept;
    static std::uint32_t ordinal(const CSchemaIndex index) noexcept;
    static bool is_kind(const CSchemaIndex index, const std::uint32_t kind, const std::size_t size) noexcept;
    CSchemaIndex lookup_type(const CStringView& name) const noexcept;
    const STypeRecord* type_record(const CSchemaIndex index) const noexcept;
    template<class T> SRecordRange<T> child_range(const CSchemaIndex type, const ECategory category, const TPodVector<T>& records) const noexcept;
    CSchemaIndex child_at(const CSchemaIndex type, const std::uint32_t ordinal, const ECategory category, const std::uint32_t kind) const noexcept;
    CSchemaDocumentQuery m_document;
    TPodVector<STypeRecord> m_types;
    TPodVector<SMemberRecord> m_members;
    TPodVector<SLabel> m_labels;
    TPodVector<SField> m_fields;
    TPodVector<SDefaultRecord> m_defaults;
    TPodVector<SMapping> m_mapping;
    std::uint32_t m_definitions{};
    bool m_ready{};
    friend class CResolver;
};

constexpr std::uint32_t CResolvedSchema::STypeRecord::alignment() const noexcept
{
    return (alignment_log2 <= 31u) ? (std::uint32_t{ 1u } << alignment_log2) : 0u;
}

template<class T>
const T& CResolvedSchema::SRecordRange<T>::at(const TPodVector<T>& records, const std::uint32_t ordinal) const noexcept
{
    return records[first + ordinal];
}

//  Complete UTF-8 source, terminated by one NUL. No partial output on failure.
[[nodiscard]] bool generate_cpp(const CResolvedSchema& schema, const CStringView& name_space, CByteBuffer& output, SDiagnostic& diagnostic) noexcept;

}   //  namespace schema

#endif   //  RESOLVED_SCHEMA_HPP_INCLUDED

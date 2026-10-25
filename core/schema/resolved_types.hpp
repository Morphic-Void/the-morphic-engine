
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resolved_types.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Move-only resolved schema ownership and read-only runtime observations.

//  Public entry point for schema resolution, observations and C++ generation.
//  Role headers include it; direct resolver consumers may include it alone.

#pragma once

#ifndef RESOLVED_TYPES_HPP_INCLUDED
#define RESOLVED_TYPES_HPP_INCLUDED

#include "containers/TPodVector.hpp"
#include "schema/document_query.hpp"

namespace schema
{

class CResolvedTypes;
class CResolver;

//==============================================================================
//  Layout limits
//==============================================================================

//  Maximum supported type alignment and base alignment of instance/bulk data buffers.
constexpr std::uint32_t k_max_alignment = 128u;

//==============================================================================
//  CTypesIndex
//==============================================================================

class CTypesIndex
{
public:
    constexpr CTypesIndex() noexcept = default;

    [[nodiscard]] constexpr bool is_valid() const noexcept { return m_value != 0u; }
    [[nodiscard]] explicit constexpr operator bool() const noexcept { return is_valid(); }
    [[nodiscard]] constexpr std::uint32_t query_value() const noexcept { return m_value; }

private:
    explicit constexpr CTypesIndex(const std::uint32_t value) noexcept : m_value(value) {}

    std::uint32_t m_value{};
    friend class CResolvedTypes;
    friend class CResolver;
};

constexpr bool operator==(const CTypesIndex a, const CTypesIndex b) noexcept
{
    return a.query_value() == b.query_value();
}

constexpr bool operator!=(const CTypesIndex a, const CTypesIndex b) noexcept
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

//  Canonical schema spelling, borrowed from static storage; allocates nothing.
//  None and invalid enum values return an empty view. No resolved owner is needed.
[[nodiscard]] CStringView primitive_name(const EPrimitive primitive) noexcept;

//  General declaration observations have no implied physical layout.
enum class EDeclarationCategory : std::uint8_t
{
    primitive = 0,
    enumeration,
    structure,
    array,
    bit_structure,
    association,
    interface_type
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
    CTypesHandle occurrence, enclosing_type, enclosing_member, related;
    bool ranges_available{};
    std::uint64_t range_begin{}, range_end{}, related_begin{}, related_end{};
};

//  Value observations, not the private storage ABI. Wrong-category queries fail.
struct SType
{
    ECategory category{};
    EPrimitive primitive{};
    CPropertyNameId name;
    CTypesHandle source;
    std::uint64_t size{}, alignment{}, stride{};
    std::uint32_t count{};
    CTypesIndex element_or_storage;
    bool gaps{}, internal{}, named_components{};
};

struct SMember
{
    CPropertyNameId name;
    CTypesHandle source;
    CTypesIndex type, default_description;
    std::uint64_t offset{}, size{};
};

struct SLabel
{
    CPropertyNameId name;
    CTypesHandle source;
    SScalar value;
};

struct SField
{
    CPropertyNameId name;
    CTypesHandle source;
    CTypesIndex type, default_description;
    std::uint64_t mask{};
    std::uint8_t shift{}, width{};
    bool signed_value{};
    EInterpretation interpretation{};
    EPrimitive primitive{};
};

struct SDefault
{
    EDefault kind{};
    CTypesHandle source;
    SScalar scalar;
    std::uint32_t supplied_count{};
};

struct SDecoration
{
    CStringView key, value;
    CTypesHandle source;
};

struct SDeclaration
{
    EDeclarationCategory category{};
    CPropertyNameId name;
    CTypesHandle source;
};

struct SAssociation
{
    CTypesIndex target; // Invalid means an explicitly null target.
};

struct SInterface
{
    std::uint32_t count{}; // Number of declared members, not expanded occurrences.
};

struct SInterfaceMember
{
    CPropertyNameId name;
    CTypesHandle source;
    CTypesIndex declaration;
    std::uint32_t count{ 1u };
};

struct SRecordSizes
{
    std::size_t type, member, label, field, default_value, mapping;
};

//==============================================================================
//  CResolvedTypes
//==============================================================================

class CResolvedTypes
{
public:
    //  Lifetime and resolution
    CResolvedTypes() noexcept = default;
    CResolvedTypes(CResolvedTypes&& source) noexcept;
    CResolvedTypes& operator=(CResolvedTypes&& source) noexcept;
    CResolvedTypes(const CResolvedTypes&) = delete;
    CResolvedTypes& operator=(const CResolvedTypes&) = delete;
    [[nodiscard]] bool resolve(const CBakedDocument& document, SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(const CLiveDocument& document, SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(SDiagnostic& diagnostic) noexcept;
    [[nodiscard]] bool resolve(const CTypesDocumentQuery& document, SDiagnostic& diagnostic) noexcept;
    void clear() noexcept;

    [[nodiscard]] bool is_ready() const noexcept { return m_ready; }

    //  Copy only the types section with canonical integer presentation and
    //  resolved structure size/alignment and array stride/alignment where omitted. Requires
    //  a resolved source and an uninitialised destination. No partial output or
    //  source mutation; suitable for text writing, adoption or document baking.
    [[nodiscard]] bool prepare_output(CLiveDocument& destination, SDiagnostic& diagnostic) const noexcept;

    //  General catalogue: authored physical definitions followed by non-storage
    //  declarations, preserving source order within each group. Built-ins are
    //  available by name but excluded from declaration_count/at.
    //  Wrong-kind/unready queries fail without modifying output observations.
    [[nodiscard]] CTypesIndex find_declaration(const CStringView& name) const noexcept;
    [[nodiscard]] std::uint32_t declaration_count() const noexcept;
    [[nodiscard]] CTypesIndex declaration_at(const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] bool declaration(const CTypesIndex index, SDeclaration& result) const noexcept;
    [[nodiscard]] bool association(const CTypesIndex index, SAssociation& result) const noexcept;
    [[nodiscard]] bool interface_info(const CTypesIndex index, SInterface& result) const noexcept;
    [[nodiscard]] CTypesIndex interface_member_at(const CTypesIndex owner, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CTypesIndex find_interface_member(const CTypesIndex owner, const CStringView& name) const noexcept;
    [[nodiscard]] bool interface_member(const CTypesIndex index, SInterfaceMember& result) const noexcept;

    //  Physical type catalogue and checked record access. Non-storage indices
    //  and names are rejected; genuine empty physical types remain valid.
    [[nodiscard]] CTypesIndex find_type(const CStringView& name) const noexcept;
    [[nodiscard]] CStringView name(const CPropertyNameId id) const noexcept;
    [[nodiscard]] std::uint32_t definition_count() const noexcept;
    [[nodiscard]] CTypesIndex definition_at(const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] bool definition_ordinal(const CTypesIndex type, std::uint32_t& ordinal) const noexcept;
    [[nodiscard]] bool type(const CTypesIndex index, SType& result) const noexcept;
    [[nodiscard]] CTypesIndex member_at(const CTypesIndex type, const std::uint32_t ordinal) const noexcept;

    //  Offset order, with declaration ordinal breaking ties. Includes empty members.
    //  Prepared during resolution; observation allocates nothing.
    [[nodiscard]] CTypesIndex physical_member_at(const CTypesIndex type, const std::uint32_t ordinal) const noexcept;

    [[nodiscard]] CTypesIndex label_at(const CTypesIndex type, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] CTypesIndex field_at(const CTypesIndex type, const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] bool member(const CTypesIndex index, SMember& result) const noexcept;
    [[nodiscard]] bool label(const CTypesIndex index, SLabel& result) const noexcept;
    [[nodiscard]] bool field(const CTypesIndex index, SField& result) const noexcept;
    [[nodiscard]] CTypesIndex find_member(const CTypesIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] bool member_ordinal(const CTypesIndex type, const CTypesIndex member, std::uint32_t& ordinal) const noexcept;
    [[nodiscard]] CTypesIndex find_label(const CTypesIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] CTypesIndex find_field(const CTypesIndex type, const CStringView& name) const noexcept;
    [[nodiscard]] CTypesIndex first_label_for_value(const CTypesIndex type, const SScalar& value) const noexcept;

    //  Local decorations on a declaration, array, member or bit field. Queries
    //  allocate nothing; strings/handles borrow the unchanged source document.
    //  No inheritance. Missing/invalid/end returns false without changing result.
    [[nodiscard]] bool find_decoration(const CTypesIndex owner, const CStringView& key, SDecoration& result) const noexcept;
    [[nodiscard]] bool first_decoration(const CTypesIndex owner, SDecoration& result) const noexcept;
    [[nodiscard]] bool next_decoration(const CTypesHandle current, SDecoration& result) const noexcept;

    //  Defaults
    //  Zero description requests implicit defaults; compounds refer callers to
    //  member/element types. No array extent is materialised into default storage.
    [[nodiscard]] bool default_value(const CTypesIndex type, const CTypesIndex description, SDefault& result) const noexcept;
    [[nodiscard]] bool default_element(const CTypesIndex type, const CTypesIndex description,
        const std::uint32_t element_ordinal, CTypesIndex& result) const noexcept;

    //  Document correspondence and implementation measurements
    [[nodiscard]] CTypesIndex map_occurrence(const CTypesHandle occurrence) const noexcept;
    [[nodiscard]] static SRecordSizes record_sizes() noexcept;

private:
    friend class CLiveTypes;
    friend struct detail::SDocumentQueryAccess;

    struct SNonStorageRecord
    {
        CPropertyNameId name;
        CTypesHandle source;
        CTypesIndex target;
        std::uint32_t first{}, count{};
        EDeclarationCategory category{};
        std::uint8_t state{};
    };

    struct STypeRecord
    {
        std::uint32_t size{}, stride{};
        CTypesIndex related;
        std::uint32_t first{}, count{};
        CPropertyNameId name;
        CTypesHandle source;
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
        CTypesIndex type, default_description;
        CPropertyNameId name;
        CTypesHandle source;
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
        CTypesHandle source;
        CTypesIndex type;
        std::uint32_t first{}, count{};
    };

    struct SMapping
    {
        CTypesHandle source;
        CTypesIndex target;
    };

    static_assert(sizeof(CTypesHandle) == 16u);
    static_assert(sizeof(STypeRecord) == 48u);
    static_assert(sizeof(SMemberRecord) == 40u);
    static_assert(sizeof(SLabel) == 40u);
    static_assert(sizeof(SField) == 48u);
    static_assert(sizeof(SDefaultRecord) == 48u);
    static_assert(sizeof(SMapping) == 24u);
    static_assert(sizeof(SRecordRange<SMemberRecord>) == 8u);

    static CTypesIndex index(const std::uint32_t kind, const std::uint32_t ordinal) noexcept;
    static std::uint32_t ordinal(const CTypesIndex index) noexcept;
    static bool is_kind(const CTypesIndex index, const std::uint32_t kind, const std::size_t size) noexcept;
    void rebind_live_document(const CLiveDocument& document) noexcept;
    const STypeRecord* type_record(const CTypesIndex index) const noexcept;
    CTypesIndex lookup_type(const CStringView& name) const noexcept;
    CTypesIndex lookup_declaration(const CStringView& name) const noexcept;
    const SNonStorageRecord* non_storage_record(const CTypesIndex index) const noexcept;
    CTypesIndex child_at(const CTypesIndex type, const std::uint32_t ordinal, const ECategory category, const std::uint32_t kind) const noexcept;
    CTypesHandle decoration_object(const CTypesIndex owner) const noexcept;
    bool read_decoration(const CTypesHandle source, SDecoration& result) const noexcept;
    template<class T> SRecordRange<T> child_range(const CTypesIndex type, const ECategory category, const TPodVector<T>& records) const noexcept;
    CTypesDocumentQuery m_document;
    TPodVector<STypeRecord> m_types;
    TPodVector<std::uint32_t> m_type_names;
    TPodVector<SMemberRecord> m_members;
    TPodVector<std::uint32_t> m_member_names;
    TPodVector<std::uint32_t> m_member_order; //  Physical-order ordinals in each type's member range.
    TPodVector<SLabel> m_labels;
    TPodVector<std::uint32_t> m_label_names;
    TPodVector<std::uint32_t> m_label_values; //  Numeric order, with the first declaration winning aliases.
    TPodVector<SField> m_fields;
    TPodVector<std::uint32_t> m_field_names;
    TPodVector<SDefaultRecord> m_defaults;
    TPodVector<SMapping> m_mapping;
    TPodVector<SNonStorageRecord> m_non_storage;
    TPodVector<std::uint32_t> m_non_storage_names;
    TPodVector<SInterfaceMember> m_interface_members;
    TPodVector<std::uint32_t> m_interface_member_names;
    std::uint32_t m_definitions{};
    bool m_ready{};
    friend class CResolver;
};

//==============================================================================
//  Internal record access
//==============================================================================

constexpr std::uint32_t CResolvedTypes::STypeRecord::alignment() const noexcept
{
    return (alignment_log2 <= 31u) ? (std::uint32_t{ 1u } << alignment_log2) : 0u;
}

template<class T>
const T& CResolvedTypes::SRecordRange<T>::at(const TPodVector<T>& records, const std::uint32_t ordinal) const noexcept
{
    return records[first + ordinal];
}

//==============================================================================
//  C++ declaration generation
//==============================================================================

//  Complete UTF-8 source, terminated by one NUL. No partial output on failure.
[[nodiscard]] bool generate_cpp(const CResolvedTypes& schema, const CStringView& name_space, CByteBuffer& output, SDiagnostic& diagnostic) noexcept;

}   //  namespace schema

#endif   //  RESOLVED_TYPES_HPP_INCLUDED


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resolved_schema.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Validate baked definitions and resolve references, defaults and natural layout.

#include "schema/resolved_schema.hpp"
#include "memory/memory_policies.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <limits>
#include <utility>

#include "types/fp16data_t.hpp"

namespace schema
{

namespace resolver_util
{

//  Scalar helpers and index constants are local to this translation unit.
static constexpr unsigned k_index_kind_shift = 29u;
static constexpr std::uint32_t k_limit = (std::uint32_t{ 1u } << k_index_kind_shift) - 1u;
static constexpr std::uint32_t k_type = 0u, k_member = 1u, k_label = 2u, k_field = 3u, k_default = 4u;
static constexpr unsigned k_max_resolution_depth = 256u;
static constexpr const char* k_primitives[] = { "", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f16", "f32", "f64", "b8" };
static constexpr std::uint8_t k_sizes[] = { 0, 1, 2, 4, 8, 1, 2, 4, 8, 2, 4, 8, 1 };

//  Table entry zero represents EPrimitive::none and has no runtime type record.
//  Named definitions follow the built-in primitive records in m_types.
static constexpr std::uint32_t k_primitive_count = static_cast<std::uint32_t>((sizeof(k_primitives) / sizeof(k_primitives[0])) - 1u);
static_assert((sizeof(k_sizes) / sizeof(k_sizes[0])) == (k_primitive_count + 1u));
static_assert(static_cast<std::uint32_t>(EPrimitive::b8) == k_primitive_count);

static bool equal(const CStringView& a, const CStringView& b) noexcept
{
    return a.length() == b.length() && (a.length() == 0u || std::memcmp(a.string(), b.string(), a.length()) == 0);
}

static bool equal(const CStringView& a, const char* const b) noexcept
{
    return equal(a, CStringView{ b });
}

static bool integer(const EPrimitive p) noexcept
{
    return p >= EPrimitive::i8 && p <= EPrimitive::u64;
}

static bool signed_integer(const EPrimitive p) noexcept
{
    return p >= EPrimitive::i8 && p <= EPrimitive::i64;
}

static std::uint64_t max_unsigned(const unsigned width) noexcept
{
    return width == 64u ? UINT64_MAX : (UINT64_C(1) << width) - 1u;
}

static bool fits(const SScalar& value, const bool is_signed, const unsigned width) noexcept
{
    if (is_signed)
    {
        const std::int64_t high = (width == 64u) ? INT64_MAX : static_cast<std::int64_t>((UINT64_C(1) << (width - 1u)) - 1u);
        const std::int64_t low = -high - 1;
        return value.kind == EScalar::signed_integer ?
            ((value.value.signed_value >= low) && (value.value.signed_value <= high))
            : (value.value.unsigned_value <= static_cast<std::uint64_t>(high));
    }
    return value.kind == EScalar::signed_integer ?
        ((value.value.signed_value >= 0) && (static_cast<std::uint64_t>(value.value.signed_value) <= max_unsigned(width)))
        : (value.value.unsigned_value <= max_unsigned(width));
}

static bool scalar_equal(const SScalar& a, const SScalar& b) noexcept
{
    if (a.kind == b.kind)
    {
        if (a.kind == EScalar::signed_integer)
        {
            return a.value.signed_value == b.value.signed_value;
        }
        if (a.kind == EScalar::floating_point)
        {
            return a.value.floating_value == b.value.floating_value;
        }
        return a.value.unsigned_value == b.value.unsigned_value;
    }
    if ((a.kind == EScalar::signed_integer) && (b.kind == EScalar::unsigned_integer))
    {
        return (a.value.signed_value >= 0) && (static_cast<std::uint64_t>(a.value.signed_value) == b.value.unsigned_value);
    }
    if ((b.kind == EScalar::signed_integer) && (a.kind == EScalar::unsigned_integer))
    {
        return scalar_equal(b, a);
    }
    return false;
}

static std::uint64_t occurrence_key(const CSchemaHandle handle) noexcept
{
    return detail::SSchemaHandleAccess::occurrence(handle).query_value();
}

}   // namespace resolver_util

//  Shared by resolution and export; identifiers are ASCII C++17 identifiers.
bool valid_identifier(const CStringView& name) noexcept
{
    const auto alpha = [](const std::uint8_t c)
    {
        return ((c >= 'a') && (c <= 'z')) || ((c >= 'A') && (c <= 'Z')) || (c == '_');
    };
    if ((name.length() == 0u) || !alpha(name.string()[0]))
    {
        return false;
    }

    //  Generated declarations live at namespace/class scope. Reserve the C++
    //  implementation namespace, including double underscores anywhere.
    if ((name.length() > 1u) && (name.string()[0] == '_') &&
        (((name.string()[1] >= 'A') && (name.string()[1] <= 'Z')) || (name.string()[1] == '_')))
    {
        return false;
    }
    for (std::size_t i = 1u; i < name.length(); ++i)
    {
        const std::uint8_t c = name.string()[i];
        if (!alpha(c) && !((c >= '0') && (c <= '9')))
        {
            return false;
        }
        if ((c == '_') && (name.string()[i - 1u] == '_'))
        {
            return false;
        }
    }
    constexpr const char* keywords[] = { "alignas", "alignof", "and", "and_eq", "asm", "auto", "bitand", "bitor",
        "bool", "break", "case", "catch", "char", "char16_t", "char32_t", "class", "compl", "const", "constexpr",
        "const_cast", "continue", "decltype", "default", "delete", "do", "double", "dynamic_cast", "else", "enum",
        "explicit", "export", "extern", "false", "float", "for", "friend", "goto", "if", "inline", "int", "long",
        "mutable", "namespace", "new", "noexcept", "not", "not_eq", "nullptr", "operator", "or", "or_eq", "private",
        "protected", "public", "register", "reinterpret_cast", "return", "short", "signed", "sizeof", "static",
        "static_assert", "static_cast", "struct", "switch", "template", "this", "thread_local", "throw", "true", "try",
        "typedef", "typeid", "typename", "union", "unsigned", "using", "virtual", "void", "volatile", "wchar_t",
        "while", "xor", "xor_eq" };
    for (const char* const keyword : keywords)
    {
        if (resolver_util::equal(name, keyword))
        {
            return false;
        }
    }
    return true;
}

//==============================================================================
//  CResolvedSchema: ownership and read-only access
//==============================================================================

void CResolvedSchema::STypeRecord::set_flag(const std::uint8_t mask, const bool value) noexcept
{
    control = static_cast<std::uint8_t>((control & ~mask) | (value ? mask : 0u));
}

void CResolvedSchema::STypeRecord::set_state(const std::uint8_t value) noexcept
{
    control = static_cast<std::uint8_t>((control & ~k_state_mask) | (value << k_state_shift));
}

CSchemaIndex CResolvedSchema::index(const std::uint32_t kind, const std::uint32_t ordinal) noexcept
{
    return CSchemaIndex{ (kind << resolver_util::k_index_kind_shift) | (ordinal + 1u) };
}

std::uint32_t CResolvedSchema::ordinal(const CSchemaIndex i) noexcept
{
    return (i.query_value() & resolver_util::k_limit) - 1u;
}

bool CResolvedSchema::is_kind(const CSchemaIndex i, const std::uint32_t kind, const std::size_t size) noexcept
{
    return i.is_valid() && ((i.query_value() >> resolver_util::k_index_kind_shift) == kind) && (ordinal(i) < size);
}

CResolvedSchema::CResolvedSchema(CResolvedSchema&& source) noexcept
{
    *this = std::move(source);
}

CResolvedSchema& CResolvedSchema::operator=(CResolvedSchema&& source) noexcept
{
    if (this != &source)
    {
        clear();
        m_document = source.m_document;
        m_types = std::move(source.m_types);
        m_members = std::move(source.m_members);
        m_labels = std::move(source.m_labels);
        m_fields = std::move(source.m_fields);
        m_defaults = std::move(source.m_defaults);
        m_mapping = std::move(source.m_mapping);
        m_definitions = source.m_definitions;
        m_ready = source.m_ready;
        source.clear();
    }
    return *this;
}

void CResolvedSchema::clear() noexcept
{
    m_ready = false;
    m_definitions = 0u;
    m_document = {};
    m_types.deallocate();
    m_members.deallocate();
    m_labels.deallocate();
    m_fields.deallocate();
    m_defaults.deallocate();
    m_mapping.deallocate();
}

void CResolvedSchema::rebind_live_document(const CLiveDocument& document) noexcept
{
    if (m_ready)
    {
        m_document = CSchemaDocumentQuery{ document };
    }
}

const CResolvedSchema::STypeRecord* CResolvedSchema::type_record(const CSchemaIndex i) const noexcept
{
    return is_kind(i, resolver_util::k_type, m_types.size()) ? &m_types[ordinal(i)] : nullptr;
}

CSchemaIndex CResolvedSchema::lookup_type(const CStringView& name) const noexcept
{
    for (std::uint32_t type_slot = 1u; type_slot <= resolver_util::k_primitive_count; ++type_slot)
    {
        if (resolver_util::equal(name, resolver_util::k_primitives[type_slot]))
        {
            return index(resolver_util::k_type, (type_slot - 1u));
        }
    }
    for (std::uint32_t type_slot = resolver_util::k_primitive_count; type_slot < (resolver_util::k_primitive_count + m_definitions); ++type_slot)
    {
        if (resolver_util::equal(name, m_document.property_name(m_types[type_slot].name)))
        {
            return index(resolver_util::k_type, type_slot);
        }
    }
    return {};
}

CSchemaIndex CResolvedSchema::find_type(const CStringView& name) const noexcept
{
    return m_ready ? lookup_type(name) : CSchemaIndex{};
}

CStringView CResolvedSchema::name(const CPropertyNameId id) const noexcept
{
    return m_ready ? m_document.property_name(id) : CStringView{};
}

std::uint32_t CResolvedSchema::definition_count() const noexcept
{
    return m_ready ? m_definitions : 0u;
}

CSchemaIndex CResolvedSchema::definition_at(const std::uint32_t n) const noexcept
{
    return (m_ready && (n < m_definitions)) ? index(resolver_util::k_type, (resolver_util::k_primitive_count + n)) : CSchemaIndex{};
}

bool CResolvedSchema::type(const CSchemaIndex index, SType& result) const noexcept
{
    const STypeRecord* const record = type_record(index);
    if (!m_ready || !record)
    {
        return false;
    }
    result = {
        record->category, record->primitive, record->name, record->source, record->size,
        record->alignment(), record->stride, record->count, record->related,
        record->flag(STypeRecord::k_gaps), record->flag(STypeRecord::k_internal), record->flag(STypeRecord::k_named_components) };
    return true;
}

CSchemaIndex CResolvedSchema::child_at(
    const CSchemaIndex i, const std::uint32_t n, const ECategory category, const std::uint32_t kind) const noexcept
{
    const STypeRecord* const r = type_record(i);
    return (m_ready && r && (r->category == category) && (n < r->count)) ? index(kind, (r->first + n)) : CSchemaIndex{};
}

CSchemaIndex CResolvedSchema::member_at(const CSchemaIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::structure, resolver_util::k_member);
}

CSchemaIndex CResolvedSchema::label_at(const CSchemaIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::enumeration, resolver_util::k_label);
}

CSchemaIndex CResolvedSchema::field_at(const CSchemaIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::bit_structure, resolver_util::k_field);
}

bool CResolvedSchema::member(const CSchemaIndex i, SMember& r) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_member, m_members.size()))
    {
        return false;
    }
    const SMemberRecord& record = m_members[ordinal(i)];
    r = { record.name, record.source, record.type, record.default_description, record.offset, record.size };
    return true;
}

bool CResolvedSchema::label(const CSchemaIndex i, SLabel& r) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_label, m_labels.size()))
    {
        return false;
    }
    r = m_labels[ordinal(i)];
    return true;
}

bool CResolvedSchema::field(const CSchemaIndex i, SField& r) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_field, m_fields.size()))
    {
        return false;
    }
    r = m_fields[ordinal(i)];
    return true;
}

template <class T>
CResolvedSchema::SRecordRange<T> CResolvedSchema::child_range(const CSchemaIndex type, const ECategory category, const TPodVector<T>& records) const noexcept
{
    const STypeRecord* const parent = type_record(type);
    if (!m_ready || !parent || (parent->category != category) ||
        (parent->first > records.size()) || (parent->count > (records.size() - parent->first)))
    {
        return {};
    }
    return { parent->first, parent->count };
}

CSchemaIndex CResolvedSchema::find_member(const CSchemaIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SMemberRecord> members = child_range(type, ECategory::structure, m_members);
    for (std::uint32_t member_ordinal = 0u; member_ordinal < members.count; ++member_ordinal)
    {
        const SMemberRecord& member = members.at(m_members, member_ordinal);
        if (resolver_util::equal(name, m_document.property_name(member.name)))
        {
            return index(resolver_util::k_member, (members.first + member_ordinal));
        }
    }
    return {};
}

CSchemaIndex CResolvedSchema::find_label(const CSchemaIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SLabel> labels = child_range(type, ECategory::enumeration, m_labels);
    for (std::uint32_t label_ordinal = 0u; label_ordinal < labels.count; ++label_ordinal)
    {
        const SLabel& label = labels.at(m_labels, label_ordinal);
        if (resolver_util::equal(name, m_document.property_name(label.name)))
        {
            return index(resolver_util::k_label, (labels.first + label_ordinal));
        }
    }
    return {};
}

CSchemaIndex CResolvedSchema::find_field(const CSchemaIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SField> fields = child_range(type, ECategory::bit_structure, m_fields);
    for (std::uint32_t field_ordinal = 0u; field_ordinal < fields.count; ++field_ordinal)
    {
        const SField& field = fields.at(m_fields, field_ordinal);
        if (resolver_util::equal(name, m_document.property_name(field.name)))
        {
            return index(resolver_util::k_field, (fields.first + field_ordinal));
        }
    }
    return {};
}

CSchemaIndex CResolvedSchema::first_label_for_value(const CSchemaIndex type, const SScalar& value) const noexcept
{
    const SRecordRange<SLabel> labels = child_range(type, ECategory::enumeration, m_labels);
    for (std::uint32_t label_ordinal = 0u; label_ordinal < labels.count; ++label_ordinal)
    {
        const SLabel& label = labels.at(m_labels, label_ordinal);
        if (resolver_util::scalar_equal(value, label.value))
        {
            return index(resolver_util::k_label, (labels.first + label_ordinal));
        }
    }
    return {};
}

bool CResolvedSchema::default_value(const CSchemaIndex type, const CSchemaIndex description, SDefault& result) const noexcept
{
    const STypeRecord* const type_info = type_record(type);
    if (!m_ready || !type_info)
    {
        return false;
    }
    SDefault value;
    value.kind = (type_info->category == ECategory::array) ? EDefault::array :
        (((type_info->category == ECategory::structure) || (type_info->category == ECategory::bit_structure))
        ? EDefault::structure : EDefault::scalar);
    if (description)
    {
        if (!is_kind(description, resolver_util::k_default, m_defaults.size()))
        {
            return false;
        }
        const SDefaultRecord& default_info = m_defaults[ordinal(description)];
        if (default_info.type != type)
        {
            return false;
        }
        value.source = default_info.source;
        value.scalar = default_info.scalar;
        value.supplied_count = default_info.count;
    }
    else if (type_info->category == ECategory::enumeration)
    {
        value.scalar = m_labels[type_info->first].value;
    }
    else if (type_info->category == ECategory::primitive)
    {
        if (resolver_util::signed_integer(type_info->primitive))
        {
            value.scalar.value.signed_value = 0;
            value.scalar.kind = EScalar::signed_integer;
        }
        else if ((type_info->primitive == EPrimitive::f32) || (type_info->primitive == EPrimitive::f64))
        {
            value.scalar.value.floating_value = 0.0;
            value.scalar.kind = EScalar::floating_point;
        }
        else
        {
            value.scalar.value.unsigned_value = 0u;
            value.scalar.kind = (type_info->primitive == EPrimitive::f16) ? EScalar::half :
                ((type_info->primitive == EPrimitive::b8) ? EScalar::boolean : EScalar::unsigned_integer);
        }
    }
    result = value;
    return true;
}

bool CResolvedSchema::default_element(const CSchemaIndex type, const CSchemaIndex description, const std::uint32_t element_ordinal, CSchemaIndex& result) const noexcept
{
    const STypeRecord* const type_info = type_record(type);
    SDefault value;
    if (!default_value(type, description, value) || (type_info->category != ECategory::array) || (element_ordinal >= type_info->count))
    {
        return false;
    }
    result = (element_ordinal < value.supplied_count) ? index(resolver_util::k_default, (m_defaults[ordinal(description)].first + element_ordinal)) : CSchemaIndex{};
    return true;
}

CSchemaIndex CResolvedSchema::map_occurrence(const CSchemaHandle occurrence) const noexcept
{
    if (!m_ready || !occurrence)
    {
        return {};
    }
    std::size_t low = 0u, high = m_mapping.size();
    while (low < high)
    {
        const std::size_t mid = low + ((high - low) / 2u);
        if (resolver_util::occurrence_key(m_mapping[mid].source) < resolver_util::occurrence_key(occurrence))
        {
            low = mid + 1u;
        }
        else
        {
            high = mid;
        }
    }
    return ((low < m_mapping.size()) && (m_mapping[low].source == occurrence)) ? m_mapping[low].target : CSchemaIndex{};
}

SRecordSizes CResolvedSchema::record_sizes() noexcept
{   //  Test the full encoding range, including the unsigned 2 GiB alignment.
    constexpr STypeRecord unit_alignment{};
    constexpr STypeRecord high_alignment{ 0u, 0u, {}, 0u, 0u, {}, {}, {}, {}, 31u, 0u };
    constexpr STypeRecord invalid_alignment{ 0u, 0u, {}, 0u, 0u, {}, {}, {}, {}, 32u, 0u };
    static_assert(unit_alignment.alignment() == 1u);
    static_assert(high_alignment.alignment() == 0x80000000u);
    static_assert(invalid_alignment.alignment() == 0u);
    return { sizeof(STypeRecord), sizeof(SMemberRecord), sizeof(SLabel), sizeof(SField), sizeof(SDefaultRecord), sizeof(SMapping) };
}

//==============================================================================
//  CResolver: declaration validation and runtime resolution
//==============================================================================

class CResolver
{
public:
    CResolver(CResolvedSchema& owner, SDiagnostic& diagnostic) noexcept;
    bool run() noexcept;

private:
    using TTypeRecord = CResolvedSchema::STypeRecord;

    //  First-error reporting, checked storage and document shape.
    bool fail(const EReason reason, const CSchemaHandle& at = {}, const CSchemaHandle& related = {}) noexcept;
    template <class T> bool grow(TPodVector<T>& values, const std::uint32_t count, const CSchemaHandle& at) noexcept;
    bool map(const CSchemaHandle& at, const CSchemaIndex target) noexcept;
    CSchemaHandle property(const CSchemaHandle& at, const char* const name) const noexcept;
    bool shape(
        const CSchemaHandle& source, const std::initializer_list<const char*> allowed,
        const std::initializer_list<const char*> required = {}) noexcept;
    bool declarations(const CSchemaHandle& container, const ECategory category) noexcept;
    bool names(const CSchemaHandle& container, const bool array, const CSchemaHandle& owner, const bool allow_empty = false) noexcept;

    //  References, scalar/default conversion and category layout.
    bool resolve_type(const CSchemaIndex type_index, const unsigned depth) noexcept;
    bool reference(const CSchemaHandle& source, CSchemaIndex& result, const unsigned depth) noexcept;
    bool number(const CSchemaHandle& source, const bool is_signed, const unsigned bit_width, SScalar& result) noexcept;
    bool extent(const CSchemaHandle& at, std::uint64_t& out) noexcept;
    bool scalar(const CSchemaIndex type, const CSchemaHandle& source, SScalar& result) noexcept;
    bool default_record(const CSchemaIndex type, const CSchemaHandle& source, const std::uint32_t slot, const unsigned depth) noexcept;
    bool make_default(const CSchemaIndex type, const CSchemaHandle& source, CSchemaIndex& result, const unsigned depth) noexcept;
    bool enumeration(TTypeRecord& type_record) noexcept;
    bool structure(TTypeRecord& type_record, const unsigned depth) noexcept;
    bool structure_layout(TTypeRecord& type_record, const bool explicit_offsets, const bool has_storage) noexcept;
    bool bit_structure(TTypeRecord& type_record, const unsigned depth) noexcept;
    bool storage(TTypeRecord& type_record) noexcept;
    bool add(const std::uint64_t a, const std::uint64_t b, std::uint64_t& out, const CSchemaHandle& at) noexcept;
    bool align(const std::uint64_t value, const std::uint64_t alignment, std::uint64_t& out, const CSchemaHandle& at) noexcept;

    //  Borrowed owner/input and current diagnostic context.
    CResolvedSchema& m_schema;
    const CSchemaDocumentQuery& m_document;
    SDiagnostic& m_diagnostic;
    EStage m_stage{ EStage::declarations };
    CSchemaHandle m_context_type, m_context_member;
};

CResolver::CResolver(CResolvedSchema& owner, SDiagnostic& diagnostic) noexcept
    : m_schema(owner), m_document(owner.m_document), m_diagnostic(diagnostic)
{
}

bool CResolver::fail(const EReason reason, const CSchemaHandle& at, const CSchemaHandle& related) noexcept
{
    if (m_diagnostic.reason == EReason::none)
    {
        m_diagnostic.reason = reason;
        m_diagnostic.stage = m_stage;
        m_diagnostic.occurrence = at;
        m_diagnostic.related = related;
        m_diagnostic.enclosing_type = m_context_type;
        m_diagnostic.enclosing_member = m_context_member;
    }
    return false;
}

template <class T>
bool CResolver::grow(TPodVector<T>& values, const std::uint32_t count, const CSchemaHandle& at) noexcept
{
    if ((count > resolver_util::k_limit - values.size()) || (count > SIZE_MAX / sizeof(T) - values.size()))
    {
        return fail(EReason::storage_limit, at);
    }
    for (std::uint32_t n = 0u; n < count; ++n)
    {
        if (!values.push_back(T{}))
        {
            return fail(EReason::allocation_failed, at);
        }
    }
    return true;
}

bool CResolver::map(const CSchemaHandle& at, const CSchemaIndex target) noexcept
{
    if (!grow(m_schema.m_mapping, 1u, at))
    {
        return false;
    }
    m_schema.m_mapping.last() = { at, target };
    return true;
}

CSchemaHandle CResolver::property(const CSchemaHandle& at, const char* const name) const noexcept
{
    return m_document.object_child(at, CStringView{ name });
}

bool CResolver::add(const std::uint64_t a, const std::uint64_t b, std::uint64_t& out, const CSchemaHandle& at) noexcept
{
    if ((b > UINT64_MAX - a) || (a + b > memory::k_byte_size_ceiling))
    {
        return fail(EReason::storage_limit, at);
    }
    out = a + b;
    return true;
}

bool CResolver::align(const std::uint64_t value, const std::uint64_t alignment, std::uint64_t& out, const CSchemaHandle& at) noexcept
{
    if (!alignment || (alignment > memory::k_byte_size_ceiling) || (alignment & (alignment - 1u)))
    {
        return fail(EReason::invalid_layout, at);
    }
    const std::uint64_t padding = (alignment - (value & (alignment - 1u))) & (alignment - 1u);
    return add(value, padding, out, at);
}

bool CResolver::shape(const CSchemaHandle& source, const std::initializer_list<const char*> allowed, const std::initializer_list<const char*> required) noexcept
{
    if (!source)
    {
        return fail(EReason::missing_property, source);
    }
    if (m_document.value_kind(source) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, source);
    }
    for (CSchemaHandle child = m_document.first_child(source); child; child = m_document.next_sibling(child))
    {
        bool found = false;
        for (const char* const key : allowed)
        {
            if (resolver_util::equal(m_document.name(child), key))
            {
                found = true;
            }
        }
        if (!found)
        {
            return fail(EReason::unknown_property, child);
        }
        for (CSchemaHandle prior = m_document.first_child(source); prior != child; prior = m_document.next_sibling(prior))
        {
            if (m_document.name_id(prior) == m_document.name_id(child))
            {
                return fail(EReason::duplicate_declaration, child, prior);
            }
        }
    }
    for (const char* const key : required)
    {
        if (!property(source, key))
        {
            return fail(EReason::missing_property, source);
        }
    }
    return true;
}

bool CResolver::names(const CSchemaHandle& container, const bool array, const CSchemaHandle& owner, const bool allow_empty) noexcept
{
    (void)owner;
    if ((m_document.value_kind(container) != (array ? EDocumentValueKind::array : EDocumentValueKind::object)) ||
        (!allow_empty && (m_document.child_count(container) == 0u)))
    {
        return fail(EReason::invalid_input, container);
    }
    for (CSchemaHandle declaration = m_document.first_child(container); declaration; declaration = m_document.next_sibling(declaration))
    {
        if (!valid_identifier(m_document.name(declaration)))
        {
            return fail(EReason::invalid_identifier, declaration);
        }
        for (CSchemaHandle prior = m_document.first_child(container); prior != declaration;
            prior = m_document.next_sibling(prior))
        {
            if (m_document.name_id(prior) == m_document.name_id(declaration))
            {
                return fail(EReason::duplicate_declaration, declaration, prior);
            }
        }
    }
    return true;
}

bool CResolver::declarations(const CSchemaHandle& container, const ECategory category) noexcept
{
    if (m_document.value_kind(container) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, container);
    }
    for (CSchemaHandle declaration = m_document.first_child(container); declaration; declaration = m_document.next_sibling(declaration))
    {
        m_context_type = declaration;
        if (!valid_identifier(m_document.name(declaration)))
        {
            return fail(EReason::invalid_identifier, declaration);
        }
        const CSchemaIndex existing = m_schema.lookup_type(m_document.name(declaration));
        if (existing)
        {
            return fail(EReason::duplicate_declaration, declaration, m_schema.type_record(existing)->source);
        }
        if (!grow(m_schema.m_types, 1u, declaration))
        {
            return false;
        }
        TTypeRecord& type_record = m_schema.m_types.last();
        type_record.name = m_document.name_id(declaration);
        type_record.source = declaration;
        type_record.category = category;
        ++m_schema.m_definitions;
        if (!map(declaration, m_schema.index(resolver_util::k_type, static_cast<std::uint32_t>(m_schema.m_types.size() - 1u))))
        {
            return false;
        }
    }
    m_context_type = {};
    return true;
}

bool CResolver::run() noexcept
{
    if (!m_document.is_ready() || !shape(m_document.root(), { "types", "instances", "data" }, { "types" }))
    {
        return fail(EReason::invalid_input, m_document.root());
    }
    for (const char* const name : { "instances", "data" })
    {
        const CSchemaHandle section = property(m_document.root(), name);
        if (section && (m_document.value_kind(section) != EDocumentValueKind::object))
        {
            return fail(EReason::invalid_input, section);
        }
    }
    const CSchemaHandle types = property(m_document.root(), "types");
    if (!shape(types, { "enumerations", "structures", "bit_structures" }))
    {
        return false;
    }
    if (!grow(m_schema.m_types, resolver_util::k_primitive_count, types))
    {
        return false;
    }
    for (std::uint32_t type_ordinal = 0u; type_ordinal < resolver_util::k_primitive_count; ++type_ordinal)
    {
        TTypeRecord& primitive_record = m_schema.m_types[type_ordinal];
        primitive_record.primitive = static_cast<EPrimitive>(type_ordinal + 1u);
        primitive_record.size = primitive_record.stride = resolver_util::k_sizes[type_ordinal + 1u];
        for (std::uint32_t alignment = primitive_record.size; alignment > 1u; alignment >>= 1u)
        {
            ++primitive_record.alignment_log2;
        }
        primitive_record.set_state(2u);
    }
    for (CSchemaHandle declaration_group = m_document.first_child(types); declaration_group; declaration_group = m_document.next_sibling(declaration_group))
    {
        const ECategory category = resolver_util::equal(m_document.name(declaration_group), "enumerations") ? ECategory::enumeration :
            (resolver_util::equal(m_document.name(declaration_group), "structures") ? ECategory::structure : ECategory::bit_structure);
        if (!declarations(declaration_group, category))
        {
            return false;
        }
    }
    for (std::uint32_t type_ordinal = 0u; type_ordinal < m_schema.m_definitions; ++type_ordinal)
    {
        if (!resolve_type(m_schema.index(resolver_util::k_type, (type_ordinal + resolver_util::k_primitive_count)), 0u))
        {
            return false;
        }
    }
    if (m_schema.m_mapping.size() > 1u)
    {
        std::sort(m_schema.m_mapping.data(), (m_schema.m_mapping.data() + m_schema.m_mapping.size()),
            [](const CResolvedSchema::SMapping& a, const CResolvedSchema::SMapping& b)
            {
                return resolver_util::occurrence_key(a.source) < resolver_util::occurrence_key(b.source);
            });
    }
    return true;
}

bool CResolver::number(const CSchemaHandle& source, const bool is_signed, const unsigned bit_width, SScalar& result) noexcept
{
    SScalar value;
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    if (m_document.signed_integer_value(source, signed_value))
    {
        value.value.signed_value = signed_value;
        value.kind = EScalar::signed_integer;
    }
    else if (m_document.unsigned_integer_value(source, unsigned_value))
    {
        value.value.unsigned_value = unsigned_value;
        value.kind = EScalar::unsigned_integer;
    }
    else
    {
        double floating_value{};
        if (!m_document.floating_point_value(source, floating_value) || !std::isfinite(floating_value) || (std::trunc(floating_value) != floating_value))
        {
            return fail(EReason::invalid_range, source);
        }

        //  Power-of-two exclusive upper bounds are exact in binary64, including
        //  2^63 and 2^64; rounded INT64_MAX/UINT64_MAX are unsafe cast guards.
        if (is_signed)
        {
            const double bound = std::ldexp(1.0, (bit_width - 1u));
            if ((floating_value < -bound) || (floating_value >= bound))
            {
                return fail(EReason::invalid_range, source);
            }
            value.kind = EScalar::signed_integer;
            value.value.signed_value = static_cast<std::int64_t>(floating_value);
        }
        else
        {
            if ((floating_value < 0.0) || (floating_value >= std::ldexp(1.0, bit_width)))
            {
                return fail(EReason::invalid_range, source);
            }
            value.value.unsigned_value = static_cast<std::uint64_t>(floating_value);
        }
    }
    if (!resolver_util::fits(value, is_signed, bit_width))
    {
        return fail(EReason::invalid_range, source);
    }
    if (is_signed && (value.kind != EScalar::signed_integer))
    {
        value.value.signed_value = static_cast<std::int64_t>(value.value.unsigned_value);
        value.kind = EScalar::signed_integer;
    }
    if (!is_signed && (value.kind == EScalar::signed_integer))
    {
        value.value.unsigned_value = static_cast<std::uint64_t>(value.value.signed_value);
        value.kind = EScalar::unsigned_integer;
    }
    result = value;
    return true;
}

bool CResolver::extent(const CSchemaHandle& at, std::uint64_t& out) noexcept
{
    SScalar value;
    if (!number(at, false, 64u, value))
    {
        return false;
    }
    out = value.value.unsigned_value;
    return true;
}

bool CResolver::storage(TTypeRecord& type_record) noexcept
{
    const CSchemaHandle storage_source = property(type_record.source, "storage");
    type_record.related = m_schema.lookup_type(m_document.string_value(storage_source));
    const TTypeRecord* const storage_record = m_schema.type_record(type_record.related);
    if (!storage_record || (storage_record->category != ECategory::primitive) || !resolver_util::integer(storage_record->primitive))
    {
        return fail(EReason::invalid_range, storage_source);
    }
    type_record.size = storage_record->size;
    type_record.alignment_log2 = storage_record->alignment_log2;
    type_record.primitive = storage_record->primitive;
    type_record.stride = storage_record->stride;
    return map(storage_source, type_record.related);
}

bool CResolver::enumeration(TTypeRecord& type_record) noexcept
{
    if (!shape(type_record.source, { "storage", "values" }, { "storage", "values" }) || !storage(type_record))
    {
        return false;
    }
    const CSchemaHandle values = property(type_record.source, "values");
    if (!names(values, false, type_record.source))
    {
        return false;
    }
    type_record.first = static_cast<std::uint32_t>(m_schema.m_labels.size());
    type_record.count = m_document.child_count(values);
    if (!grow(m_schema.m_labels, type_record.count, values))
    {
        return false;
    }
    const EPrimitive primitive = type_record.primitive;
    std::uint32_t label_slot = type_record.first;
    for (CSchemaHandle label_source = m_document.first_child(values); label_source; label_source = m_document.next_sibling(label_source), ++label_slot)
    {
        SLabel& label = m_schema.m_labels[label_slot];
        label.name = m_document.name_id(label_source);
        label.source = label_source;
        if (!number(label_source, resolver_util::signed_integer(primitive), static_cast<unsigned>(type_record.size * 8u), label.value) ||
            !map(label_source, m_schema.index(resolver_util::k_label, label_slot)))
        {
            return false;
        }
    }
    return true;
}

bool CResolver::reference(const CSchemaHandle& source, CSchemaIndex& result, const unsigned depth) noexcept
{
    m_stage = EStage::references;
    const CSchemaHandle origin = source;
    CSchemaHandle current = origin;
    unsigned current_depth = depth;
    while (true)
    {
        if (current_depth >= resolver_util::k_max_resolution_depth)
        {
            return fail(EReason::storage_limit, current);
        }
        if (m_document.value_kind(current) == EDocumentValueKind::string)
        {
            result = m_schema.lookup_type(m_document.string_value(current));
            if (!result)
            {
                return fail(EReason::unknown_type, current);
            }
            if (!map(current, result) || !resolve_type(result, (current_depth + 1u)))
            {
                return false;
            }
            break;
        }
        if (!shape(current, { "element", "count" }, { "element", "count" }))
        {
            return false;
        }
        std::uint64_t count{};
        if (!extent(property(current, "count"), count))
        {
            return false;
        }
        if ((count == 0u) || (count > UINT32_MAX))
        {
            return fail(EReason::invalid_range, property(current, "count"));
        }
        current = property(current, "element");
        ++current_depth;
    }

    //  Each nested descriptor is an element child of its containing descriptor.
    //  Walk back outward, retaining the original inner-before-outer slot order.
    while (current != origin)
    {
        const CSchemaHandle descriptor = m_document.parent(current);
        std::uint64_t count{};
        if (!extent(property(descriptor, "count"), count))
        {
            return false;
        }
        TTypeRecord array_record;
        array_record.category = ECategory::array;
        array_record.source = descriptor;
        array_record.count = static_cast<std::uint32_t>(count);
        array_record.related = result;
        const TTypeRecord element = *m_schema.type_record(result);
        if ((element.size != 0u) && ((count > UINT64_MAX / element.size) || (count * element.size > memory::k_byte_size_ceiling)))
        {
            return fail(EReason::storage_limit, descriptor);
        }
        array_record.size = static_cast<std::uint32_t>(count * element.size);
        array_record.alignment_log2 = element.alignment_log2;
        array_record.stride = element.size;
        array_record.set_flag(TTypeRecord::k_gaps, element.flag(TTypeRecord::k_gaps));
        array_record.set_state(2u);
        if (!grow(m_schema.m_types, 1u, descriptor))
        {
            return false;
        }
        m_schema.m_types.last() = array_record;
        result = m_schema.index(resolver_util::k_type, static_cast<std::uint32_t>(m_schema.m_types.size() - 1u));
        if (!map(descriptor, result))
        {
            return false;
        }
        current = descriptor;
    }
    return true;
}

bool CResolver::resolve_type(const CSchemaIndex type_index, const unsigned depth) noexcept
{
    const std::uint32_t slot = m_schema.ordinal(type_index);
    TTypeRecord type_record = m_schema.m_types[slot];
    if (type_record.state() == 2u)
    {
        return true;
    }
    if (type_record.state() == 1u)
    {
        return fail(EReason::cycle, type_record.source, m_context_type);
    }
    if (depth >= resolver_util::k_max_resolution_depth)
    {
        return fail(EReason::storage_limit, type_record.source);
    }
    m_schema.m_types[slot].set_state(1u);
    const CSchemaHandle old_type = m_context_type, old_member = m_context_member;
    m_context_type = type_record.source;
    m_context_member = {};
    m_stage = EStage::declarations;
    const bool resolved = (type_record.category == ECategory::enumeration) ? enumeration(type_record) :
        ((type_record.category == ECategory::structure) ? structure(type_record, depth) : bit_structure(type_record, depth));
    if (!resolved)
    {
        return false;
    }
    type_record.set_state(2u);
    m_schema.m_types[slot] = type_record;
    m_context_type = old_type;
    m_context_member = old_member;
    return true;
}

bool CResolver::structure(TTypeRecord& type_record, const unsigned depth) noexcept
{
    if (!shape(type_record.source, { "members", "detail" }, { "members" }))
    {
        return false;
    }
    const CSchemaHandle members = property(type_record.source, "members");
    if (!names(members, true, type_record.source, true))
    {
        return false;
    }
    type_record.first = static_cast<std::uint32_t>(m_schema.m_members.size());
    type_record.count = m_document.child_count(members);
    type_record.alignment_log2 = 0u;
    type_record.set_flag(TTypeRecord::k_named_components, type_record.count != 0u);
    if (!grow(m_schema.m_members, type_record.count, members))
    {
        return false;
    }
    std::uint32_t member_slot = type_record.first;
    CSchemaIndex component;
    bool explicit_offsets = false;
    CSchemaHandle first_member;
    bool has_storage = false;
    for (CSchemaHandle member_source = m_document.first_child(members); member_source; member_source = m_document.next_sibling(member_source), ++member_slot)
    {
        m_context_member = member_source;
        m_stage = EStage::declarations;
        if (!shape(member_source, { "type", "default", "offset" }, { "type" }))
        {
            return false;
        }
        const CSchemaHandle offset = property(member_source, "offset");
        if (member_slot == type_record.first)
        {
            explicit_offsets = offset.is_valid();
            first_member = member_source;
        }
        else if (offset.is_valid() != explicit_offsets)
        {
            return fail(EReason::invalid_layout, offset ? offset : member_source, first_member);
        }
        CResolvedSchema::SMemberRecord member;
        member.name = m_document.name_id(member_source);
        member.source = member_source;
        if (!reference(property(member_source, "type"), member.type, (depth + 1u)))
        {
            return false;
        }
        const TTypeRecord member_type = *m_schema.type_record(member.type);
        m_stage = EStage::layout;
        if (offset)
        {
            std::uint64_t member_offset{};
            if (!extent(offset, member_offset))
            {
                return false;
            }
            if (member_offset > memory::k_byte_size_ceiling)
            {
                return fail(EReason::storage_limit, offset);
            }
            member.offset = static_cast<std::uint32_t>(member_offset);
        }
        member.size = member_type.size;
        type_record.set_flag(TTypeRecord::k_gaps,
            type_record.flag(TTypeRecord::k_gaps) || member_type.flag(TTypeRecord::k_gaps));
        type_record.alignment_log2 = std::max(type_record.alignment_log2, member_type.alignment_log2);
        has_storage |= member.size != 0u;
        if (member_slot == type_record.first)
        {
            component = member.type;
        }
        type_record.set_flag(TTypeRecord::k_named_components,
            (type_record.flag(TTypeRecord::k_named_components) && (member_type.category == ECategory::primitive) && (component == member.type)));
        if (!make_default(member.type, property(member_source, "default"), member.default_description, (depth + 1u)))
        {
            return false;
        }
        m_schema.m_members[member_slot] = member;
        if (!map(member_source, m_schema.index(resolver_util::k_member, member_slot)))
        {
            return false;
        }
    }
    m_context_member = {};
    return structure_layout(type_record, explicit_offsets, has_storage);
}

bool CResolver::structure_layout(TTypeRecord& type_record, const bool explicit_offsets, const bool has_storage) noexcept
{
    m_stage = EStage::layout;
    const CSchemaHandle detail = property(type_record.source, "detail");
    CSchemaHandle size_source;
    std::uint64_t declared_size{};
    if (detail)
    {
        if (!shape(detail, { "size", "alignment", "internal" }))
        {
            return false;
        }
        const CSchemaHandle internal_source = property(detail, "internal");
        bool internal{};
        if (internal_source && !m_document.boolean_value(internal_source, internal))
        {
            return fail(EReason::invalid_input, internal_source);
        }
        type_record.set_flag(TTypeRecord::k_internal, internal);
        const CSchemaHandle alignment_source = property(detail, "alignment");
        if (alignment_source)
        {
            std::uint64_t declared_value{};
            if (!extent(alignment_source, declared_value))
            {
                return false;
            }
            if (declared_value > memory::k_byte_size_ceiling)
            {
                return fail(EReason::storage_limit, alignment_source);
            }
            if (!declared_value || (declared_value & (declared_value - 1u)) || (declared_value < type_record.alignment()))
            {
                return fail(EReason::invalid_layout, alignment_source);
            }
            if (!has_storage && (declared_value != 1u))
            {
                return fail(EReason::invalid_layout, alignment_source);
            }
            if (declared_value > 128u)
            {
                return fail(EReason::unsupported_feature, alignment_source);
            }
            while (type_record.alignment() < declared_value)
            {
                ++type_record.alignment_log2;
            }
        }
        size_source = property(detail, "size");
        if (size_source)
        {
            if (!extent(size_source, declared_size))
            {
                return false;
            }
            if (declared_size > memory::k_byte_size_ceiling)
            {
                return fail(EReason::storage_limit, size_source);
            }
            if (!has_storage && (declared_size != 0u))
            {
                return fail(EReason::invalid_layout, size_source);
            }
        }
    }
    if (explicit_offsets && !size_source)
    {
        return fail(EReason::missing_property, detail ? detail : type_record.source);
    }

    //  In explicit mode cursor totals occupied member extents; offsets set their positions independently.
    std::uint64_t cursor = 0u;
    for (std::uint32_t ordinal = 0u; ordinal < type_record.count; ++ordinal)
    {
        CResolvedSchema::SMemberRecord& member = m_schema.m_members[type_record.first + ordinal];
        const TTypeRecord member_type = *m_schema.type_record(member.type);
        m_context_member = member.source;
        std::uint64_t member_offset = member.offset, member_end{};
        if (!explicit_offsets)
        {
            if (!align(cursor, member_type.alignment(), member_offset, member.source))
            {
                return false;
            }
            member.offset = static_cast<std::uint32_t>(member_offset);
            type_record.set_flag(TTypeRecord::k_gaps,
                type_record.flag(TTypeRecord::k_gaps) || (member_offset != cursor));
        }
        else if ((member_offset & (member_type.alignment() - 1u)) != 0u)
        {
            return fail(EReason::invalid_layout, property(member.source, "offset"));
        }
        if (!add(member_offset, member.size, member_end, member.source))
        {
            return false;
        }
        if (explicit_offsets)
        {
            if (member_end > declared_size)
            {
                return fail(EReason::invalid_layout, member.source);
            }
            if (member.size != 0u)
            {
                for (std::uint32_t prior_ordinal = 0u; prior_ordinal < ordinal; ++prior_ordinal)
                {
                    const CResolvedSchema::SMemberRecord& prior = m_schema.m_members[type_record.first + prior_ordinal];
                    const std::uint64_t prior_end = static_cast<std::uint64_t>(prior.offset) + prior.size;
                    if ((prior.size != 0u) && (member_offset < prior_end) && (prior.offset < member_end))
                    {
                        fail(EReason::invalid_layout, member.source, prior.source);
                        m_diagnostic.ranges_available = true;
                        m_diagnostic.range_begin = member_offset;
                        m_diagnostic.range_end = member_end;
                        m_diagnostic.related_begin = prior.offset;
                        m_diagnostic.related_end = prior_end;
                        return false;
                    }
                }
            }
            if (!add(cursor, member.size, cursor, member.source))
            {
                return false;
            }
        }
        else
        {
            cursor = member_end;
        }
    }
    m_context_member = {};
    if (explicit_offsets)
    {
        if ((declared_size & (type_record.alignment() - 1u)) != 0u)
        {
            return fail(EReason::invalid_layout, size_source);
        }
        type_record.set_flag(TTypeRecord::k_gaps,
            type_record.flag(TTypeRecord::k_gaps) || (cursor != declared_size));
        type_record.size = type_record.stride = static_cast<std::uint32_t>(declared_size);
    }
    else
    {
        std::uint64_t padded_size{};
        if (!align(cursor, type_record.alignment(), padded_size, type_record.source))
        {
            return false;
        }
        type_record.size = type_record.stride = static_cast<std::uint32_t>(padded_size);
        type_record.set_flag(TTypeRecord::k_gaps,
            type_record.flag(TTypeRecord::k_gaps) || (cursor != padded_size));
        if (size_source && (declared_size != padded_size))
        {
            return fail(EReason::invalid_layout, size_source);
        }
    }
    return true;
}

bool CResolver::scalar(const CSchemaIndex type, const CSchemaHandle& source, SScalar& result) noexcept
{
    const TTypeRecord type_record = *m_schema.type_record(type);
    if (type_record.category == ECategory::enumeration)
    {
        for (std::uint32_t label_ordinal = 0u; label_ordinal < type_record.count; ++label_ordinal)
        {
            const SLabel& label = m_schema.m_labels[type_record.first + label_ordinal];
            if ((m_document.value_kind(source) == EDocumentValueKind::string) &&
                resolver_util::equal(m_document.string_value(source), m_document.property_name(label.name)))
            {
                result = label.value;
                return true;
            }
        }
        return fail(EReason::invalid_default, source);
    }
    if (type_record.category != ECategory::primitive)
    {
        return fail(EReason::invalid_default, source);
    }
    if (resolver_util::integer(type_record.primitive))
    {
        return number(source, resolver_util::signed_integer(type_record.primitive), static_cast<unsigned>(type_record.size * 8u), result);
    }
    double floating_value{};
    std::int64_t signed_value{};
    std::uint64_t unsigned_value{};
    bool boolean{};
    bool special = false;
    if (m_document.signed_integer_value(source, signed_value))
    {
        floating_value = static_cast<double>(signed_value);
    }
    else if (m_document.unsigned_integer_value(source, unsigned_value))
    {
        floating_value = static_cast<double>(unsigned_value);
    }
    else if (m_document.floating_point_value(source, floating_value))
    {
    }
    else if ((type_record.primitive == EPrimitive::b8) && m_document.boolean_value(source, boolean))
    {
        floating_value = boolean ? 1.0 : 0.0;
    }
    else if ((type_record.primitive != EPrimitive::b8) && (m_document.value_kind(source) == EDocumentValueKind::string))
    {
        const CStringView text = m_document.string_value(source);
        char lower[10]{};
        if (text.length() >= sizeof(lower))
        {
            return fail(EReason::invalid_default, source);
        }
        for (std::size_t character_index = 0u; character_index < text.length(); ++character_index)
        {
            const std::uint8_t character = text.string()[character_index];
            lower[character_index] = static_cast<char>(((character >= 'A') && (character <= 'Z')) ? (character + ('a' - 'A')) : character);
        }
        const CStringView word{ lower, text.length() };
        if (resolver_util::equal(word, "nan"))
        {
            floating_value = std::numeric_limits<double>::quiet_NaN();
        }
        else if (resolver_util::equal(word, "inf") || resolver_util::equal(word, "+inf") ||
                 resolver_util::equal(word, "infinity") || resolver_util::equal(word, "+infinity"))
        {
            floating_value = std::numeric_limits<double>::infinity();
        }
        else if (resolver_util::equal(word, "-inf") || resolver_util::equal(word, "-infinity"))
        {
            floating_value = -std::numeric_limits<double>::infinity();
        }
        else
        {
            return fail(EReason::invalid_default, source);
        }
        special = true;
    }
    else
    {
        return fail(EReason::invalid_default, source);
    }
    if (!special && !std::isfinite(floating_value))
    {
        return fail(EReason::invalid_default, source);
    }
    if (type_record.primitive == EPrimitive::b8)
    {
        result.kind = EScalar::boolean;
        result.value.unsigned_value = (floating_value != 0.0) ? 1u : 0u;
    }
    else if (type_record.primitive == EPrimitive::f16)
    {
        result.kind = EScalar::half;
        result.value.unsigned_value = fp16data_t{ floating_value }.getBits();
    }
    else
    {
        if (type_record.primitive == EPrimitive::f32)
        {
            if (!special && (std::abs(floating_value) > static_cast<double>(std::numeric_limits<float>::max())))
            {
                return fail(EReason::invalid_range, source);
            }
            const float rounded_value = static_cast<float>(floating_value);
            if (!special && (!std::isfinite(rounded_value) || ((floating_value != 0.0) && (rounded_value == 0.0f))))
            {
                return fail(EReason::invalid_range, source);
            }
            floating_value = rounded_value;
        }
        result.kind = EScalar::floating_point;
        result.value.floating_value = floating_value;
    }
    return true;
}

bool CResolver::make_default(const CSchemaIndex type, const CSchemaHandle& source, CSchemaIndex& result, const unsigned depth) noexcept
{
    if (!source)
    {
        return true;
    }
    m_stage = EStage::defaults;
    const std::uint32_t default_slot = static_cast<std::uint32_t>(m_schema.m_defaults.size());
    if (!grow(m_schema.m_defaults, 1u, source) || !default_record(type, source, default_slot, depth))
    {
        return false;
    }
    result = m_schema.index(resolver_util::k_default, default_slot);
    return true;
}

bool CResolver::default_record(const CSchemaIndex type, const CSchemaHandle& source, const std::uint32_t slot, const unsigned depth) noexcept
{
    if (depth >= resolver_util::k_max_resolution_depth)
    {
        return fail(EReason::storage_limit, source);
    }
    const TTypeRecord type_record = *m_schema.type_record(type);
    CResolvedSchema::SDefaultRecord default_info;
    default_info.type = type;
    default_info.source = source;
    if (type_record.category == ECategory::array)
    {
        TTypeRecord base = type_record;
        while (base.category == ECategory::array)
        {
            base = *m_schema.type_record(base.related);
        }
        if ((base.category != ECategory::primitive) && (base.category != ECategory::enumeration))
        {
            return fail(EReason::invalid_default, source);
        }
        if ((m_document.value_kind(source) != EDocumentValueKind::array) || (m_document.child_count(source) > type_record.count))
        {
            return fail(EReason::invalid_default, source);
        }
        default_info.first = static_cast<std::uint32_t>(m_schema.m_defaults.size());
        default_info.count = m_document.child_count(source);
        if (!grow(m_schema.m_defaults, default_info.count, source))
        {
            return false;
        }
        CSchemaHandle child = m_document.first_child(source);
        for (std::uint32_t element_ordinal = 0u; element_ordinal < default_info.count; ++element_ordinal)
        {
            if (m_document.is_object_entry(child))
            {
                return fail(EReason::invalid_default, child);
            }
            if (!default_record(type_record.related, child, (default_info.first + element_ordinal), (depth + 1u)))
            {
                return false;
            }
            child = m_document.next_sibling(child);
        }
    }
    else if (!scalar(type, source, default_info.scalar))
    {
        return false;
    }
    m_schema.m_defaults[slot] = default_info;
    return true;
}

bool CResolver::bit_structure(TTypeRecord& type_record, const unsigned depth) noexcept
{
    if (!shape(type_record.source, { "storage", "members" }, { "storage", "members" }) || !storage(type_record))
    {
        return false;
    }
    const CSchemaHandle members = property(type_record.source, "members");
    if (!names(members, true, type_record.source))
    {
        return false;
    }
    type_record.first = static_cast<std::uint32_t>(m_schema.m_fields.size());
    type_record.count = m_document.child_count(members);
    if (!grow(m_schema.m_fields, type_record.count, members))
    {
        return false;
    }
    std::uint64_t used{};
    std::uint32_t field_slot = type_record.first;
    for (CSchemaHandle field_source = m_document.first_child(members); field_source; field_source = m_document.next_sibling(field_source), ++field_slot)
    {
        m_context_member = field_source;
        m_stage = EStage::declarations;
        if (!shape(field_source, { "type", "mask", "interpretation", "default" }, { "type", "mask" }))
        {
            return false;
        }
        SField field;
        field.name = m_document.name_id(field_source);
        field.source = field_source;
        if (m_document.value_kind(property(field_source, "type")) != EDocumentValueKind::string)
        {
            return fail(EReason::invalid_input, property(field_source, "type"));
        }
        if (!reference(property(field_source, "type"), field.type, (depth + 1u)))
        {
            return false;
        }
        const TTypeRecord logical = *m_schema.type_record(field.type);
        if (((logical.category != ECategory::primitive) && (logical.category != ECategory::enumeration)) ||
            (!resolver_util::integer(logical.primitive) && (logical.primitive != EPrimitive::b8)))
        {
            return fail(EReason::invalid_range, property(field_source, "type"));
        }
        field.primitive = logical.primitive;
        field.signed_value = resolver_util::signed_integer(field.primitive);
        m_stage = EStage::layout;
        if (!extent(property(field_source, "mask"), field.mask))
        {
            return false;
        }
        if (!field.mask || (field.mask & ~resolver_util::max_unsigned(static_cast<unsigned>(type_record.size * 8u))))
        {
            return fail(EReason::invalid_range, property(field_source, "mask"));
        }
        std::uint64_t bits = field.mask;
        while ((bits & 1u) == 0u)
        {
            ++field.shift;
            bits >>= 1u;
        }
        while ((bits & 1u) != 0u)
        {
            ++field.width;
            bits >>= 1u;
        }
        if (bits != 0u)
        {
            return fail(EReason::invalid_range, property(field_source, "mask"));
        }
        if (used & field.mask)
        {
            for (std::uint32_t prior = type_record.first; prior < field_slot; ++prior)
            {
                if (m_schema.m_fields[prior].mask & field.mask)
                {
                    const SField& other = m_schema.m_fields[prior];
                    fail(EReason::invalid_layout, field_source, other.source);
                    m_diagnostic.ranges_available = true;
                    m_diagnostic.range_begin = field.shift;
                    m_diagnostic.range_end = field.shift + field.width;
                    m_diagnostic.related_begin = other.shift;
                    m_diagnostic.related_end = other.shift + other.width;
                    return false;
                }
            }
        }
        used |= field.mask;
        if (logical.category == ECategory::enumeration)
        {
            for (std::uint32_t label = 0u; label < logical.count; ++label)
            {
                if (!resolver_util::fits(m_schema.m_labels[logical.first + label].value, field.signed_value, field.width))
                {
                    return fail(EReason::invalid_range, field_source, m_schema.m_labels[logical.first + label].source);
                }
            }
        }
        const CSchemaHandle interpretation_source = property(field_source, "interpretation");
        if (interpretation_source)
        {
            if (!resolver_util::equal(m_document.string_value(interpretation_source), "unorm"))
            {
                return fail(EReason::unsupported_feature, interpretation_source);
            }
            if ((logical.category != ECategory::primitive) || !resolver_util::integer(logical.primitive) || field.signed_value)
            {
                return fail(EReason::invalid_range, interpretation_source);
            }
            field.interpretation = EInterpretation::unorm;
        }
        const CSchemaHandle def = property(field_source, "default");
        m_stage = EStage::defaults;
        if ((field.interpretation == EInterpretation::unorm) && def)
        {
            SScalar value;
            if (!scalar(m_schema.index(resolver_util::k_type, (static_cast<std::uint32_t>(EPrimitive::f64) - 1u)), def, value))
            {
                return false;
            }
            if (!std::isfinite(value.value.floating_value) || (value.value.floating_value < 0.0) || (value.value.floating_value > 1.0))
            {
                return fail(EReason::invalid_default, def);
            }
            if (!grow(m_schema.m_defaults, 1u, def))
            {
                return false;
            }
            CResolvedSchema::SDefaultRecord& default_info = m_schema.m_defaults.last();
            default_info.type = field.type;
            default_info.source = def;
            default_info.scalar = value;
            field.default_description = m_schema.index(resolver_util::k_default, static_cast<std::uint32_t>(m_schema.m_defaults.size() - 1u));
        }
        else if (def)
        {
            if (!make_default(field.type, def, field.default_description, (depth + 1u)))
            {
                return false;
            }
            const SScalar value = m_schema.m_defaults[m_schema.ordinal(field.default_description)].scalar;
            if (!resolver_util::fits(value, field.signed_value, field.width))
            {
                return fail(EReason::invalid_default, def);
            }
        }
        m_schema.m_fields[field_slot] = field;
        if (!map(field_source, m_schema.index(resolver_util::k_field, field_slot)))
        {
            return false;
        }
    }
    type_record.set_flag(TTypeRecord::k_gaps, (used != resolver_util::max_unsigned(static_cast<unsigned>(type_record.size * 8u))));
    return true;
}

bool CResolvedSchema::resolve(const CBakedDocument& document, SDiagnostic& diagnostic) noexcept
{
    return resolve(CSchemaDocumentQuery{ document }, diagnostic);
}

bool CResolvedSchema::resolve(const CLiveDocument& document, SDiagnostic& diagnostic) noexcept
{
    return resolve(CSchemaDocumentQuery{ document }, diagnostic);
}

bool CResolvedSchema::resolve(SDiagnostic& diagnostic) noexcept
{
    return resolve(m_document, diagnostic);
}

bool CResolvedSchema::resolve(const CSchemaDocumentQuery& document, SDiagnostic& diagnostic) noexcept
{   //  Copy first: the caller may pass this owner's current retained query.
    const CSchemaDocumentQuery input = document;
    clear();
    diagnostic = {};
    m_document = input;
    CResolver resolver{ *this, diagnostic };
    if (!resolver.run())
    {
        clear();
        return false;
    }
    m_ready = true;
    return true;
}
}   // namespace schema

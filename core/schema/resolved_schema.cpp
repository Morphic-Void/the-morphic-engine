
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resolved_schema.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Resolve live/baked definitions, references, defaults and physical layouts.

#include "schema/resolved_schema.hpp"
#include "schema/data_block_format.hpp"
#include "schema/type_compatibility.hpp"
#include "schema/value_conversion.hpp"
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
static constexpr std::uint32_t k_non_storage = 5u, k_interface_member = 6u;
static constexpr const char* k_primitives[] = { "", "i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f16", "f32", "f64", "b8" };
static constexpr std::uint8_t k_sizes[] = { 0, 1, 2, 4, 8, 1, 2, 4, 8, 2, 4, 8, 1 };

//  Table entry zero represents EPrimitive::none and has no runtime type record.
//  Named definitions follow the built-in primitive records in m_types.
static constexpr std::uint32_t k_primitive_count = static_cast<std::uint32_t>((sizeof(k_primitives) / sizeof(k_primitives[0])) - 1u);
static_assert((sizeof(k_sizes) / sizeof(k_sizes[0])) == (k_primitive_count + 1u));
static_assert(static_cast<std::uint32_t>(EPrimitive::b8) == k_primitive_count);
static_assert(static_cast<unsigned>(ECategory::primitive) == static_cast<unsigned>(EDeclarationCategory::primitive));
static_assert(static_cast<unsigned>(ECategory::enumeration) == static_cast<unsigned>(EDeclarationCategory::enumeration));
static_assert(static_cast<unsigned>(ECategory::structure) == static_cast<unsigned>(EDeclarationCategory::structure));
static_assert(static_cast<unsigned>(ECategory::array) == static_cast<unsigned>(EDeclarationCategory::array));
static_assert(static_cast<unsigned>(ECategory::bit_structure) == static_cast<unsigned>(EDeclarationCategory::bit_structure));

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
    return detail::fits_scalar(value, is_signed, width);
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

CStringView primitive_name(const EPrimitive primitive) noexcept
{
    const auto ordinal = static_cast<std::uint32_t>(primitive);
    return (ordinal <= resolver_util::k_primitive_count) ? CStringView{ resolver_util::k_primitives[ordinal] } : CStringView{};
}

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
        m_type_names = std::move(source.m_type_names);
        m_members = std::move(source.m_members);
        m_member_names = std::move(source.m_member_names);
        m_member_order = std::move(source.m_member_order);
        m_labels = std::move(source.m_labels);
        m_label_names = std::move(source.m_label_names);
        m_label_values = std::move(source.m_label_values);
        m_fields = std::move(source.m_fields);
        m_field_names = std::move(source.m_field_names);
        m_defaults = std::move(source.m_defaults);
        m_mapping = std::move(source.m_mapping);
        m_non_storage = std::move(source.m_non_storage);
        m_non_storage_names = std::move(source.m_non_storage_names);
        m_interface_members = std::move(source.m_interface_members);
        m_interface_member_names = std::move(source.m_interface_member_names);
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
    m_type_names.deallocate();
    m_members.deallocate();
    m_member_names.deallocate();
    m_member_order.deallocate();
    m_labels.deallocate();
    m_label_names.deallocate();
    m_label_values.deallocate();
    m_fields.deallocate();
    m_field_names.deallocate();
    m_defaults.deallocate();
    m_mapping.deallocate();
    m_non_storage.deallocate();
    m_non_storage_names.deallocate();
    m_interface_members.deallocate();
    m_interface_member_names.deallocate();
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

//  Name IDs belong to one document. Keep declaration records in their original
//  order and search small ordinal permutations with integer comparisons.
template<class T>
static void order_names(const TPodVector<T>& records, TPodVector<std::uint32_t>& order,
    const std::uint32_t first, const std::uint32_t count) noexcept
{
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        order[first + i] = first + i;
    }
    if (count > 1u)
    {
        std::sort((order.data() + first), (order.data() + first + count),
            [&records](const std::uint32_t a, const std::uint32_t b)
            {
                return records[a].name.query_value() < records[b].name.query_value();
            });
    }
}

template<class T>
[[nodiscard]] static std::uint32_t find_named_record(const TPodVector<T>& records,
    const TPodVector<std::uint32_t>& order, const std::uint32_t first,
    const std::uint32_t count, const CPropertyNameId name) noexcept
{
    if (!name.is_valid())
    {
        return UINT32_MAX;
    }
    std::uint32_t low = first, high = first + count;
    while (low < high)
    {
        const std::uint32_t mid = low + ((high - low) / 2u);
        if (records[order[mid]].name.query_value() < name.query_value())
        {
            low = mid + 1u;
        }
        else
        {
            high = mid;
        }
    }
    return ((low < (first + count)) && (records[order[low]].name == name)) ? order[low] : UINT32_MAX;
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
    const std::uint32_t found = find_named_record(m_types, m_type_names, resolver_util::k_primitive_count,
        m_definitions, m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_type, found) : CSchemaIndex{};
}

CSchemaIndex CResolvedSchema::find_type(const CStringView& name) const noexcept
{
    return m_ready ? lookup_type(name) : CSchemaIndex{};
}

const CResolvedSchema::SNonStorageRecord* CResolvedSchema::non_storage_record(const CSchemaIndex i) const noexcept
{
    return is_kind(i, resolver_util::k_non_storage, m_non_storage.size()) ? &m_non_storage[ordinal(i)] : nullptr;
}

CSchemaIndex CResolvedSchema::lookup_declaration(const CStringView& name) const noexcept
{
    const CSchemaIndex physical = lookup_type(name);
    if (physical)
    {
        return physical;
    }
    const std::uint32_t found = find_named_record(m_non_storage, m_non_storage_names, 0u,
        static_cast<std::uint32_t>(m_non_storage.size()), m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_non_storage, found) : CSchemaIndex{};
}

CSchemaIndex CResolvedSchema::find_declaration(const CStringView& name) const noexcept
{
    return m_ready ? lookup_declaration(name) : CSchemaIndex{};
}

std::uint32_t CResolvedSchema::declaration_count() const noexcept
{
    return m_ready ? (m_definitions + static_cast<std::uint32_t>(m_non_storage.size())) : 0u;
}

CSchemaIndex CResolvedSchema::declaration_at(const std::uint32_t n) const noexcept
{
    if (!m_ready || (n >= declaration_count()))
    {
        return {};
    }
    return (n < m_definitions) ? definition_at(n) : index(resolver_util::k_non_storage, n - m_definitions);
}

bool CResolvedSchema::declaration(const CSchemaIndex i, SDeclaration& result) const noexcept
{
    if (!m_ready)
    {
        return false;
    }
    if (const STypeRecord* const record = type_record(i))
    {   //  Physical category values deliberately retain their established order.
        result = { static_cast<EDeclarationCategory>(record->category), record->name, record->source };
        return true;
    }
    if (const SNonStorageRecord* const record = non_storage_record(i))
    {
        result = { record->category, record->name, record->source };
        return true;
    }
    return false;
}

bool CResolvedSchema::association(const CSchemaIndex i, SAssociation& result) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(i);
    if (!m_ready || !record || (record->category != EDeclarationCategory::association))
    {
        return false;
    }
    result = { record->target };
    return true;
}

bool CResolvedSchema::interface_info(const CSchemaIndex i, SInterface& result) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(i);
    if (!m_ready || !record || (record->category != EDeclarationCategory::interface_type))
    {
        return false;
    }
    result = { record->count };
    return true;
}

CSchemaIndex CResolvedSchema::interface_member_at(const CSchemaIndex owner, const std::uint32_t n) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(owner);
    SInterface info;
    return (interface_info(owner, info) && (n < info.count)) ? index(resolver_util::k_interface_member, record->first + n) : CSchemaIndex{};
}

CSchemaIndex CResolvedSchema::find_interface_member(const CSchemaIndex owner, const CStringView& name) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(owner);
    SInterface info;
    if (!interface_info(owner, info))
    {
        return {};
    }
    const std::uint32_t found = find_named_record(m_interface_members, m_interface_member_names,
        record->first, record->count, m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_interface_member, found) : CSchemaIndex{};
}

bool CResolvedSchema::interface_member(const CSchemaIndex i, SInterfaceMember& result) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_interface_member, m_interface_members.size()))
    {
        return false;
    }
    result = m_interface_members[ordinal(i)];
    return true;
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

bool CResolvedSchema::definition_ordinal(const CSchemaIndex type, std::uint32_t& result) const noexcept
{
    if (!m_ready || !is_kind(type, resolver_util::k_type, (resolver_util::k_primitive_count + m_definitions)) ||
        (ordinal(type) < resolver_util::k_primitive_count))
    {
        return false;
    }
    result = ordinal(type) - resolver_util::k_primitive_count;
    return true;
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

CSchemaIndex CResolvedSchema::physical_member_at(const CSchemaIndex i, const std::uint32_t n) const noexcept
{
    const STypeRecord* const record = type_record(i);
    if (!m_ready || !record || (record->category != ECategory::structure) || (n >= record->count))
    {
        return {};
    }
    return index(resolver_util::k_member, (record->first + m_member_order[record->first + n]));
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

CSchemaHandle CResolvedSchema::decoration_object(const CSchemaIndex owner) const noexcept
{
    if (!m_ready)
    {
        return {};
    }
    CSchemaHandle source;
    if (const STypeRecord* const record = type_record(owner))
    {
        source = record->source;
    }
    else if (is_kind(owner, resolver_util::k_member, m_members.size()))
    {
        source = m_members[ordinal(owner)].source;
    }
    else if (is_kind(owner, resolver_util::k_field, m_fields.size()))
    {
        source = m_fields[ordinal(owner)].source;
    }
    else if (const SNonStorageRecord* const declaration = non_storage_record(owner))
    {
        source = declaration->source;
    }
    else if (is_kind(owner, resolver_util::k_interface_member, m_interface_members.size()))
    {
        source = m_interface_members[ordinal(owner)].source;
    }
    return m_document.object_child(source, CStringView{ "decorations" });
}

bool CResolvedSchema::read_decoration(const CSchemaHandle source, SDecoration& result) const noexcept
{
    if (m_document.value_kind(source) != EDocumentValueKind::string)
    {
        return false;
    }
    result = { m_document.name(source), m_document.string_value(source), source };
    return true;
}

bool CResolvedSchema::find_decoration(const CSchemaIndex owner, const CStringView& key, SDecoration& result) const noexcept
{
    return read_decoration(m_document.object_child(decoration_object(owner), key), result);
}

bool CResolvedSchema::first_decoration(const CSchemaIndex owner, SDecoration& result) const noexcept
{
    return read_decoration(m_document.first_child(decoration_object(owner)), result);
}

bool CResolvedSchema::next_decoration(const CSchemaHandle current, SDecoration& result) const noexcept
{
    if (!m_ready)
    {
        return false;
    }
    const CSchemaHandle object = m_document.parent(current);
    if (!object || (decoration_object(map_occurrence(m_document.parent(object))) != object))
    {
        return false;
    }
    return read_decoration(m_document.next_sibling(current), result);
}

template<class T>
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
    const std::uint32_t found = find_named_record(m_members, m_member_names, members.first, members.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_member, found) : CSchemaIndex{};
}

bool CResolvedSchema::member_ordinal(const CSchemaIndex type, const CSchemaIndex member, std::uint32_t& result) const noexcept
{
    const auto members = child_range(type, ECategory::structure, m_members);
    if (!is_kind(member, resolver_util::k_member, m_members.size()) || (ordinal(member) < members.first) ||
        ((ordinal(member) - members.first) >= members.count))
    {
        return false;
    }
    result = ordinal(member) - members.first;
    return true;
}

CSchemaIndex CResolvedSchema::find_label(const CSchemaIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SLabel> labels = child_range(type, ECategory::enumeration, m_labels);
    const std::uint32_t found = find_named_record(m_labels, m_label_names, labels.first, labels.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_label, found) : CSchemaIndex{};
}

CSchemaIndex CResolvedSchema::find_field(const CSchemaIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SField> fields = child_range(type, ECategory::bit_structure, m_fields);
    const std::uint32_t found = find_named_record(m_fields, m_field_names, fields.first, fields.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_field, found) : CSchemaIndex{};
}

[[nodiscard]] static std::uint64_t label_value_key(const SScalar& value) noexcept
{
    return value.kind == EScalar::signed_integer ?
        static_cast<std::uint64_t>(value.value.signed_value) ^ (UINT64_C(1) << 63u) : value.value.unsigned_value;
}

CSchemaIndex CResolvedSchema::first_label_for_value(const CSchemaIndex type, const SScalar& value) const noexcept
{
    const SRecordRange<SLabel> labels = child_range(type, ECategory::enumeration, m_labels);
    if (!labels.count || (value.kind != EScalar::signed_integer && value.kind != EScalar::unsigned_integer))
    {
        return {};
    }
    SScalar query = value;
    if (query.kind != m_labels[labels.first].value.kind)
    {
        if (query.kind == EScalar::signed_integer)
        {
            if (query.value.signed_value < 0)
            {
                return {};
            }
            query.value.unsigned_value = static_cast<std::uint64_t>(query.value.signed_value);
            query.kind = EScalar::unsigned_integer;
        }
        else
        {
            if (query.value.unsigned_value > INT64_MAX)
            {
                return {};
            }
            query.value.signed_value = static_cast<std::int64_t>(query.value.unsigned_value);
            query.kind = EScalar::signed_integer;
        }
    }
    const std::uint64_t key = label_value_key(query);
    std::uint32_t low = labels.first, high = labels.first + labels.count;
    while (low < high)
    {
        const std::uint32_t mid = low + ((high - low) / 2u);
        if (label_value_key(m_labels[m_label_values[mid]].value) < key)
        {
            low = mid + 1u;
        }
        else
        {
            high = mid;
        }
    }
    return ((low < (labels.first + labels.count)) && resolver_util::scalar_equal(value, m_labels[m_label_values[low]].value)) ?
        index(resolver_util::k_label, m_label_values[low]) : CSchemaIndex{};
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
    template<class T> bool grow(TPodVector<T>& values, const std::uint32_t count, const CSchemaHandle& at) noexcept;
    bool map(const CSchemaHandle& at, const CSchemaIndex target) noexcept;
    CSchemaHandle property(const CSchemaHandle& at, const char* const name) const noexcept;
    bool shape(
        const CSchemaHandle& source, const std::initializer_list<const char*> allowed,
        const std::initializer_list<const char*> required = {}) noexcept;
    bool decorations(const CSchemaHandle& owner) noexcept;
    bool declarations(const CSchemaHandle& container, const ECategory category) noexcept;
    bool non_storage_declarations(const CSchemaHandle& container, const EDeclarationCategory category) noexcept;
    bool resolve_non_storage(const CSchemaIndex declaration, const unsigned depth) noexcept;
    bool declaration_reference(const CSchemaHandle source, CSchemaIndex& result, const unsigned depth) noexcept;
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
    bool order_members(const TTypeRecord& type_record, const bool explicit_offsets, std::uint32_t& overlap, std::uint32_t& related) noexcept;
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
    COrdinalHashIndex m_type_index;
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

template<class T>
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

bool CResolver::decorations(const CSchemaHandle& owner) noexcept
{
    const CSchemaHandle object = property(owner, "decorations");
    if (!object)
    {
        return true;
    }
    if (m_document.value_kind(object) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, object);
    }

    //  Document objects enforce unique keys. Unlike declaration names, these
    //  keys follow document string rules and need not be C++ identifiers.
    for (CSchemaHandle child = m_document.first_child(object); child; child = m_document.next_sibling(child))
    {
        if (m_document.name(child).length() == 0u)
        {
            return fail(EReason::invalid_identifier, child);
        }
        if (m_document.value_kind(child) != EDocumentValueKind::string)
        {
            return fail(EReason::invalid_input, child);
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

    //  Objects already enforce unique names. Named array elements need a
    //  duplicate check; sort IDs while retaining their declaration ordinals.
    TPodVector<std::uint64_t> names;
    std::uint32_t duplicate = UINT32_MAX, related = UINT32_MAX;
    if (array && (m_document.child_count(container) > 1u))
    {
        if (!names.reserve(m_document.child_count(container)))
        {
            return fail(EReason::allocation_failed, container);
        }
        std::uint32_t ordinal{};
        for (CSchemaHandle child = m_document.first_child(container); child; child = m_document.next_sibling(child), ++ordinal)
        {
            if (!names.push_back((static_cast<std::uint64_t>(m_document.name_id(child).query_value()) << 32u) | ordinal))
            {
                return fail(EReason::allocation_failed, child);
            }
        }
        if (names.size() > 1u) std::sort(names.data(), (names.data() + names.size()));
        for (std::size_t i = 1u; i < names.size(); ++i)
        {
            if (((names[i] >> 32u) == (names[i - 1u] >> 32u)) && (static_cast<std::uint32_t>(names[i]) < duplicate))
            {
                duplicate = static_cast<std::uint32_t>(names[i]);
                related = static_cast<std::uint32_t>(names[i - 1u]);
            }
        }
    }
    std::uint32_t ordinal{};
    CSchemaHandle prior;
    for (CSchemaHandle declaration = m_document.first_child(container); declaration; declaration = m_document.next_sibling(declaration), ++ordinal)
    {
        if (!valid_identifier(m_document.name(declaration)))
        {
            return fail(EReason::invalid_identifier, declaration);
        }
        if (ordinal == related) prior = declaration;
        if (ordinal == duplicate)
        {
            return fail(EReason::duplicate_declaration, declaration, prior);
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

        //  Check built-ins before the operation-local declaration index.
        for (std::uint32_t i = 1u; i <= resolver_util::k_primitive_count; ++i)
        {
            if (resolver_util::equal(m_document.name(declaration), resolver_util::k_primitives[i]))
            {
                return fail(EReason::duplicate_declaration, declaration);
            }
        }
        const CPropertyNameId name = m_document.name_id(declaration);
        const auto hash = [this](const std::size_t i)
        {
            return COrdinalHashIndex::hash_key(m_schema.m_types[resolver_util::k_primitive_count + i].name.query_value());
        };
        const std::uint32_t existing = m_type_index.find(COrdinalHashIndex::hash_key(name.query_value()), [this, name](const std::uint32_t i)
            {
                return m_schema.m_types[resolver_util::k_primitive_count + i].name == name;
            });
        if (existing != COrdinalHashIndex::k_missing_record)
        {
            return fail(EReason::duplicate_declaration, declaration,
                m_schema.m_types[resolver_util::k_primitive_count + existing].source);
        }
        if (!m_type_index.reserve((static_cast<std::size_t>(m_schema.m_definitions) + 1u), m_schema.m_definitions, hash))
        {
            return fail(EReason::allocation_failed, declaration);
        }
        if (!grow(m_schema.m_types, 1u, declaration))
        {
            return false;
        }
        TTypeRecord& type_record = m_schema.m_types.last();
        type_record.name = name;
        type_record.source = declaration;
        type_record.category = category;
        m_type_index.insert(m_schema.m_definitions++, COrdinalHashIndex::hash_key(name.query_value()));
        if (!map(declaration, m_schema.index(resolver_util::k_type, static_cast<std::uint32_t>(m_schema.m_types.size() - 1u))))
        {
            return false;
        }
    }
    m_context_type = {};
    return true;
}

bool CResolver::non_storage_declarations(const CSchemaHandle& container, const EDeclarationCategory category) noexcept
{
    if (m_document.value_kind(container) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, container);
    }
    for (CSchemaHandle source = m_document.first_child(container); source; source = m_document.next_sibling(source))
    {
        m_context_type = source;
        const CStringView name = m_document.name(source);
        if (!valid_identifier(name))
        {
            return fail(EReason::invalid_identifier, source);
        }
        const CSchemaIndex physical = m_schema.lookup_type(name);
        if (physical)
        {
            return fail(EReason::duplicate_declaration, source, m_schema.type_record(physical)->source);
        }
        const CPropertyNameId name_id = m_document.name_id(source);
        const auto hash = [this](const std::size_t i) noexcept
        {
            return COrdinalHashIndex::hash_key(m_schema.m_non_storage[i].name.query_value());
        };
        const std::uint32_t existing = m_type_index.find(COrdinalHashIndex::hash_key(name_id.query_value()),
            [this, name_id](const std::uint32_t i) noexcept { return m_schema.m_non_storage[i].name == name_id; });
        if (existing != COrdinalHashIndex::k_missing_record)
        {
            return fail(EReason::duplicate_declaration, source, m_schema.m_non_storage[existing].source);
        }
        const auto count = static_cast<std::uint32_t>(m_schema.m_non_storage.size());
        if (count >= resolver_util::k_limit - m_schema.m_definitions)
        {
            return fail(EReason::storage_limit, source);
        }
        if (!m_type_index.reserve((static_cast<std::size_t>(count) + 1u), count, hash))
        {
            return fail(EReason::allocation_failed, source);
        }
        if (!grow(m_schema.m_non_storage, 1u, source))
        {
            return false;
        }
        m_schema.m_non_storage.last() = { name_id, source, {}, 0u, 0u, category, 0u };
        m_type_index.insert(count, COrdinalHashIndex::hash_key(name_id.query_value()));
        if (!map(source, m_schema.index(resolver_util::k_non_storage, count)))
        {
            return false;
        }
    }
    m_context_type = {};
    return true;
}

bool CResolver::declaration_reference(const CSchemaHandle source, CSchemaIndex& result, const unsigned depth) noexcept
{
    m_stage = EStage::references;
    if (m_document.value_kind(source) != EDocumentValueKind::string)
    {
        return fail(EReason::invalid_input, source);
    }
    result = m_schema.lookup_declaration(m_document.string_value(source));
    if (!result)
    {
        return fail(EReason::unknown_type, source);
    }
    const auto* const record = m_schema.non_storage_record(result);
    if (record && (record->state == 1u))
    {
        return fail(EReason::cycle, source, record->source);
    }
    return map(source, result) && (!record || resolve_non_storage(result, depth));
}

bool CResolver::resolve_non_storage(const CSchemaIndex declaration, const unsigned depth) noexcept
{
    const std::uint32_t slot = m_schema.ordinal(declaration);
    auto record = m_schema.m_non_storage[slot];
    if (record.state == 2u)
    {
        return true;
    }
    if (depth >= document_limits::k_max_recursion_depth)
    {
        return fail(EReason::storage_limit, record.source);
    }
    m_schema.m_non_storage[slot].state = 1u;
    const CSchemaHandle old_type = m_context_type, old_member = m_context_member;
    m_context_type = record.source;
    m_context_member = {};
    m_stage = EStage::declarations;
    if (record.category == EDeclarationCategory::association)
    {
        if (!shape(record.source, { "target", "decorations" }, { "target" }) || !decorations(record.source))
        {
            return false;
        }
        const CSchemaHandle target = property(record.source, "target");
        if ((m_document.value_kind(target) != EDocumentValueKind::null_value) &&
            !declaration_reference(target, record.target, depth + 1u))
        {
            return false;
        }
    }
    else
    {
        if (!shape(record.source, { "members", "decorations" }, { "members" }) || !decorations(record.source))
        {
            return false;
        }
        const CSchemaHandle members = property(record.source, "members");
        if (!names(members, true, record.source, true))
        {
            return false;
        }
        record.first = static_cast<std::uint32_t>(m_schema.m_interface_members.size());
        record.count = m_document.child_count(members);
        if (!grow(m_schema.m_interface_members, record.count, members) ||
            !grow(m_schema.m_interface_member_names, record.count, members))
        {
            return false;
        }
        std::uint32_t member_slot = record.first;
        for (CSchemaHandle source = m_document.first_child(members); source; source = m_document.next_sibling(source))
        {
            m_stage = EStage::declarations;
            m_context_member = source;
            if (!shape(source, { "type", "count", "decorations" }, { "type" }) || !decorations(source))
            {
                return false;
            }
            SInterfaceMember member;
            member.name = m_document.name_id(source);
            member.source = source;
            const CSchemaHandle count_source = property(source, "count");
            std::uint64_t count = 1u;
            if (count_source && !extent(count_source, count))
            {
                return false;
            }
            if ((count == 0u) || (count > UINT32_MAX))
            {
                return fail(EReason::invalid_range, count_source);
            }
            member.count = static_cast<std::uint32_t>(count);
            if (!declaration_reference(property(source, "type"), member.declaration, depth + 1u))
            {
                return false;
            }
            m_schema.m_interface_members[member_slot] = member;
            if (!map(source, m_schema.index(resolver_util::k_interface_member, member_slot)))
            {
                return false;
            }
            ++member_slot;
        }
        order_names(m_schema.m_interface_members, m_schema.m_interface_member_names, record.first, record.count);
    }
    record.state = 2u;
    m_schema.m_non_storage[slot] = record;
    m_context_type = old_type;
    m_context_member = old_member;
    return true;
}

bool CResolver::run() noexcept
{
    detail::SDataBlockRoot root;
    SDataBlockDiagnostic diagnostic;
    if (!detail::read_data_block_root(detail::SDocumentQueryAccess::read(m_document), root, diagnostic) || !root.types)
    {
        const detail::SOccurrence value = diagnostic.location.live_node ? detail::SOccurrence{ diagnostic.location.live_node } : detail::SOccurrence{ diagnostic.location.baked_value };
        return fail((diagnostic.reason == EDataBlockReason::unknown_property) ? EReason::unknown_property :
            (((diagnostic.reason == EDataBlockReason::none) || (diagnostic.reason == EDataBlockReason::missing_property)) ? EReason::missing_property : EReason::invalid_input),
            (value ? detail::SSchemaHandleAccess::make(value) : m_document.root()));
    }
    const CSchemaHandle types = property(m_document.root(), "types");
    if (!shape(types, { "enumerations", "structures", "bit_structures", "associations", "interfaces" }))
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
        if (resolver_util::equal(m_document.name(declaration_group), "associations") ||
            resolver_util::equal(m_document.name(declaration_group), "interfaces"))
        {
            continue;
        }
        const ECategory category = resolver_util::equal(m_document.name(declaration_group), "enumerations") ? ECategory::enumeration :
            (resolver_util::equal(m_document.name(declaration_group), "structures") ? ECategory::structure : ECategory::bit_structure);
        if (!declarations(declaration_group, category))
        {
            return false;
        }
    }
    if (!grow(m_schema.m_type_names, static_cast<std::uint32_t>(m_schema.m_types.size()), types))
    {
        return false;
    }
    order_names(m_schema.m_types, m_schema.m_type_names, resolver_util::k_primitive_count, m_schema.m_definitions);
    m_type_index = {};
    for (CSchemaHandle group = m_document.first_child(types); group; group = m_document.next_sibling(group))
    {
        if (resolver_util::equal(m_document.name(group), "associations"))
        {
            if (!non_storage_declarations(group, EDeclarationCategory::association))
            {
                return false;
            }
        }
        else if (resolver_util::equal(m_document.name(group), "interfaces"))
        {
            if (!non_storage_declarations(group, EDeclarationCategory::interface_type))
            {
                return false;
            }
        }
    }
    m_type_index = {};
    if (!grow(m_schema.m_non_storage_names, static_cast<std::uint32_t>(m_schema.m_non_storage.size()), types))
    {
        return false;
    }
    order_names(m_schema.m_non_storage, m_schema.m_non_storage_names, 0u, static_cast<std::uint32_t>(m_schema.m_non_storage.size()));
    for (std::uint32_t type_ordinal = 0u; type_ordinal < m_schema.m_definitions; ++type_ordinal)
    {
        if (!resolve_type(m_schema.index(resolver_util::k_type, (type_ordinal + resolver_util::k_primitive_count)), 0u))
        {
            return false;
        }
    }
    for (std::uint32_t n = 0u; n < m_schema.m_non_storage.size(); ++n)
    {
        if (!resolve_non_storage(m_schema.index(resolver_util::k_non_storage, n), 0u))
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
    EReason reason{ EReason::none };
    return detail::convert_number(m_document, source, is_signed, bit_width, result, reason) || fail(reason, source);
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
    if (!shape(type_record.source, { "storage", "values", "decorations" }, { "storage", "values" }) ||
        !decorations(type_record.source) || !storage(type_record))
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
    if (!grow(m_schema.m_labels, type_record.count, values) ||
        !grow(m_schema.m_label_names, type_record.count, values) ||
        !grow(m_schema.m_label_values, type_record.count, values))
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
    order_names(m_schema.m_labels, m_schema.m_label_names, type_record.first, type_record.count);
    for (std::uint32_t i = 0u; i < type_record.count; ++i)
    {
        m_schema.m_label_values[type_record.first + i] = type_record.first + i;
    }
    if (type_record.count > 1u)
    {
        std::sort((m_schema.m_label_values.data() + type_record.first),
            (m_schema.m_label_values.data() + type_record.first + type_record.count),
            [this](const std::uint32_t a, const std::uint32_t b)
            {
                const auto av = label_value_key(m_schema.m_labels[a].value), bv = label_value_key(m_schema.m_labels[b].value);
                return (av < bv) || ((av == bv) && (a < b));
            });
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
        if (current_depth >= document_limits::k_max_recursion_depth)
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
        if (!shape(current, { "element", "count", "stride", "alignment", "decorations" }, { "element", "count" }) ||
            !decorations(current))
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
        const CSchemaHandle stride_source = property(descriptor, "stride");
        const CSchemaHandle alignment_source = property(descriptor, "alignment");
        std::uint64_t stride = element.size, alignment = element.alignment();
        if ((stride_source && !extent(stride_source, stride)) || (alignment_source && !extent(alignment_source, alignment)))
        {
            return false;
        }
        if (stride > memory::k_byte_size_ceiling)
        {
            return fail(EReason::storage_limit, stride_source);
        }
        if ((stride < element.size) || ((stride % element.alignment()) != 0u) || ((element.size == 0u) && (stride != 0u)))
        {
            return fail(EReason::invalid_layout, stride_source);
        }
        if (alignment > memory::k_byte_size_ceiling)
        {
            return fail(EReason::storage_limit, alignment_source);
        }
        if (!alignment || (alignment & (alignment - 1u)) || (alignment < element.alignment()) || ((element.size == 0u) && (alignment != 1u)))
        {
            return fail(EReason::invalid_layout, alignment_source);
        }
        if (alignment > k_max_alignment)
        {
            return fail(EReason::unsupported_feature, alignment_source);
        }
        if ((stride != 0u) && (count > (memory::k_byte_size_ceiling / stride)))
        {
            return fail(EReason::storage_limit, descriptor);
        }
        std::uint64_t size{};
        if (!align((count * stride), alignment, size, (alignment_source ? alignment_source : descriptor)))
        {
            return false;
        }
        array_record.size = static_cast<std::uint32_t>(size);
        array_record.alignment_log2 = element.alignment_log2;
        while (array_record.alignment() < alignment)
        {
            ++array_record.alignment_log2;
        }
        array_record.stride = static_cast<std::uint32_t>(stride);
        array_record.set_flag(TTypeRecord::k_gaps, element.flag(TTypeRecord::k_gaps) || (stride > element.size) || (size > (count * stride)));
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
    if (depth >= document_limits::k_max_recursion_depth)
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
    if (!shape(type_record.source, { "members", "detail", "decorations" }, { "members" }) || !decorations(type_record.source))
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
    if (!grow(m_schema.m_members, type_record.count, members) ||
        !grow(m_schema.m_member_names, type_record.count, members) ||
        !grow(m_schema.m_member_order, type_record.count, members))
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
        if (!shape(member_source, { "type", "default", "offset", "decorations" }, { "type" }) || !decorations(member_source))
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
    order_names(m_schema.m_members, m_schema.m_member_names, type_record.first, type_record.count);
    return structure_layout(type_record, explicit_offsets, has_storage);
}

bool CResolver::order_members(const TTypeRecord& type_record, const bool explicit_offsets, std::uint32_t& overlap, std::uint32_t& related) noexcept
{
    overlap = related = UINT32_MAX;
    if (type_record.count == 0u)
    {
        return true;
    }
    std::uint32_t* const order = m_schema.m_member_order.data() + type_record.first;
    for (std::uint32_t ordinal = 0u; ordinal < type_record.count; ++ordinal)
    {
        order[ordinal] = ordinal;
    }

    //  Natural layout already follows declaration order, including empty members.
    if (!explicit_offsets || (type_record.count < 2u))
    {
        return true;
    }
    const CResolvedSchema::SMemberRecord* const members = m_schema.m_members.data() + type_record.first;
    std::sort(order, (order + type_record.count),
        [members](const std::uint32_t a, const std::uint32_t b) noexcept
        {
            return (members[a].offset < members[b].offset) ||
                ((members[a].offset == members[b].offset) && (a < b));
        });

    //  At each start offset, the lowest active declaration ordinal gives the
    //  earliest diagnostic pair involving that member. Expired entries below
    //  the heap root may remain until exposed; each entry is pushed/popped once.
    TPodVector<std::uint32_t> active;
    if (!active.allocate(type_record.count))
    {
        return fail(EReason::allocation_failed, type_record.source);
    }
    const auto later_ordinal = [](const std::uint32_t a, const std::uint32_t b) noexcept { return a > b; };
    for (std::uint32_t position = 0u; position < type_record.count; ++position)
    {
        const std::uint32_t ordinal = order[position];
        const CResolvedSchema::SMemberRecord& member = members[ordinal];
        if (member.size == 0u)
        {
            continue;
        }
        while (active.size() != 0u)
        {
            const CResolvedSchema::SMemberRecord& prior = members[active[0u]];

            //  Layout bounds have not been checked yet; an invalid end may
            //  exceed the 32-bit range. Report it later in declaration order.
            const std::uint64_t end = static_cast<std::uint64_t>(prior.offset) + prior.size;
            if (end > member.offset)
            {
                break;
            }
            std::pop_heap(active.data(), (active.data() + active.size()), later_ordinal);
            (void)active.set_size(active.size() - 1u);
        }
        if (active.size() != 0u)
        {
            const std::uint32_t later = std::max(ordinal, active[0u]);
            const std::uint32_t earlier = std::min(ordinal, active[0u]);
            if ((later < overlap) || ((later == overlap) && (earlier < related)))
            {
                overlap = later;
                related = earlier;
            }
        }

        //  The complete capacity was reserved above; this append cannot grow it.
        (void)active.push_back(ordinal);
        std::push_heap(active.data(), (active.data() + active.size()), later_ordinal);
    }
    return true;
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
            if (declared_value > k_max_alignment)
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
    std::uint32_t overlap{}, related{};
    if (!order_members(type_record, explicit_offsets, overlap, related))
    {
        return false;
    }
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
            if (ordinal == overlap)
            {
                const CResolvedSchema::SMemberRecord& prior = m_schema.m_members[type_record.first + related];
                fail(EReason::invalid_layout, member.source, prior.source);
                m_diagnostic.ranges_available = true;
                m_diagnostic.range_begin = member_offset;
                m_diagnostic.range_end = member_end;
                m_diagnostic.related_begin = prior.offset;
                m_diagnostic.related_end = static_cast<std::uint64_t>(prior.offset) + prior.size;
                return false;
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
        if (m_document.value_kind(source) == EDocumentValueKind::string)
        {
            const std::uint32_t found = find_named_record(m_schema.m_labels, m_schema.m_label_names,
                type_record.first, type_record.count, m_document.find_property_name_id(m_document.string_value(source)));
            if (found != UINT32_MAX)
            {
                result = m_schema.m_labels[found].value;
                return true;
            }
        }
        return fail(EReason::invalid_default, source);
    }
    if (type_record.category != ECategory::primitive)
    {
        return fail(EReason::invalid_default, source);
    }
    EReason reason{ EReason::none };
    return detail::convert_primitive(m_document, source, type_record.primitive,
        static_cast<unsigned>(type_record.size), result, reason) || fail(reason, source);
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
    if (depth >= document_limits::k_max_recursion_depth)
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
    if (!shape(type_record.source, { "storage", "members", "decorations" }, { "storage", "members" }) ||
        !decorations(type_record.source) || !storage(type_record))
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
    if (!grow(m_schema.m_fields, type_record.count, members) ||
        !grow(m_schema.m_field_names, type_record.count, members))
    {
        return false;
    }
    std::uint64_t used{};
    std::uint32_t field_slot = type_record.first;
    for (CSchemaHandle field_source = m_document.first_child(members); field_source; field_source = m_document.next_sibling(field_source), ++field_slot)
    {
        m_context_member = field_source;
        m_stage = EStage::declarations;
        if (!shape(field_source, { "type", "mask", "interpretation", "default", "decorations" }, { "type", "mask" }) ||
            !decorations(field_source))
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
        if (logical.category == ECategory::enumeration &&
            (!resolver_util::fits(m_schema.m_labels[m_schema.m_label_values[logical.first]].value, field.signed_value, field.width) ||
                !resolver_util::fits(m_schema.m_labels[m_schema.m_label_values[logical.first + logical.count - 1u]].value,
                    field.signed_value, field.width)))
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
            const CStringView interpretation = m_document.string_value(interpretation_source);
            if (resolver_util::equal(interpretation, "unorm"))
            {
                field.interpretation = EInterpretation::unorm;
            }
            else if (resolver_util::equal(interpretation, "snorm"))
            {
                field.interpretation = EInterpretation::snorm;
            }
            else
            {
                return fail(EReason::unsupported_feature, interpretation_source);
            }
            if ((logical.category != ECategory::primitive) || !resolver_util::integer(logical.primitive) ||
                (field.signed_value != (field.interpretation == EInterpretation::snorm)) ||
                ((field.interpretation == EInterpretation::snorm) && (field.width < 2u)))
            {
                return fail(EReason::invalid_range, interpretation_source);
            }
        }
        const CSchemaHandle def = property(field_source, "default");
        m_stage = EStage::defaults;
        if ((field.interpretation != EInterpretation::ordinary) && def)
        {
            SScalar value;
            if (m_document.value_kind(def) == EDocumentValueKind::integer)
            {
                if (!scalar(field.type, def, value))
                {
                    return false;
                }
            }
            else
            {
                EReason reason{ EReason::none };
                if (!detail::convert_normalised(m_document, def, field.interpretation, field.width, value, reason))
                {
                    return fail(reason, def);
                }
            }
            if (!resolver_util::fits(value, field.signed_value, static_cast<unsigned>(logical.size * 8u)) ||
                !resolver_util::fits(value, field.signed_value, field.width))
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
    order_names(m_schema.m_fields, m_schema.m_field_names, type_record.first, type_record.count);
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
bool detail::SDocumentQueryAccess::backs(const CResolvedSchema& schema, const CLiveDocument& document) noexcept
{
    return read(schema.m_document).backs(document);
}

}   // namespace schema

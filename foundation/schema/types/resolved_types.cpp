
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    resolved_types.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Resolve live/baked definitions, references, defaults and physical layouts.

#include "schema/resolved_types.hpp"
#include "schema/operations/operation_meter.hpp"
#include "schema/values/reference_value.hpp"
#include "schema/blocks/data_block_format.hpp"
#include "schema/types/type_compatibility.hpp"
#include "schema/values/value_conversion.hpp"
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

static std::uint64_t occurrence_key(const CTypesHandle handle) noexcept
{
    return detail::STypesHandleAccess::occurrence(handle).query_value();
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
//  CResolvedTypes: ownership and read-only access
//==============================================================================

void CResolvedTypes::STypeRecord::set_flag(const std::uint8_t mask, const bool value) noexcept
{
    control = static_cast<std::uint8_t>((control & ~mask) | (value ? mask : 0u));
}

void CResolvedTypes::STypeRecord::set_state(const std::uint8_t value) noexcept
{
    control = static_cast<std::uint8_t>((control & ~k_state_mask) | (value << k_state_shift));
}

CTypesIndex CResolvedTypes::index(const std::uint32_t kind, const std::uint32_t ordinal) noexcept
{
    return CTypesIndex{ (kind << resolver_util::k_index_kind_shift) | (ordinal + 1u) };
}

std::uint32_t CResolvedTypes::ordinal(const CTypesIndex i) noexcept
{
    return (i.query_value() & resolver_util::k_limit) - 1u;
}

bool CResolvedTypes::is_kind(const CTypesIndex i, const std::uint32_t kind, const std::size_t size) noexcept
{
    return i.is_valid() && ((i.query_value() >> resolver_util::k_index_kind_shift) == kind) && (ordinal(i) < size);
}

CResolvedTypes::CResolvedTypes(CResolvedTypes&& source) noexcept
{
    *this = std::move(source);
}

CResolvedTypes& CResolvedTypes::operator=(CResolvedTypes&& source) noexcept
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

void CResolvedTypes::clear() noexcept
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

void CResolvedTypes::rebind_live_document(const CLiveDocument& document) noexcept
{
    if (m_ready)
    {
        m_document = CTypesDocumentQuery{ document };
    }
}

const CResolvedTypes::STypeRecord* CResolvedTypes::type_record(const CTypesIndex i) const noexcept
{
    return is_kind(i, resolver_util::k_type, m_types.size()) ? &m_types[ordinal(i)] : nullptr;
}

//  Name IDs belong to one document. Keep declaration records in their original
//  order and search small ordinal permutations with integer comparisons.
template<class T>
[[nodiscard]] static bool order_names(const TPodVector<T>& records, TPodVector<std::uint32_t>& order,
    const std::uint32_t first, const std::uint32_t count) noexcept
{
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        if (!detail::data_work()) return false;
        order[first + i] = first + i;
    }
    if (count > 1u)
    {
        if (!detail::data_sort((order.data() + first), (order.data() + first + count),
            [&records](const std::uint32_t a, const std::uint32_t b)
            {
                return records[a].name.query_value() < records[b].name.query_value();
            })) return false;
    }
    return true;
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

CTypesIndex CResolvedTypes::lookup_type(const CStringView& name) const noexcept
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
    return found != UINT32_MAX ? index(resolver_util::k_type, found) : CTypesIndex{};
}

CTypesIndex CResolvedTypes::find_type(const CStringView& name) const noexcept
{
    return m_ready ? lookup_type(name) : CTypesIndex{};
}

const CResolvedTypes::SNonStorageRecord* CResolvedTypes::non_storage_record(const CTypesIndex i) const noexcept
{
    return is_kind(i, resolver_util::k_non_storage, m_non_storage.size()) ? &m_non_storage[ordinal(i)] : nullptr;
}

CTypesIndex CResolvedTypes::lookup_declaration(const CStringView& name) const noexcept
{
    const CTypesIndex physical = lookup_type(name);
    if (physical)
    {
        return physical;
    }
    const std::uint32_t found = find_named_record(m_non_storage, m_non_storage_names, 0u,
        static_cast<std::uint32_t>(m_non_storage.size()), m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_non_storage, found) : CTypesIndex{};
}

CTypesIndex CResolvedTypes::find_declaration(const CStringView& name) const noexcept
{
    return m_ready ? lookup_declaration(name) : CTypesIndex{};
}

std::uint32_t CResolvedTypes::declaration_count() const noexcept
{
    return m_ready ? (m_definitions + static_cast<std::uint32_t>(m_non_storage.size())) : 0u;
}

CTypesIndex CResolvedTypes::declaration_at(const std::uint32_t n) const noexcept
{
    if (!m_ready || (n >= declaration_count()))
    {
        return {};
    }
    return (n < m_definitions) ? definition_at(n) : index(resolver_util::k_non_storage, n - m_definitions);
}

bool CResolvedTypes::declaration(const CTypesIndex i, SDeclaration& result) const noexcept
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

bool CResolvedTypes::association(const CTypesIndex i, SAssociation& result) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(i);
    if (!m_ready || !record || (record->category != EDeclarationCategory::association))
    {
        return false;
    }
    result = { record->target };
    return true;
}

bool CResolvedTypes::interface_info(const CTypesIndex i, SInterface& result) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(i);
    if (!m_ready || !record || (record->category != EDeclarationCategory::interface_type))
    {
        return false;
    }
    result = { record->count };
    return true;
}

CTypesIndex CResolvedTypes::interface_member_at(const CTypesIndex owner, const std::uint32_t n) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(owner);
    SInterface info;
    return (interface_info(owner, info) && (n < info.count)) ? index(resolver_util::k_interface_member, record->first + n) : CTypesIndex{};
}

CTypesIndex CResolvedTypes::find_interface_member(const CTypesIndex owner, const CStringView& name) const noexcept
{
    const SNonStorageRecord* const record = non_storage_record(owner);
    SInterface info;
    if (!interface_info(owner, info))
    {
        return {};
    }
    const std::uint32_t found = find_named_record(m_interface_members, m_interface_member_names,
        record->first, record->count, m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_interface_member, found) : CTypesIndex{};
}

bool CResolvedTypes::interface_member(const CTypesIndex i, SInterfaceMember& result) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_interface_member, m_interface_members.size()))
    {
        return false;
    }
    result = m_interface_members[ordinal(i)];
    return true;
}

CStringView CResolvedTypes::name(const CPropertyNameId id) const noexcept
{
    return m_ready ? m_document.property_name(id) : CStringView{};
}

std::uint32_t CResolvedTypes::definition_count() const noexcept
{
    return m_ready ? m_definitions : 0u;
}

CTypesIndex CResolvedTypes::definition_at(const std::uint32_t n) const noexcept
{
    return (m_ready && (n < m_definitions)) ? index(resolver_util::k_type, (resolver_util::k_primitive_count + n)) : CTypesIndex{};
}

bool CResolvedTypes::definition_ordinal(const CTypesIndex type, std::uint32_t& result) const noexcept
{
    if (!m_ready || !is_kind(type, resolver_util::k_type, (resolver_util::k_primitive_count + m_definitions)) ||
        (ordinal(type) < resolver_util::k_primitive_count))
    {
        return false;
    }
    result = ordinal(type) - resolver_util::k_primitive_count;
    return true;
}

bool CResolvedTypes::type(const CTypesIndex index, SType& result) const noexcept
{
    if (!detail::data_work()) return false;
    const STypeRecord* const record = type_record(index);
    if (!m_ready || !record)
    {
        return false;
    }
    result = {
        record->category, record->primitive, record->name, record->source, record->size,
        record->alignment(), record->stride, record->count, record->related,
        record->flag(STypeRecord::k_gaps), record->flag(STypeRecord::k_internal), record->flag(STypeRecord::k_named_components),
        record->flag(STypeRecord::k_nullable), record->flag(STypeRecord::k_references) };
    return true;
}

CTypesIndex CResolvedTypes::child_at(
    const CTypesIndex i, const std::uint32_t n, const ECategory category, const std::uint32_t kind) const noexcept
{
    const STypeRecord* const r = type_record(i);
    return (m_ready && r && (r->category == category) && (n < r->count)) ? index(kind, (r->first + n)) : CTypesIndex{};
}

CTypesIndex CResolvedTypes::member_at(const CTypesIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::structure, resolver_util::k_member);
}

CTypesIndex CResolvedTypes::physical_member_at(const CTypesIndex i, const std::uint32_t n) const noexcept
{
    const STypeRecord* const record = type_record(i);
    if (!m_ready || !record || (record->category != ECategory::structure) || (n >= record->count))
    {
        return {};
    }
    return index(resolver_util::k_member, (record->first + m_member_order[record->first + n]));
}

CTypesIndex CResolvedTypes::label_at(const CTypesIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::enumeration, resolver_util::k_label);
}

CTypesIndex CResolvedTypes::field_at(const CTypesIndex i, const std::uint32_t n) const noexcept
{
    return child_at(i, n, ECategory::bit_structure, resolver_util::k_field);
}

bool CResolvedTypes::member(const CTypesIndex i, SMember& r) const noexcept
{
    if (!detail::data_work()) return false;
    if (!m_ready || !is_kind(i, resolver_util::k_member, m_members.size()))
    {
        return false;
    }
    const SMemberRecord& record = m_members[ordinal(i)];
    r = { record.name, record.source, record.type, record.default_description, record.offset, record.size };
    return true;
}

bool CResolvedTypes::label(const CTypesIndex i, SLabel& r) const noexcept
{
    if (!m_ready || !is_kind(i, resolver_util::k_label, m_labels.size()))
    {
        return false;
    }
    r = m_labels[ordinal(i)];
    return true;
}

bool CResolvedTypes::field(const CTypesIndex i, SField& r) const noexcept
{
    if (!detail::data_work()) return false;
    if (!m_ready || !is_kind(i, resolver_util::k_field, m_fields.size()))
    {
        return false;
    }
    r = m_fields[ordinal(i)];
    return true;
}

CTypesHandle CResolvedTypes::decoration_object(const CTypesIndex owner) const noexcept
{
    if (!m_ready)
    {
        return {};
    }
    CTypesHandle source;
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

bool CResolvedTypes::read_decoration(const CTypesHandle source, SDecoration& result) const noexcept
{
    if (m_document.value_kind(source) != EDocumentValueKind::string)
    {
        return false;
    }
    result = { m_document.name(source), m_document.string_value(source), source };
    return true;
}

bool CResolvedTypes::find_decoration(const CTypesIndex owner, const CStringView& key, SDecoration& result) const noexcept
{
    return read_decoration(m_document.object_child(decoration_object(owner), key), result);
}

bool CResolvedTypes::first_decoration(const CTypesIndex owner, SDecoration& result) const noexcept
{
    return read_decoration(m_document.first_child(decoration_object(owner)), result);
}

bool CResolvedTypes::next_decoration(const CTypesHandle current, SDecoration& result) const noexcept
{
    if (!m_ready)
    {
        return false;
    }
    const CTypesHandle object = m_document.parent(current);
    if (!object || (decoration_object(map_occurrence(m_document.parent(object))) != object))
    {
        return false;
    }
    return read_decoration(m_document.next_sibling(current), result);
}

template<class T>
CResolvedTypes::SRecordRange<T> CResolvedTypes::child_range(const CTypesIndex type, const ECategory category, const TPodVector<T>& records) const noexcept
{
    const STypeRecord* const parent = type_record(type);
    if (!m_ready || !parent || (parent->category != category) ||
        (parent->first > records.size()) || (parent->count > (records.size() - parent->first)))
    {
        return {};
    }
    return { parent->first, parent->count };
}

CTypesIndex CResolvedTypes::find_member(const CTypesIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SMemberRecord> members = child_range(type, ECategory::structure, m_members);
    const std::uint32_t found = find_named_record(m_members, m_member_names, members.first, members.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_member, found) : CTypesIndex{};
}

bool CResolvedTypes::member_ordinal(const CTypesIndex type, const CTypesIndex member, std::uint32_t& result) const noexcept
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

CTypesIndex CResolvedTypes::find_label(const CTypesIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SLabel> labels = child_range(type, ECategory::enumeration, m_labels);
    const std::uint32_t found = find_named_record(m_labels, m_label_names, labels.first, labels.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_label, found) : CTypesIndex{};
}

CTypesIndex CResolvedTypes::find_field(const CTypesIndex type, const CStringView& name) const noexcept
{
    const SRecordRange<SField> fields = child_range(type, ECategory::bit_structure, m_fields);
    const std::uint32_t found = find_named_record(m_fields, m_field_names, fields.first, fields.count,
        m_document.find_property_name_id(name));
    return found != UINT32_MAX ? index(resolver_util::k_field, found) : CTypesIndex{};
}

[[nodiscard]] static std::uint64_t label_value_key(const SScalar& value) noexcept
{
    return value.kind == EScalar::signed_integer ?
        static_cast<std::uint64_t>(value.value.signed_value) ^ (UINT64_C(1) << 63u) : value.value.unsigned_value;
}

CTypesIndex CResolvedTypes::first_label_for_value(const CTypesIndex type, const SScalar& value) const noexcept
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
        index(resolver_util::k_label, m_label_values[low]) : CTypesIndex{};
}

bool CResolvedTypes::default_value(const CTypesIndex type, const CTypesIndex description, SDefault& result) const noexcept
{
    if (!detail::data_work()) return false;
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
    if ((type_info->category == ECategory::text) || (type_info->category == ECategory::span))
    {
        value.kind = (type_info->category == ECategory::text) ? EDefault::text : EDefault::span;
        value.absent = description ? (value.scalar.value.signed_value == -1) : type_info->flag(STypeRecord::k_nullable);
        if (description && !value.absent && (type_info->category == ECategory::text))
        {
            value.text = m_document.string_value(value.source);
        }
    }
    result = value;
    return true;
}

bool CResolvedTypes::default_element(const CTypesIndex type, const CTypesIndex description, const std::uint32_t element_ordinal, CTypesIndex& result) const noexcept
{
    const STypeRecord* const type_info = type_record(type);
    SDefault value;
    if (!default_value(type, description, value) || (type_info->category != ECategory::array) || (element_ordinal >= type_info->count))
    {
        return false;
    }
    result = (element_ordinal < value.supplied_count) ? index(resolver_util::k_default, (m_defaults[ordinal(description)].first + element_ordinal)) : CTypesIndex{};
    return true;
}

CTypesIndex CResolvedTypes::map_occurrence(const CTypesHandle occurrence) const noexcept
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
    return ((low < m_mapping.size()) && (m_mapping[low].source == occurrence)) ? m_mapping[low].target : CTypesIndex{};
}

SRecordSizes CResolvedTypes::record_sizes() noexcept
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
    CResolver(CResolvedTypes& owner, SDiagnostic& diagnostic) noexcept;
    bool run() noexcept;

private:
    using TTypeRecord = CResolvedTypes::STypeRecord;

    //  Only continuations grow with input depth. The allocator owns this bounded
    //  scratch; the native call chain and per-step layout/conversion scratch do
    //  not grow when a dependency suspends its parent.
    enum class EReferencePhase : std::uint8_t { descend, descriptor, arrays };
    struct SReferenceFrame
    {
        CTypesHandle origin, current;
        CTypesIndex result;
        unsigned depth{};
        EReferencePhase phase{};
        bool span{}, nullable{};
    };
    enum class ETypePhase : std::uint8_t { enter, member, reference, finish_member };
    struct STypeFrame
    {
        TTypeRecord record;
        SReferenceFrame reference;
        CTypesHandle next, first_member;
        std::uint64_t used{};
        CTypesIndex component;
        std::uint32_t slot{}, ordinal{};
        unsigned depth{};
        ETypePhase phase{};
        bool explicit_offsets{}, has_storage{};
    };
    static_assert(sizeof(STypeFrame) <= 160u);
    enum class EDeclarationPhase : std::uint8_t { enter, member, finish_member, complete };
    struct SDeclarationFrame
    {
        CResolvedTypes::SNonStorageRecord record;
        SInterfaceMember member;
        CTypesHandle next;
        std::uint32_t slot{}, ordinal{};
        unsigned depth{};
        EDeclarationPhase phase{};
    };
    struct SDefaultFrame
    {
        CTypesIndex type, element;
        CTypesHandle source, next;
        std::uint32_t slot{}, child_slot{};
        unsigned depth{};
    };
    static_assert(sizeof(SDeclarationFrame) <= 128u);
    static_assert(sizeof(SDefaultFrame) <= 64u);

    //  First-error reporting, checked storage and document shape.
    bool fail(const EReason reason, const CTypesHandle& at = {}, const CTypesHandle& related = {}) noexcept;
    template<class T> bool grow(TPodVector<T>& values, const std::uint32_t count, const CTypesHandle& at) noexcept;
    bool map(const CTypesHandle& at, const CTypesIndex target) noexcept;
    CTypesHandle property(const CTypesHandle& at, const char* const name) const noexcept;
    bool shape(
        const CTypesHandle& source, const std::initializer_list<const char*> allowed,
        const std::initializer_list<const char*> required = {}) noexcept;
    bool decorations(const CTypesHandle& owner) noexcept;
    bool declarations(const CTypesHandle& container, const ECategory category) noexcept;
    bool non_storage_declarations(const CTypesHandle& container, const EDeclarationCategory category) noexcept;
    bool resolve_non_storage(const CTypesIndex declaration, const unsigned depth) noexcept;
    bool push_declaration(const CTypesIndex declaration, const unsigned depth) noexcept;
    bool begin_declaration(SDeclarationFrame& frame) noexcept;
    bool declaration_reference(const CTypesHandle source, CTypesIndex& result) noexcept;
    bool names(const CTypesHandle& container, const bool array, const CTypesHandle& owner, const bool allow_empty = false) noexcept;

    //  References, scalar/default conversion and category layout.
    bool resolve_type(const CTypesIndex type_index, const unsigned depth) noexcept;
    bool push_type(const CTypesIndex type_index, const unsigned depth) noexcept;
    bool reference(SReferenceFrame& frame, CTypesIndex& dependency, unsigned& dependency_depth) noexcept;
    bool reference_target(const CTypesHandle source, CTypesIndex& result) noexcept;
    bool begin_members(STypeFrame& frame) noexcept;
    bool begin_member(STypeFrame& frame) noexcept;
    bool finish_members(STypeFrame& frame) noexcept;
    bool number(const CTypesHandle& source, const bool is_signed, const unsigned bit_width, SScalar& result) noexcept;
    bool extent(const CTypesHandle& at, std::uint64_t& out) noexcept;
    bool scalar(const CTypesIndex type, const CTypesHandle& source, SScalar& result) noexcept;
    bool default_record(const CTypesIndex type, const CTypesHandle& source, const std::uint32_t slot, const unsigned depth) noexcept;
    bool enter_default(SDefaultFrame& frame) noexcept;
    bool make_default(const CTypesIndex type, const CTypesHandle& source, CTypesIndex& result, const unsigned depth) noexcept;
    bool enumeration(TTypeRecord& type_record) noexcept;
    bool structure_member(STypeFrame& frame) noexcept;
    bool structure_layout(TTypeRecord& type_record, const bool explicit_offsets, const bool has_storage) noexcept;
    bool order_members(const TTypeRecord& type_record, const bool explicit_offsets, std::uint32_t& overlap, std::uint32_t& related) noexcept;
    bool bit_member(STypeFrame& frame) noexcept;
    bool storage(TTypeRecord& type_record) noexcept;
    bool add(const std::uint64_t a, const std::uint64_t b, std::uint64_t& out, const CTypesHandle& at) noexcept;
    bool align(const std::uint64_t value, const std::uint64_t alignment, std::uint64_t& out, const CTypesHandle& at) noexcept;

    //  Borrowed owner/input and current diagnostic context.
    CResolvedTypes& m_schema;
    const CTypesDocumentQuery& m_document;
    SDiagnostic& m_diagnostic;
    EStage m_stage{ EStage::declarations };
    CTypesHandle m_context_type, m_context_member;
    COrdinalHashIndex m_type_index;
    TPodVector<STypeFrame> m_type_frames;
    TPodVector<SDeclarationFrame> m_declaration_frames;
    TPodVector<SDefaultFrame> m_default_frames;
};

CResolver::CResolver(CResolvedTypes& owner, SDiagnostic& diagnostic) noexcept
    : m_schema(owner), m_document(owner.m_document), m_diagnostic(diagnostic)
{
}

bool CResolver::fail(const EReason reason, const CTypesHandle& at, const CTypesHandle& related) noexcept
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
bool CResolver::grow(TPodVector<T>& values, const std::uint32_t count, const CTypesHandle& at) noexcept
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

bool CResolver::map(const CTypesHandle& at, const CTypesIndex target) noexcept
{
    if (!grow(m_schema.m_mapping, 1u, at))
    {
        return false;
    }
    m_schema.m_mapping.last() = { at, target };
    return true;
}

CTypesHandle CResolver::property(const CTypesHandle& at, const char* const name) const noexcept
{
    return m_document.object_child(at, CStringView{ name });
}

bool CResolver::add(const std::uint64_t a, const std::uint64_t b, std::uint64_t& out, const CTypesHandle& at) noexcept
{
    if ((b > UINT64_MAX - a) || (a + b > memory::k_byte_size_ceiling))
    {
        return fail(EReason::storage_limit, at);
    }
    out = a + b;
    return true;
}

bool CResolver::align(const std::uint64_t value, const std::uint64_t alignment, std::uint64_t& out, const CTypesHandle& at) noexcept
{
    if (!alignment || (alignment > memory::k_byte_size_ceiling) || (alignment & (alignment - 1u)))
    {
        return fail(EReason::invalid_layout, at);
    }
    const std::uint64_t padding = (alignment - (value & (alignment - 1u))) & (alignment - 1u);
    return add(value, padding, out, at);
}

bool CResolver::shape(const CTypesHandle& source, const std::initializer_list<const char*> allowed, const std::initializer_list<const char*> required) noexcept
{
    if (!source)
    {
        return fail(EReason::missing_property, source);
    }
    if (m_document.value_kind(source) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, source);
    }
    for (CTypesHandle child = m_document.first_child(source); child; child = m_document.next_sibling(child))
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
        for (CTypesHandle prior = m_document.first_child(source); prior != child; prior = m_document.next_sibling(prior))
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

bool CResolver::decorations(const CTypesHandle& owner) noexcept
{
    const CTypesHandle object = property(owner, "decorations");
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
    for (CTypesHandle child = m_document.first_child(object); child; child = m_document.next_sibling(child))
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

bool CResolver::names(const CTypesHandle& container, const bool array, const CTypesHandle& owner, const bool allow_empty) noexcept
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
        for (CTypesHandle child = m_document.first_child(container); child; child = m_document.next_sibling(child), ++ordinal)
        {
            if (!names.push_back((static_cast<std::uint64_t>(m_document.name_id(child).query_value()) << 32u) | ordinal))
            {
                return fail(EReason::allocation_failed, child);
            }
        }
        if ((names.size() > 1u) && !detail::data_sort(names.data(), (names.data() + names.size()),
            [](const std::uint64_t a, const std::uint64_t b) noexcept { return a < b; }))
        {
            return fail(EReason::storage_limit);
        }
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
    CTypesHandle prior;
    for (CTypesHandle declaration = m_document.first_child(container); declaration; declaration = m_document.next_sibling(declaration), ++ordinal)
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

bool CResolver::declarations(const CTypesHandle& container, const ECategory category) noexcept
{
    if (m_document.value_kind(container) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, container);
    }
    for (CTypesHandle declaration = m_document.first_child(container); declaration; declaration = m_document.next_sibling(declaration))
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
        if (!m_type_index.insert(m_schema.m_definitions, COrdinalHashIndex::hash_key(name.query_value())))
        {
            return fail(EReason::storage_limit, declaration);
        }
        ++m_schema.m_definitions;
        if (!map(declaration, m_schema.index(resolver_util::k_type, static_cast<std::uint32_t>(m_schema.m_types.size() - 1u))))
        {
            return false;
        }
    }
    m_context_type = {};
    return true;
}

bool CResolver::non_storage_declarations(const CTypesHandle& container, const EDeclarationCategory category) noexcept
{
    if (m_document.value_kind(container) != EDocumentValueKind::object)
    {
        return fail(EReason::invalid_input, container);
    }
    for (CTypesHandle source = m_document.first_child(container); source; source = m_document.next_sibling(source))
    {
        m_context_type = source;
        const CStringView name = m_document.name(source);
        if (!valid_identifier(name))
        {
            return fail(EReason::invalid_identifier, source);
        }
        const CTypesIndex physical = m_schema.lookup_type(name);
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
        if (!m_type_index.insert(count, COrdinalHashIndex::hash_key(name_id.query_value())))
        {
            return fail(EReason::storage_limit, source);
        }
        if (!map(source, m_schema.index(resolver_util::k_non_storage, count)))
        {
            return false;
        }
    }
    m_context_type = {};
    return true;
}

bool CResolver::declaration_reference(const CTypesHandle source, CTypesIndex& result) noexcept
{
    m_stage = EStage::references;
    if (m_document.value_kind(source) != EDocumentValueKind::string)
    {
        return fail(EReason::invalid_input, source);
    }
    result = m_schema.lookup_declaration(m_document.string_value(source));
    if (!result) return fail(EReason::unknown_type, source);
    const auto* const record = m_schema.non_storage_record(result);
    if (record && (record->state == 1u))
    {
        return fail(EReason::cycle, source, record->source);
    }
    return map(source, result);
}

bool CResolver::push_declaration(const CTypesIndex declaration, const unsigned depth) noexcept
{
    const auto* const record = m_schema.non_storage_record(declaration);
    if (!record || (record->state == 2u))
    {
        return true;
    }
    if (!detail::data_depth(depth) || (depth >= document_limits::k_max_recursion_depth))
    {
        return fail(EReason::storage_limit, record->source);
    }
    SDeclarationFrame frame;
    frame.record = *record;
    frame.slot = m_schema.ordinal(declaration);
    frame.depth = depth;
    if (!m_declaration_frames.push_back(frame))
    {
        return fail(EReason::allocation_failed, record->source);
    }
    m_schema.m_non_storage[frame.slot].state = 1u;
    return true;
}

bool CResolver::begin_declaration(SDeclarationFrame& frame) noexcept
{
    auto& record = frame.record;
    m_stage = EStage::declarations;
    if (record.category == EDeclarationCategory::association)
    {
        return shape(record.source, { "target", "decorations" }, { "target" }) && decorations(record.source);
    }
    if (!shape(record.source, { "members", "decorations" }, { "members" }) || !decorations(record.source))
    {
        return false;
    }
    const CTypesHandle members = property(record.source, "members");
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
    frame.next = m_document.first_child(members);
    return true;
}

bool CResolver::resolve_non_storage(const CTypesIndex declaration, const unsigned depth) noexcept
{
    const CTypesHandle old_type = m_context_type, old_member = m_context_member;
    if (!push_declaration(declaration, depth)) return false;
    while (!m_declaration_frames.is_empty())
    {
        SDeclarationFrame& frame = m_declaration_frames.last();
        m_context_type = frame.record.source;
        m_context_member = frame.next ? frame.next : frame.member.source;
        switch (frame.phase)
        {
            case EDeclarationPhase::enter:
            {
                if (!begin_declaration(frame)) return false;
                if (frame.record.category == EDeclarationCategory::association)
                {
                    frame.phase = EDeclarationPhase::complete;
                    const CTypesHandle target = property(frame.record.source, "target");
                    if (m_document.value_kind(target) != EDocumentValueKind::null_value)
                    {
                        if (!declaration_reference(target, frame.record.target))
                        {
                            return false;
                        }
                        if (!push_declaration(frame.record.target, (frame.depth + 1u)))
                        {
                            return false;
                        }
                    }
                }
                else frame.phase = EDeclarationPhase::member;
                break;
            }
            case EDeclarationPhase::member:
            {
                if (!frame.next)
                {
                    if (!order_names(m_schema.m_interface_members, m_schema.m_interface_member_names,
                        frame.record.first, frame.record.count))
                    {
                        return fail(EReason::storage_limit);
                    }
                    frame.phase = EDeclarationPhase::complete;
                    break;
                }
                const CTypesHandle source = frame.next;
                m_stage = EStage::declarations;
                if (!shape(source, { "type", "count", "decorations" }, { "type" }) || !decorations(source))
                {
                    return false;
                }
                frame.member = {};
                frame.member.name = m_document.name_id(source);
                frame.member.source = source;
                const CTypesHandle count_source = property(source, "count");
                std::uint64_t count = 1u;
                if (count_source && !extent(count_source, count))
                {
                    return false;
                }
                if ((count == 0u) || (count > UINT32_MAX))
                {
                    return fail(EReason::invalid_range, count_source);
                }
                frame.member.count = static_cast<std::uint32_t>(count);
                if (!declaration_reference(property(source, "type"), frame.member.declaration))
                {
                    return false;
                }
                frame.phase = EDeclarationPhase::finish_member;
                if (!push_declaration(frame.member.declaration, (frame.depth + 1u)))
                {
                    return false;
                }
                break;
            }
            case EDeclarationPhase::finish_member:
            {
                const std::uint32_t slot = frame.record.first + frame.ordinal;
                m_schema.m_interface_members[slot] = frame.member;
                if (!map(frame.next, m_schema.index(resolver_util::k_interface_member, slot)))
                {
                    return false;
                }
                ++frame.ordinal;
                frame.next = m_document.next_sibling(frame.next);
                frame.phase = EDeclarationPhase::member;
                break;
            }
            case EDeclarationPhase::complete:
            {
                frame.record.state = 2u;
                m_schema.m_non_storage[frame.slot] = frame.record;
                (void)m_declaration_frames.discard_back();
                break;
            }
        }
    }
    m_context_type = old_type;
    m_context_member = old_member;
    return true;
}

bool CResolver::run() noexcept
{
    detail::SDataBlockRoot root;
    SDataBlockDiagnostic diagnostic;
    const bool envelope = static_cast<bool>(m_document.object_child(m_document.root(), CStringView{ "fileType" }));
    if (!detail::read_data_block_root(detail::SDocumentQueryAccess::read(m_document), root, diagnostic, envelope))
    {
        const detail::SOccurrence value = diagnostic.location.live_node ? detail::SOccurrence{ diagnostic.location.live_node } : detail::SOccurrence{ diagnostic.location.baked_value };
        return fail((diagnostic.reason == EDataBlockReason::unknown_property) ? EReason::unknown_property :
            ((diagnostic.reason == EDataBlockReason::missing_property) ? EReason::missing_property : EReason::invalid_input),
            (value ? detail::STypesHandleAccess::make(value) : m_document.root()));
    }
    if (!root.types)
    {
        return fail(EReason::missing_property, m_document.root());
    }
    const CTypesHandle types = detail::STypesHandleAccess::make(root.types);
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
    for (CTypesHandle declaration_group = m_document.first_child(types); declaration_group; declaration_group = m_document.next_sibling(declaration_group))
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
    if (!order_names(m_schema.m_types, m_schema.m_type_names, resolver_util::k_primitive_count, m_schema.m_definitions)) return fail(EReason::storage_limit);
    m_type_index = {};
    for (CTypesHandle group = m_document.first_child(types); group; group = m_document.next_sibling(group))
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
    if (!order_names(m_schema.m_non_storage, m_schema.m_non_storage_names, 0u, static_cast<std::uint32_t>(m_schema.m_non_storage.size()))) return fail(EReason::storage_limit);
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
        if (!detail::data_sort(m_schema.m_mapping.data(), (m_schema.m_mapping.data() + m_schema.m_mapping.size()),
            [](const CResolvedTypes::SMapping& a, const CResolvedTypes::SMapping& b)
            {
                return resolver_util::occurrence_key(a.source) < resolver_util::occurrence_key(b.source);
            })) return fail(EReason::storage_limit);
    }
    return true;
}

bool CResolver::number(const CTypesHandle& source, const bool is_signed, const unsigned bit_width, SScalar& result) noexcept
{
    EReason reason{ EReason::none };
    return detail::convert_number(m_document, source, is_signed, bit_width, result, reason) || fail(reason, source);
}

bool CResolver::extent(const CTypesHandle& at, std::uint64_t& out) noexcept
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
    const CTypesHandle storage_source = property(type_record.source, "storage");
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
    const CTypesHandle values = property(type_record.source, "values");
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
    for (CTypesHandle label_source = m_document.first_child(values); label_source; label_source = m_document.next_sibling(label_source), ++label_slot)
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
    if (!order_names(m_schema.m_labels, m_schema.m_label_names, type_record.first, type_record.count)) return fail(EReason::storage_limit);
    for (std::uint32_t i = 0u; i < type_record.count; ++i)
    {
        m_schema.m_label_values[type_record.first + i] = type_record.first + i;
    }
    if (type_record.count > 1u)
    {
        if (!detail::data_sort((m_schema.m_label_values.data() + type_record.first),
            (m_schema.m_label_values.data() + type_record.first + type_record.count),
            [this](const std::uint32_t a, const std::uint32_t b)
            {
                const auto av = label_value_key(m_schema.m_labels[a].value), bv = label_value_key(m_schema.m_labels[b].value);
                return (av < bv) || ((av == bv) && (a < b));
            })) return fail(EReason::storage_limit);
    }
    return true;
}

bool CResolver::reference_target(const CTypesHandle source, CTypesIndex& result) noexcept
{
    result = m_schema.lookup_type(m_document.string_value(source));
    if (!result) return fail(EReason::unknown_type, source);
    return map(source, result);
}

bool CResolver::reference(SReferenceFrame& frame, CTypesIndex& dependency, unsigned& dependency_depth) noexcept
{
    dependency = {};
    CTypesHandle& current = frame.current;
    const CTypesHandle origin = frame.origin;
    CTypesIndex& result = frame.result;
    while (frame.phase == EReferencePhase::descend)
    {
        if (!detail::data_depth(frame.depth) || (frame.depth >= document_limits::k_max_recursion_depth))
        {
            return fail(EReason::storage_limit, current);
        }
        if (m_document.value_kind(current) == EDocumentValueKind::string)
        {
            if (!reference_target(current, result))
            {
                return false;
            }
            frame.phase = EReferencePhase::arrays;
            dependency = result;
            dependency_depth = frame.depth + 1u;
            return true;
        }
        const CTypesHandle kind = property(current, "kind");
        if (kind)
        {
            const bool text = resolver_util::equal(m_document.string_value(kind), "text");
            frame.span = resolver_util::equal(m_document.string_value(kind), "span");
            if ((!text && !frame.span) || !shape(current, (frame.span ?
                std::initializer_list<const char*>{ "kind", "element", "nullable" } :
                std::initializer_list<const char*>{ "kind", "nullable" }), { "kind" }))
            {
                return fail(EReason::invalid_input, current);
            }
            const CTypesHandle nullable_source = property(current, "nullable");
            if (nullable_source && !m_document.boolean_value(nullable_source, frame.nullable))
            {
                return fail(EReason::invalid_input, nullable_source);
            }
            frame.phase = EReferencePhase::descriptor;
            if (frame.span)
            {
                const CTypesHandle element = property(current, "element");
                if (!element)
                {
                    return fail(EReason::missing_property, current);
                }
                if (m_document.value_kind(element) != EDocumentValueKind::string)
                {
                    return fail(EReason::invalid_input, element);
                }
                if (!detail::data_depth(frame.depth + 1u) || ((frame.depth + 1u) >= document_limits::k_max_recursion_depth))
                {
                    return fail(EReason::storage_limit, element);
                }
                if (!reference_target(element, result))
                {
                    return false;
                }
                dependency = result;
                dependency_depth = frame.depth + 2u;
                return true;
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
        ++frame.depth;
    }
    if (frame.phase == EReferencePhase::descriptor)
    {
        TTypeRecord record;
        record.category = frame.span ? ECategory::span : ECategory::text;
        record.source = current;
        record.size = record.stride = frame.span ? 12u : 4u;
        record.alignment_log2 = 2u;
        record.related = result;
        record.set_flag(TTypeRecord::k_references, true);
        record.set_flag(TTypeRecord::k_nullable, frame.nullable);
        record.set_state(2u);
        if (!grow(m_schema.m_types, 1u, current))
        {
            return false;
        }
        m_schema.m_types.last() = record;
        result = m_schema.index(resolver_util::k_type, static_cast<std::uint32_t>(m_schema.m_types.size() - 1u));
        if (!map(current, result)) return false;
        frame.phase = EReferencePhase::arrays;
    }

    //  Each nested descriptor is an element child of its containing descriptor.
    //  Walk back outward, retaining the original inner-before-outer slot order.
    while (current != origin)
    {
        const CTypesHandle descriptor = m_document.parent(current);
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
        const CTypesHandle stride_source = property(descriptor, "stride");
        const CTypesHandle alignment_source = property(descriptor, "alignment");
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
        array_record.set_flag(TTypeRecord::k_gaps, (element.flag(TTypeRecord::k_gaps) || (stride > element.size) || (size > (count * stride))));
        array_record.set_state(2u);
        array_record.set_flag(TTypeRecord::k_references, element.flag(TTypeRecord::k_references));
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

bool CResolver::push_type(const CTypesIndex type_index, const unsigned depth) noexcept
{
    const std::uint32_t slot = m_schema.ordinal(type_index);
    const TTypeRecord record = m_schema.m_types[slot];
    if (record.state() == 2u) return true;
    if (record.state() == 1u)
    {
        return fail(EReason::cycle, record.source, m_context_type);
    }
    if (!detail::data_depth(depth) || (depth >= document_limits::k_max_recursion_depth))
    {
        return fail(EReason::storage_limit, record.source);
    }
    STypeFrame frame;
    frame.record = record;
    frame.slot = slot;
    frame.depth = depth;
    if (!m_type_frames.push_back(frame))
    {
        return fail(EReason::allocation_failed, record.source);
    }
    m_schema.m_types[slot].set_state(1u);
    return true;
}

bool CResolver::resolve_type(const CTypesIndex type_index, const unsigned depth) noexcept
{
    const CTypesHandle old_type = m_context_type, old_member = m_context_member;
    if (!push_type(type_index, depth)) return false;
    while (!m_type_frames.is_empty())
    {
        STypeFrame& frame = m_type_frames.last();
        m_context_type = frame.record.source;
        if (frame.phase == ETypePhase::enter) m_context_member = {};
        else if (frame.next) m_context_member = frame.next;
        bool complete{};
        switch (frame.phase)
        {
            case ETypePhase::enter:
            {
                m_stage = EStage::declarations;
                if (frame.record.category == ECategory::enumeration)
                {
                    if (!enumeration(frame.record))
                    {
                        return false;
                    }
                    complete = true;
                }
                else
                {
                    if (!begin_members(frame)) return false;
                    frame.phase = ETypePhase::member;
                }
                break;
            }
            case ETypePhase::member:
            {
                if (!frame.next)
                {
                    if (frame.record.category == ECategory::structure)
                    {
                        m_context_member = {};
                    }
                    if (!finish_members(frame))
                    {
                        return false;
                    }
                    complete = true;
                }
                else
                {
                    if (!begin_member(frame)) return false;
                    frame.phase = ETypePhase::reference;
                }
                break;
            }
            case ETypePhase::reference:
            {
                CTypesIndex dependency;
                unsigned dependency_depth{};
                if (!reference(frame.reference, dependency, dependency_depth))
                {
                    return false;
                }
                if (dependency)
                {
                    //  Pushing may relocate the vector; no frame reference crosses it.
                    if (!push_type(dependency, dependency_depth))
                    {
                        return false;
                    }
                    continue;
                }
                frame.phase = ETypePhase::finish_member;
                break;
            }
            case ETypePhase::finish_member:
            {
                if (!((frame.record.category == ECategory::structure) ? structure_member(frame) : bit_member(frame)))
                {
                    return false;
                }
                frame.next = m_document.next_sibling(frame.next);
                ++frame.ordinal;
                frame.phase = ETypePhase::member;
                break;
            }
        }
        if (complete)
        {
            frame.record.set_state(2u);
            m_schema.m_types[frame.slot] = frame.record;
            (void)m_type_frames.discard_back();
        }
    }
    m_context_type = old_type;
    m_context_member = old_member;
    return true;
}

bool CResolver::begin_members(STypeFrame& frame) noexcept
{
    TTypeRecord& record = frame.record;
    const bool bits = record.category == ECategory::bit_structure;
    if (!shape(record.source, (bits ?
        std::initializer_list<const char*>{ "storage", "members", "decorations" } :
        std::initializer_list<const char*>{ "members", "detail", "decorations" }), (bits ?
        std::initializer_list<const char*>{ "storage", "members" } : std::initializer_list<const char*>{ "members" })) ||
        !decorations(record.source) || (bits && !storage(record)))
    {
        return false;
    }
    const CTypesHandle members = property(record.source, "members");
    if (!names(members, true, record.source, !bits))
    {
        return false;
    }
    record.count = m_document.child_count(members);
    if (bits)
    {
        record.first = static_cast<std::uint32_t>(m_schema.m_fields.size());
        if (!grow(m_schema.m_fields, record.count, members) || !grow(m_schema.m_field_names, record.count, members))
        {
            return false;
        }
    }
    else
    {
        record.first = static_cast<std::uint32_t>(m_schema.m_members.size());
        record.alignment_log2 = 0u;
        record.set_flag(TTypeRecord::k_named_components, (record.count != 0u));
        if (!grow(m_schema.m_members, record.count, members) || !grow(m_schema.m_member_names, record.count, members) ||
            !grow(m_schema.m_member_order, record.count, members))
        {
            return false;
        }
    }
    frame.next = m_document.first_child(members);
    return true;
}

bool CResolver::begin_member(STypeFrame& frame) noexcept
{
    const CTypesHandle source = frame.next;
    const bool bits = frame.record.category == ECategory::bit_structure;
    m_stage = EStage::declarations;
    if (!shape(source, (bits ?
        std::initializer_list<const char*>{ "type", "mask", "interpretation", "default", "decorations" } :
        std::initializer_list<const char*>{ "type", "default", "offset", "decorations" }), (bits ?
        std::initializer_list<const char*>{ "type", "mask" } : std::initializer_list<const char*>{ "type" })) ||
        !decorations(source))
    {
        return false;
    }
    const CTypesHandle type = property(source, "type");
    if (bits)
    {
        if (m_document.value_kind(type) != EDocumentValueKind::string)
        {
            return fail(EReason::invalid_input, type);
        }
    }
    else
    {
        const CTypesHandle offset = property(source, "offset");
        if (frame.ordinal == 0u)
        {
            frame.explicit_offsets = offset.is_valid();
            frame.first_member = source;
        }
        else if (offset.is_valid() != frame.explicit_offsets)
        {
            return fail(EReason::invalid_layout, (offset ? offset : source), frame.first_member);
        }
    }
    m_stage = EStage::references;
    frame.reference = {};
    frame.reference.origin = frame.reference.current = type;
    frame.reference.depth = frame.depth + 1u;
    return true;
}

bool CResolver::finish_members(STypeFrame& frame) noexcept
{
    TTypeRecord& record = frame.record;
    if (record.category == ECategory::structure)
    {
        if (!order_names(m_schema.m_members, m_schema.m_member_names, record.first, record.count))
        {
            return fail(EReason::storage_limit);
        }
        return structure_layout(record, frame.explicit_offsets, frame.has_storage);
    }
    if (!order_names(m_schema.m_fields, m_schema.m_field_names, record.first, record.count))
    {
        return fail(EReason::storage_limit);
    }
    record.set_flag(TTypeRecord::k_gaps, (frame.used != resolver_util::max_unsigned(static_cast<unsigned>(record.size << 3u))));
    return true;
}

bool CResolver::structure_member(STypeFrame& frame) noexcept
{
    TTypeRecord& type_record = frame.record;
    const CTypesHandle member_source = frame.next;
    const CTypesHandle offset = property(member_source, "offset");
    const std::uint32_t member_slot = type_record.first + frame.ordinal;
    const unsigned depth = frame.depth;
    CResolvedTypes::SMemberRecord member;
    member.name = m_document.name_id(member_source);
    member.source = member_source;
    member.type = frame.reference.result;
    const TTypeRecord member_type = *m_schema.type_record(member.type);
    type_record.set_flag(TTypeRecord::k_references,
        (type_record.flag(TTypeRecord::k_references) || member_type.flag(TTypeRecord::k_references)));
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
        (type_record.flag(TTypeRecord::k_gaps) || member_type.flag(TTypeRecord::k_gaps)));
    type_record.alignment_log2 = std::max(type_record.alignment_log2, member_type.alignment_log2);
    frame.has_storage |= member.size != 0u;
    if (member_slot == type_record.first)
    {
        frame.component = member.type;
    }
    type_record.set_flag(TTypeRecord::k_named_components,
        (type_record.flag(TTypeRecord::k_named_components) && (member_type.category == ECategory::primitive) && (frame.component == member.type)));
    if (!make_default(member.type, property(member_source, "default"), member.default_description, (depth + 1u)))
    {
        return false;
    }
    m_schema.m_members[member_slot] = member;
    if (!map(member_source, m_schema.index(resolver_util::k_member, member_slot)))
    {
        return false;
    }
    return true;
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
    const CResolvedTypes::SMemberRecord* const members = m_schema.m_members.data() + type_record.first;
    if (!detail::data_sort(order, (order + type_record.count),
        [members](const std::uint32_t a, const std::uint32_t b) noexcept
        {
            return (members[a].offset < members[b].offset) ||
                ((members[a].offset == members[b].offset) && (a < b));
        })) return fail(EReason::storage_limit);

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
        const CResolvedTypes::SMemberRecord& member = members[ordinal];
        if (member.size == 0u)
        {
            continue;
        }
        while (active.size() != 0u)
        {
            const CResolvedTypes::SMemberRecord& prior = members[active[0u]];

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
    const CTypesHandle detail = property(type_record.source, "detail");
    CTypesHandle size_source;
    std::uint64_t declared_size{};
    if (detail)
    {
        if (!shape(detail, { "size", "alignment", "internal" }))
        {
            return false;
        }
        const CTypesHandle internal_source = property(detail, "internal");
        bool internal{};
        if (internal_source && !m_document.boolean_value(internal_source, internal))
        {
            return fail(EReason::invalid_input, internal_source);
        }
        type_record.set_flag(TTypeRecord::k_internal, internal);
        const CTypesHandle alignment_source = property(detail, "alignment");
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
        CResolvedTypes::SMemberRecord& member = m_schema.m_members[type_record.first + ordinal];
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
                const CResolvedTypes::SMemberRecord& prior = m_schema.m_members[type_record.first + related];
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

bool CResolver::scalar(const CTypesIndex type, const CTypesHandle& source, SScalar& result) noexcept
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

bool CResolver::make_default(const CTypesIndex type, const CTypesHandle& source, CTypesIndex& result, const unsigned depth) noexcept
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

bool CResolver::enter_default(SDefaultFrame& frame) noexcept
{
    const CTypesIndex type = frame.type;
    const CTypesHandle source = frame.source;
    const std::uint32_t slot = frame.slot;
    const unsigned depth = frame.depth;
    if (!detail::data_depth(depth) || (depth >= document_limits::k_max_recursion_depth))
    {
        return fail(EReason::storage_limit, source);
    }
    const TTypeRecord type_record = *m_schema.type_record(type);
    CResolvedTypes::SDefaultRecord default_info;
    default_info.type = type;
    default_info.source = source;
    if (type_record.category == ECategory::array)
    {
        TTypeRecord base = type_record;
        while (base.category == ECategory::array)
        {
            base = *m_schema.type_record(base.related);
        }
        if ((base.category != ECategory::primitive) && (base.category != ECategory::enumeration) &&
            (base.category != ECategory::text) && (base.category != ECategory::span))
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
        frame.element = type_record.related;
        frame.next = m_document.first_child(source);
        frame.child_slot = default_info.first;
    }
    else if ((type_record.category == ECategory::text) || (type_record.category == ECategory::span))
    {
        detail::SReferenceValue value;
        if (!detail::read_reference_value(detail::SDocumentQueryAccess::read(m_document),
            detail::STypesHandleAccess::occurrence(source), type_record.category,
            type_record.flag(TTypeRecord::k_nullable), value, true) || value.targeted)
        {
            return fail(EReason::invalid_default, source);
        }
        default_info.scalar.kind = EScalar::signed_integer;
        default_info.scalar.value.signed_value = value.absent ? -1 : 0;
    }
    else if (!scalar(type, source, default_info.scalar))
    {
        return false;
    }
    m_schema.m_defaults[slot] = default_info;
    return true;
}

bool CResolver::default_record(const CTypesIndex type, const CTypesHandle& source,
    const std::uint32_t slot, const unsigned depth) noexcept
{
    SDefaultFrame frame;
    frame.type = type;
    frame.source = source;
    frame.slot = slot;
    frame.depth = depth;
    if (!enter_default(frame)) return false;
    for (;;)
    {
        if (frame.next)
        {
            SDefaultFrame child;
            child.type = frame.element;
            child.source = frame.next;
            child.slot = frame.child_slot++;
            child.depth = frame.depth + 1u;
            frame.next = m_document.next_sibling(frame.next);
            const ECategory category = m_schema.type_record(child.type)->category;
            if (m_document.is_object_entry(child.source) && (category != ECategory::text) && (category != ECategory::span))
            {
                return fail(EReason::invalid_default, child.source);
            }
            if (!enter_default(child)) return false;
            if (child.next)
            {
                if (!m_default_frames.push_back(frame))
                {
                    return fail(EReason::allocation_failed, child.source);
                }
                frame = child;
            }
        }
        else
        {
            if (m_default_frames.is_empty()) return true;
            (void)m_default_frames.pop_back(frame);
        }
    }
}

bool CResolver::bit_member(STypeFrame& frame) noexcept
{
    TTypeRecord& type_record = frame.record;
    const CTypesHandle field_source = frame.next;
    const std::uint32_t field_slot = type_record.first + frame.ordinal;
    const unsigned depth = frame.depth;
    SField field;
    field.name = m_document.name_id(field_source);
    field.source = field_source;
    field.type = frame.reference.result;
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
    if (!field.mask || (field.mask & ~resolver_util::max_unsigned(static_cast<unsigned>(type_record.size << 3u))))
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
    if (frame.used & field.mask)
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
    frame.used |= field.mask;
    if ((logical.category == ECategory::enumeration) &&
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
    const CTypesHandle interpretation_source = property(field_source, "interpretation");
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
    const CTypesHandle def = property(field_source, "default");
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
        if (!resolver_util::fits(value, field.signed_value, static_cast<unsigned>(logical.size << 3u)) ||
            !resolver_util::fits(value, field.signed_value, field.width))
        {
            return fail(EReason::invalid_default, def);
        }
        if (!grow(m_schema.m_defaults, 1u, def))
        {
            return false;
        }
        CResolvedTypes::SDefaultRecord& default_info = m_schema.m_defaults.last();
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
    return true;
}

bool CResolvedTypes::resolve(const CBakedDocument& document, SDiagnostic& diagnostic) noexcept
{
    return resolve(CTypesDocumentQuery{ document }, diagnostic);
}

bool CResolvedTypes::resolve(const CLiveDocument& document, SDiagnostic& diagnostic) noexcept
{
    return resolve(CTypesDocumentQuery{ document }, diagnostic);
}

bool CResolvedTypes::resolve(SDiagnostic& diagnostic) noexcept
{
    return resolve(m_document, diagnostic);
}

bool CResolvedTypes::resolve(const CTypesDocumentQuery& document, SDiagnostic& diagnostic) noexcept
{   //  Copy first: the caller may pass this owner's current retained query.
    const CTypesDocumentQuery input = document;
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
bool detail::SDocumentQueryAccess::backs(const CResolvedTypes& schema, const CLiveDocument& document) noexcept
{
    return read(schema.m_document).backs(document);
}

}   // namespace schema

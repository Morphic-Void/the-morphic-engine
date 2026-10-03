
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_codec.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Construct values in resolved physical layouts without touching padding.

#include "schema/value_codec.hpp"
#include "schema/value_conversion.hpp"
#include "data_model/live_document.hpp"

#include <cmath>
#include <cstring>
#include <cstdint>

namespace schema
{

[[nodiscard]] static bool scalar_is_signed(const EPrimitive primitive) noexcept
{
    return (primitive == EPrimitive::i8) || (primitive == EPrimitive::i16) ||
        (primitive == EPrimitive::i32) || (primitive == EPrimitive::i64);
}

[[nodiscard]] static std::int64_t sign_extend_scalar(const std::uint64_t bits, const unsigned width) noexcept
{
    if (width == 64u)
    {
        return static_cast<std::int64_t>(bits);
    }
    const std::uint64_t sign = std::uint64_t{ 1u } << (width - 1u);
    return static_cast<std::int64_t>((bits ^ sign) - sign);
}

[[nodiscard]] static CNodeKey create_decoded_scalar(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EScalarDecodeReason& reason) noexcept
{
    SType layout;
    if (!schema.type(type, layout))
    {
        reason = EScalarDecodeReason::invalid_type;
        return {};
    }
    if (layout.category == ECategory::enumeration)
    {
        SScalar value;
        value.kind = scalar_is_signed(layout.primitive) ? EScalar::signed_integer : EScalar::unsigned_integer;
        if (value.kind == EScalar::signed_integer)
        {
            value.value.signed_value = sign_extend_scalar(bits, width);
        }
        else
        {
            value.value.unsigned_value = bits;
        }
        SLabel label;
        const CSchemaIndex found = schema.first_label_for_value(type, value);
        if (!found || !schema.label(found, label))
        {
            reason = EScalarDecodeReason::unrepresentable_value;
            return {};
        }
        return document.create_string(schema.name(label.name), name);
    }
    if (layout.category != ECategory::primitive)
    {
        reason = EScalarDecodeReason::invalid_type;
        return {};
    }
    if (scalar_is_signed(layout.primitive))
    {
        return document.create_signed_integer(sign_extend_scalar(bits, width), name);
    }
    if (layout.primitive == EPrimitive::b8)
    {
        return document.create_boolean((bits != 0u), name);
    }
    if ((layout.primitive == EPrimitive::f16) || (layout.primitive == EPrimitive::f32) ||
        (layout.primitive == EPrimitive::f64))
    {
        double value{};
        if (layout.primitive == EPrimitive::f16)
        {
            value = static_cast<double>(fp16data_t::fromBits(static_cast<std::uint16_t>(bits)));
        }
        else if (layout.primitive == EPrimitive::f32)
        {
            const std::uint32_t raw = static_cast<std::uint32_t>(bits);
            float converted{};
            std::memcpy(&converted, &raw, sizeof(converted));
            value = converted;
        }
        else
        {
            std::memcpy(&value, &bits, sizeof(value));
        }
        if (std::isnan(value))
        {
            return document.create_string(CStringView{ "nan" }, name);
        }
        if (std::isinf(value))
        {
            return document.create_string(CStringView{ (value < 0.0) ? "-inf" : "inf" }, name);
        }
        return document.create_floating_point(value, name);
    }
    return document.create_unsigned_integer(bits, name);
}

CNodeKey decode_document_scalar(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EScalarDecodeReason& reason) noexcept
{
    reason = EScalarDecodeReason::none;
    if ((width == 0u) || (width > 64u))
    {
        reason = EScalarDecodeReason::invalid_type;
        return {};
    }
    const CNodeKey value = create_decoded_scalar(document, schema, type, bits, width, name, reason);
    if (!value && (reason == EScalarDecodeReason::none))
    {
        reason = EScalarDecodeReason::allocation_failed;
    }
    return value;
}

namespace detail
{

enum class EWriteMode : std::uint8_t { instance, bulk, alternative };
constexpr unsigned k_max_value_depth = 256u;

bool is_scalar_shorthand(const CResolvedSchema& schema, const CSchemaIndex type,
    const SType& layout, const CDocumentRead& document, const SOccurrence value) noexcept
{
    if (layout.count != 1u)
    {
        return false;
    }
    const EDocumentValueKind kind = document.value_kind(value);
    if ((kind != EDocumentValueKind::boolean) && (kind != EDocumentValueKind::integer) &&
        (kind != EDocumentValueKind::floating_point) && (kind != EDocumentValueKind::string))
    {
        return false;
    }
    if (layout.category == ECategory::bit_structure)
    {
        return true;
    }
    SMember member;
    SType member_type;
    return (layout.category == ECategory::structure) &&
        schema.member(schema.member_at(type, 0u), member) && schema.type(member.type, member_type) &&
        ((member_type.category == ECategory::primitive) || (member_type.category == ECategory::enumeration));
}

[[nodiscard]] static bool equal_name(const CStringView a, const CStringView b) noexcept
{
    return a.length() == b.length() && (a.length() == 0u || std::memcmp(a.string(), b.string(), a.length()) == 0);
}

[[nodiscard]] static bool is_nan_bits(const EPrimitive primitive, const std::uint64_t bits) noexcept
{
    if (primitive == EPrimitive::f16)
    {
        return ((bits & 0x7c00u) == 0x7c00u) && ((bits & 0x03ffu) != 0u);
    }
    if (primitive == EPrimitive::f32)
    {
        return ((bits & 0x7f800000u) == 0x7f800000u) && ((bits & 0x007fffffu) != 0u);
    }
    if (primitive == EPrimitive::f64)
    {
        return ((bits & UINT64_C(0x7ff0000000000000)) == UINT64_C(0x7ff0000000000000)) &&
            ((bits & UINT64_C(0x000fffffffffffff)) != 0u);
    }
    return false;
}

[[nodiscard]] static std::uint64_t scalar_bits(const SScalar& scalar, const EPrimitive primitive) noexcept
{
    if (primitive == EPrimitive::f64)
    {
        std::uint64_t bits{};
        std::memcpy(&bits, &scalar.value.floating_value, sizeof(bits));
        return bits;
    }
    if (primitive == EPrimitive::f32)
    {
        const float narrowed = static_cast<float>(scalar.value.floating_value);
        std::uint32_t bits{};
        std::memcpy(&bits, &narrowed, sizeof(bits));
        return bits;
    }
    return scalar.kind == EScalar::signed_integer ?
        static_cast<std::uint64_t>(scalar.value.signed_value) : scalar.value.unsigned_value;
}

class CValueWriter
{
public:
    CValueWriter(const CResolvedSchema& schema, const CDocumentRead& document, SValueDiagnostic& diagnostic) noexcept
        : m_schema(schema), m_document(document), m_diagnostic(diagnostic) {}

    [[nodiscard]] bool write(const CSchemaIndex type, const CSchemaIndex description, const SOccurrence source,
        std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode,
        const unsigned depth, const bool singleton_element = false) noexcept;

    [[nodiscard]] bool fail(const EReason reason, const SOccurrence source, const CSchemaIndex type) noexcept
    {
        if (m_diagnostic.reason == EReason::none)
        {
            m_diagnostic = { reason, source, type };
        }
        return false;
    }

private:
    [[nodiscard]] bool validate_object(const CSchemaIndex type, const SOccurrence source, const ECategory category) noexcept;
    [[nodiscard]] bool validate_positional(const CSchemaIndex type, const SOccurrence source) noexcept;
    [[nodiscard]] bool scalar(const CSchemaIndex type, const SOccurrence source, SScalar& result) noexcept;
    [[nodiscard]] bool field_scalar(const SField& field, const SOccurrence source, SScalar& result) noexcept;
    [[nodiscard]] bool structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
        std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode,
        const unsigned depth, const bool singleton_element) noexcept;
    [[nodiscard]] bool array(const CSchemaIndex type, const CSchemaIndex description, const SType& layout, const SOccurrence source,
        std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode, const unsigned depth) noexcept;
    [[nodiscard]] bool bit_structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
        std::uint8_t* const destination, const EWriteMode mode, const bool singleton_element) noexcept;

    const CResolvedSchema& m_schema;
    const CDocumentRead& m_document;
    SValueDiagnostic& m_diagnostic;
};

bool CValueWriter::validate_object(const CSchemaIndex type, const SOccurrence source, const ECategory category) noexcept
{
    for (SOccurrence child = m_document.first_child(source); child.is_valid(); child = m_document.next_sibling(child))
    {
        if (!m_document.is_object_entry(child))
        {
            return fail(EReason::invalid_input, child, type);
        }
        const CStringView name = m_document.name(child);
        const CSchemaIndex selected = category == ECategory::bit_structure ?
            m_schema.find_field(type, name) : m_schema.find_member(type, name);
        if (!selected)
        {
            return fail(EReason::unknown_property, child, type);
        }
        for (SOccurrence previous = m_document.first_child(source); previous != child;
            previous = m_document.next_sibling(previous))
        {
            if (equal_name(name, m_document.name(previous)))
            {
                return fail(EReason::duplicate_declaration, child, type);
            }
        }
    }
    return true;
}

bool CValueWriter::validate_positional(const CSchemaIndex type, const SOccurrence source) noexcept
{
    for (SOccurrence child = m_document.first_child(source); child.is_valid(); child = m_document.next_sibling(child))
    {
        if (m_document.is_object_entry(child))
        {
            return fail(EReason::invalid_input, child, type);
        }
    }
    return true;
}

bool CValueWriter::scalar(const CSchemaIndex type, const SOccurrence source, SScalar& result) noexcept
{
    SType layout;
    if (!m_schema.type(type, layout))
    {
        return fail(EReason::unknown_type, source, type);
    }
    if (layout.category == ECategory::enumeration)
    {
        if (m_document.value_kind(source) != EDocumentValueKind::string)
        {
            return fail(EReason::invalid_input, source, type);
        }
        const CSchemaIndex label_index = m_schema.find_label(type, m_document.string_value(source));
        SLabel label;
        if (!label_index || !m_schema.label(label_index, label))
        {
            return fail(EReason::invalid_input, source, type);
        }
        result = label.value;
        return true;
    }
    if (layout.category != ECategory::primitive)
    {
        return fail(EReason::invalid_input, source, type);
    }
    EReason reason{ EReason::none };
    if (!convert_primitive(m_document, source, layout.primitive, static_cast<unsigned>(layout.size), result, reason))
    {
        return fail(reason == EReason::invalid_default ? EReason::invalid_input : reason, source, type);
    }
    return true;
}

bool CValueWriter::field_scalar(const SField& field, const SOccurrence source, SScalar& result) noexcept
{
    if ((field.interpretation == EInterpretation::ordinary) ||
        (m_document.value_kind(source) == EDocumentValueKind::integer))
    {
        if (!scalar(field.type, source, result))
        {
            return false;
        }
    }
    else
    {
        EReason reason{ EReason::none };
        if (!convert_normalised(m_document, source, field.interpretation, field.width, result, reason))
        {
            return fail(reason == EReason::invalid_default ? EReason::invalid_input : reason, source, field.type);
        }
    }
    SType logical;
    if (!m_schema.type(field.type, logical) ||
        !fits_scalar(result, field.signed_value, static_cast<unsigned>(logical.size * 8u)) ||
        !fits_scalar(result, field.signed_value, field.width))
    {
        return fail(EReason::invalid_range, source, field.type);
    }
    return true;
}

bool CValueWriter::structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
    std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode,
    const unsigned depth, const bool singleton_element) noexcept
{
    const EDocumentValueKind kind = source.is_valid() ? m_document.value_kind(source) : EDocumentValueKind::invalid;
    const bool shorthand = !singleton_element && is_scalar_shorthand(m_schema, type, layout, m_document, source);
    if (source.is_valid() && !singleton_element && !shorthand &&
        (kind != EDocumentValueKind::object) && (kind != EDocumentValueKind::array))
    {
        return fail(EReason::invalid_input, source, type);
    }
    if (singleton_element && !m_schema.find_member(type, m_document.name(source)))
    {
        return fail(EReason::unknown_property, source, type);
    }
    if (!singleton_element && (kind == EDocumentValueKind::object) &&
        !validate_object(type, source, ECategory::structure))
    {
        return false;
    }
    const std::uint32_t supplied = (singleton_element || shorthand) ? 1u : (source.is_valid() ? m_document.child_count(source) : 0u);
    if (!singleton_element && (kind == EDocumentValueKind::array) && (supplied > layout.count))
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((mode == EWriteMode::bulk) &&
        (!source.is_valid() || (supplied != layout.count)))
    {
        return fail(EReason::missing_property, source, type);
    }
    SOccurrence positional = (!singleton_element && (kind == EDocumentValueKind::array)) ?
        m_document.first_child(source) : SOccurrence{};
    for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
    {
        const CSchemaIndex member_index = m_schema.member_at(type, ordinal);
        SMember member;
        if (!m_schema.member(member_index, member) || (member.offset > destination_size) ||
            (member.size > destination_size - static_cast<std::size_t>(member.offset)))
        {
            return fail(EReason::invalid_layout, source, type);
        }
        const bool selected_singleton = singleton_element &&
            equal_name(m_document.name(source), m_schema.name(member.name));
        const SOccurrence child = (selected_singleton || shorthand) ? source :
            (!singleton_element && (kind == EDocumentValueKind::object)) ?
            m_document.object_child(source, m_schema.name(member.name)) :
            ((!singleton_element && (kind == EDocumentValueKind::array) && (ordinal < supplied)) ?
                positional : SOccurrence{});
        if (positional.is_valid() && !singleton_element && (kind == EDocumentValueKind::array))
        {
            positional = m_document.next_sibling(positional);
        }
        const bool child_singleton = !singleton_element && (kind == EDocumentValueKind::array) &&
            child.is_valid() && m_document.is_object_entry(child);
        if (child_singleton)
        {
            SType member_layout;
            if (!m_schema.type(member.type, member_layout) ||
                ((member_layout.category != ECategory::structure) &&
                    (member_layout.category != ECategory::bit_structure)))
            {
                return fail(EReason::invalid_input, child, member.type);
            }
        }
        if ((mode == EWriteMode::bulk) && !child.is_valid())
        {
            return fail(EReason::missing_property, source, member.type);
        }
        if ((mode == EWriteMode::alternative) && !child.is_valid())
        {
            continue;
        }
        std::uint8_t* const member_destination = destination ? destination + member.offset : nullptr;
        if (!write(member.type, member.default_description, child, member_destination,
            static_cast<std::size_t>(member.size), mode, (depth + 1u), child_singleton))
        {
            return false;
        }
    }
    return true;
}

bool CValueWriter::array(const CSchemaIndex type, const CSchemaIndex description, const SType& layout, const SOccurrence source,
    std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode, const unsigned depth) noexcept
{
    if (source.is_valid() && (m_document.value_kind(source) != EDocumentValueKind::array))
    {
        return fail(EReason::invalid_input, source, type);
    }
    SType element;
    if (!m_schema.type(layout.element_or_storage, element))
    {
        return fail(EReason::unknown_type, source, type);
    }
    if (source.is_valid())
    {
        for (SOccurrence child = m_document.first_child(source); child.is_valid(); child = m_document.next_sibling(child))
        {
            if (m_document.is_object_entry(child) &&
                ((element.category != ECategory::structure) && (element.category != ECategory::bit_structure)))
            {
                return fail(EReason::invalid_input, child, type);
            }
        }
    }
    const std::uint32_t supplied = source.is_valid() ? m_document.child_count(source) : 0u;
    if (supplied > layout.count)
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((mode == EWriteMode::bulk) && (supplied != layout.count))
    {
        return fail(EReason::missing_property, source, type);
    }
    //  Omitted zero-byte elements need no physical default work. Authored
    //  elements still pass through recursive shape and count validation.
    const std::uint32_t visited = (mode == EWriteMode::bulk || element.size != 0u) ?
        (mode == EWriteMode::alternative ? supplied : layout.count) : supplied;
    SOccurrence positional = source.is_valid() ? m_document.first_child(source) : SOccurrence{};
    for (std::uint32_t ordinal = 0u; ordinal < visited; ++ordinal)
    {
        const SOccurrence child = ordinal < supplied ? positional : SOccurrence{};
        if (positional.is_valid())
        {
            positional = m_document.next_sibling(positional);
        }
        if (!child.is_valid() && (mode == EWriteMode::alternative))
        {
            continue;
        }
        CSchemaIndex element_default;
        if ((mode != EWriteMode::bulk) &&
            !m_schema.default_element(type, description, ordinal, element_default))
        {
            return fail(EReason::invalid_default, source, type);
        }
        const std::uint64_t offset = layout.stride * static_cast<std::uint64_t>(ordinal);
        if ((offset > destination_size) || (element.size > destination_size - static_cast<std::size_t>(offset)))
        {
            return fail(EReason::invalid_layout, child, type);
        }
        std::uint8_t* const element_destination = destination ? destination + static_cast<std::size_t>(offset) : nullptr;
        if (!write(layout.element_or_storage, element_default, child, element_destination,
            static_cast<std::size_t>(element.size), mode, (depth + 1u),
            (child.is_valid() && m_document.is_object_entry(child))))
        {
            return false;
        }
    }
    return true;
}

bool CValueWriter::bit_structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
    std::uint8_t* const destination, const EWriteMode mode, const bool singleton_element) noexcept
{
    const EDocumentValueKind kind = source.is_valid() ? m_document.value_kind(source) : EDocumentValueKind::invalid;
    const bool shorthand = !singleton_element && is_scalar_shorthand(m_schema, type, layout, m_document, source);
    if (source.is_valid() && !singleton_element && !shorthand &&
        (kind != EDocumentValueKind::object) && (kind != EDocumentValueKind::array))
    {
        return fail(EReason::invalid_input, source, type);
    }
    if (singleton_element && !m_schema.find_field(type, m_document.name(source)))
    {
        return fail(EReason::unknown_property, source, type);
    }
    if (!singleton_element && (kind == EDocumentValueKind::object) &&
        !validate_object(type, source, ECategory::bit_structure))
    {
        return false;
    }
    if (!singleton_element && (kind == EDocumentValueKind::array) && !validate_positional(type, source))
    {
        return false;
    }
    const std::uint32_t supplied = (singleton_element || shorthand) ? 1u :
        (source.is_valid() ? m_document.child_count(source) : 0u);
    if (!singleton_element && (kind == EDocumentValueKind::array) && (supplied > layout.count))
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((mode == EWriteMode::bulk) && (!source.is_valid() || (supplied != layout.count)))
    {
        return fail(EReason::missing_property, source, type);
    }
    std::uint64_t word = mode == EWriteMode::alternative ?
        read_scalar_bits(destination, static_cast<std::size_t>(layout.size)) : 0u;
    SOccurrence positional = (!singleton_element && (kind == EDocumentValueKind::array)) ?
        m_document.first_child(source) : SOccurrence{};
    for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
    {
        SField field;
        if (!m_schema.field(m_schema.field_at(type, ordinal), field))
        {
            return fail(EReason::invalid_layout, source, type);
        }
        const bool selected_singleton = singleton_element &&
            equal_name(m_document.name(source), m_schema.name(field.name));
        const SOccurrence child = (selected_singleton || shorthand) ? source :
            (!singleton_element && (kind == EDocumentValueKind::object)) ?
            m_document.object_child(source, m_schema.name(field.name)) :
            ((!singleton_element && (kind == EDocumentValueKind::array) && (ordinal < supplied)) ?
                positional : SOccurrence{});
        if (positional.is_valid() && !singleton_element && (kind == EDocumentValueKind::array))
        {
            positional = m_document.next_sibling(positional);
        }
        if ((mode == EWriteMode::bulk) && !child.is_valid())
        {
            return fail(EReason::missing_property, source, field.type);
        }
        if ((mode == EWriteMode::alternative) && !child.is_valid())
        {
            continue;
        }
        SScalar value;
        if (child.is_valid())
        {
            if (!field_scalar(field, child, value))
            {
                return false;
            }
        }
        else
        {
            SDefault default_value;
            if (!m_schema.default_value(field.type, field.default_description, default_value))
            {
                return fail(EReason::invalid_default, source, field.type);
            }
            value = default_value.scalar;
        }
        const std::uint64_t bits = scalar_bits(value, field.primitive);
        word = (word & ~field.mask) | ((bits << field.shift) & field.mask);
    }
    write_scalar_bits(destination, static_cast<std::size_t>(layout.size), word);
    return true;
}

bool CValueWriter::write(const CSchemaIndex type, const CSchemaIndex description, const SOccurrence source,
    std::uint8_t* const destination, const std::size_t destination_size, const EWriteMode mode,
    const unsigned depth, const bool singleton_element) noexcept
{
    if (depth >= k_max_value_depth)
    {
        return fail(EReason::storage_limit, source, type);
    }
    SType layout;
    if (!m_schema.type(type, layout))
    {
        return fail(EReason::unknown_type, source, type);
    }
    if ((layout.size > destination_size) || ((layout.size != 0u) && !destination))
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((mode == EWriteMode::alternative) && !source.is_valid())
    {
        return true;
    }
    switch (layout.category)
    {
        case ECategory::primitive:
        case ECategory::enumeration:
        {
            if ((mode == EWriteMode::bulk) && !source.is_valid())
            {
                return fail(EReason::missing_property, source, type);
            }
            SScalar value;
            if (source.is_valid())
            {
                if (!scalar(type, source, value))
                {
                    return false;
                }
            }
            else
            {
                SDefault default_value;
                if (!m_schema.default_value(type, description, default_value))
                {
                    return fail(EReason::invalid_default, source, type);
                }
                value = default_value.scalar;
            }
            write_scalar_bits(destination, static_cast<std::size_t>(layout.size), scalar_bits(value, layout.primitive));
            return true;
        }
        case ECategory::structure:
            return structure(type, layout, source, destination, destination_size, mode, depth, singleton_element);
        case ECategory::array:
            return array(type, description, layout, source, destination, destination_size, mode, depth);
        case ECategory::bit_structure:
            return bit_structure(type, layout, source, destination, mode, singleton_element);
        default:
            return fail(EReason::unknown_type, source, type);
    }
}

[[nodiscard]] static bool preflight(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const bytes,
    const std::size_t byte_count, SValueDiagnostic& diagnostic, SType& layout) noexcept
{
    diagnostic = {};
    if (!schema.is_ready() || !document.is_ready() || (declaration.is_valid() && !document.contains(declaration)) ||
        !schema.type(type, layout))
    {
        diagnostic = { EReason::invalid_input, declaration, type };
        return false;
    }
    if ((byte_count < layout.size) || ((layout.size != 0u) &&
        (!bytes || ((reinterpret_cast<std::uintptr_t>(bytes) & (layout.alignment - 1u)) != 0u))))
    {
        diagnostic = { EReason::invalid_range, declaration, type };
        return false;
    }
    return true;
}

[[nodiscard]] static bool compare_encoded_value(const CResolvedSchema& schema, const CSchemaIndex type,
    const std::uint8_t* const expected, const std::uint8_t* const actual,
    const std::size_t available, const unsigned depth) noexcept
{
    SType layout;
    if ((depth >= k_max_value_depth) || !schema.type(type, layout) || (layout.size > available))
    {
        return false;
    }
    if (layout.size == 0u)
    {
        return true;
    }
    if (!expected || !actual)
    {
        return false;
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        const std::uint64_t left = read_scalar_bits(expected, static_cast<std::size_t>(layout.size));
        const std::uint64_t right = read_scalar_bits(actual, static_cast<std::size_t>(layout.size));
        return (left == right) || (is_nan_bits(layout.primitive, left) && is_nan_bits(layout.primitive, right));
    }
    if (layout.category == ECategory::bit_structure)
    {
        const std::uint64_t differing =
            read_scalar_bits(expected, static_cast<std::size_t>(layout.size)) ^
            read_scalar_bits(actual, static_cast<std::size_t>(layout.size));
        for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
        {
            SField field;
            if (!schema.field(schema.field_at(type, ordinal), field) || ((differing & field.mask) != 0u))
            {
                return false;
            }
        }
        return true;
    }
    if (layout.category == ECategory::structure)
    {
        for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, ordinal), member) ||
                (member.offset > available) || (member.size > (available - member.offset)) ||
                !compare_encoded_value(schema, member.type, (expected + member.offset), (actual + member.offset),
                    static_cast<std::size_t>(member.size), (depth + 1u)))
            {
                return false;
            }
        }
        return true;
    }
    if (layout.category == ECategory::array)
    {
        SType element;
        if (!schema.type(layout.element_or_storage, element))
        {
            return false;
        }
        if (element.size == 0u)
        {
            return true;
        }
        for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
        {
            const std::uint64_t offset = layout.stride * static_cast<std::uint64_t>(ordinal);
            if ((offset > available) || (element.size > (available - offset)) ||
                !compare_encoded_value(schema, layout.element_or_storage, (expected + offset), (actual + offset),
                    static_cast<std::size_t>(element.size), (depth + 1u)))
            {
                return false;
            }
        }
        return true;
    }
    return false;
}

bool construct_value(const CResolvedSchema& schema, const CDocumentRead& document, const CSchemaIndex type,
    const SOccurrence declaration, std::uint8_t* const destination, const std::size_t destination_size,
    const EConstructionMode mode, SValueDiagnostic& diagnostic, const bool singleton_element) noexcept
{
    SType layout;
    if (!preflight(schema, document, type, declaration, destination, destination_size, diagnostic, layout))
    {
        return false;
    }
    if (singleton_element && (layout.category != ECategory::structure) && (layout.category != ECategory::bit_structure))
    {
        diagnostic = { EReason::invalid_input, declaration, type };
        return false;
    }
    CValueWriter writer{ schema, document, diagnostic };
    return writer.write(type, {}, declaration, destination, static_cast<std::size_t>(layout.size),
        ((mode == EConstructionMode::complete_bulk) ? EWriteMode::bulk : EWriteMode::instance), 0u, singleton_element);
}

bool construct_alternative(const CResolvedSchema& schema, const CDocumentRead& document, const CSchemaIndex type,
    const SOccurrence declaration, const std::uint8_t* const base, const std::size_t base_size,
    std::uint8_t* const destination, const std::size_t destination_size, SValueDiagnostic& diagnostic) noexcept
{
    SType layout;
    if (!preflight(schema, document, type, declaration, base, base_size, diagnostic, layout) ||
        !preflight(schema, document, type, declaration, destination, destination_size, diagnostic, layout))
    {
        return false;
    }
    const std::uintptr_t source_address = reinterpret_cast<std::uintptr_t>(base);
    const std::uintptr_t destination_address = reinterpret_cast<std::uintptr_t>(destination);
    if ((base_size != 0u) && (destination_size != 0u) &&
        (source_address <= destination_address ?
            ((destination_address - source_address) < base_size) :
            ((source_address - destination_address) < destination_size)))
    {
        diagnostic = { EReason::invalid_range, declaration, type };
        return false;
    }
    if (layout.size != 0u)
    {
        std::memcpy(destination, base, static_cast<std::size_t>(layout.size));
    }
    CValueWriter writer{ schema, document, diagnostic };
    return writer.write(type, {}, declaration, destination, static_cast<std::size_t>(layout.size), EWriteMode::alternative, 0u);
}

bool compare_encoded(const CResolvedSchema& schema, const CSchemaIndex type,
    const std::uint8_t* const expected, const std::size_t expected_size,
    const std::uint8_t* const actual, const std::size_t actual_size) noexcept
{
    SType layout;
    return schema.type(type, layout) && (layout.size <= expected_size) && (layout.size <= actual_size) &&
        compare_encoded_value(schema, type, expected, actual, static_cast<std::size_t>(layout.size), 0u);
}

}   // namespace detail

}   // namespace schema

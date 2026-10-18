
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    value_codec.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    30 Sep 26
//
//  Decode scalars, construct schema values and compare addressable encodings.

#include "schema/value_codec.hpp"
#include "schema/value_conversion.hpp"
#include "schema/integer_notation.hpp"
#include "data_model/live_document.hpp"

#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdint>

namespace schema
{

//==============================================================================
//  Scalar decoding
//==============================================================================

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
        const std::int64_t value = sign_extend_scalar(bits, width);
        return document.create_signed_integer(value, signed_integer_metadata(value, integer_notation(layout.primitive)), name);
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
    return document.create_unsigned_integer(bits, unsigned_integer_metadata(bits, integer_notation(layout.primitive)), name);
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

//==============================================================================
//  Internal construction policy and scalar helpers
//==============================================================================

enum class EWriteMode : std::uint8_t { instance, bulk_data, alternative };
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

//==============================================================================
//  Value construction
//==============================================================================

//  One traversal serves writing and checking. Each recursive view retains its
//  own bounded location; checking never obtains a writable payload pointer.
struct SValueStorage
{
    std::uint8_t* writable{};
    const std::uint8_t* bytes{};
    const std::uint8_t* base{};

    [[nodiscard]] SValueStorage at(const std::size_t offset) const noexcept
    {
        return { writable ? writable + offset : nullptr, bytes ? bytes + offset : nullptr,
            base ? base + offset : nullptr };
    }
};

class CValueWriter
{
public:
    CValueWriter(const CResolvedSchema& schema, const CDocumentRead& document,
        const EWriteMode mode, SValueDiagnostic& diagnostic, const bool checking = false) noexcept :
        m_schema(schema), m_document(document), m_mode(mode), m_diagnostic(diagnostic), m_checking(checking) {}

    [[nodiscard]] bool matches() const noexcept { return m_matches; }

    [[nodiscard]] bool write(const CSchemaIndex type, const CSchemaIndex description, const SOccurrence source,
        const SValueStorage storage, const std::size_t destination_size,
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
    struct SCompoundInput
    {
        EDocumentValueKind kind{ EDocumentValueKind::invalid };
        std::uint32_t supplied{};
        bool shorthand{};
        CSchemaIndex singleton;
        CObjectSelection named;
    };

    [[nodiscard]] bool prepare_compound(const CSchemaIndex type, const SType& layout,
        const SOccurrence source, const bool singleton_element, SCompoundInput& input) noexcept;
    [[nodiscard]] bool validate_positional(const CSchemaIndex type, const SOccurrence source) noexcept;
    [[nodiscard]] bool scalar(const CSchemaIndex type, const SOccurrence source, SScalar& result) noexcept;
    [[nodiscard]] bool field_scalar(const SField& field, const SOccurrence source, SScalar& result) noexcept;
    [[nodiscard]] bool structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
        const SValueStorage storage, const std::size_t destination_size,
        const unsigned depth, const bool singleton_element) noexcept;
    [[nodiscard]] bool array(const CSchemaIndex type, const CSchemaIndex description, const SType& layout, const SOccurrence source,
        const SValueStorage storage, const std::size_t destination_size, const unsigned depth) noexcept;
    [[nodiscard]] bool bit_structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
        const SValueStorage storage, const bool singleton_element) noexcept;

    const CResolvedSchema& m_schema;
    const CDocumentRead& m_document;
    const EWriteMode m_mode;
    SValueDiagnostic& m_diagnostic;
    bool m_checking{};
    bool m_matches{ true };
};

CSchemaIndex CObjectSelection::component(const SOccurrence source) const noexcept
{
    return m_category == ECategory::bit_structure ? m_schema->find_field(m_type, m_document->name(source)) :
        m_schema->find_member(m_type, m_document->name(source));
}

bool CObjectSelection::prepare(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const ECategory category, const SOccurrence source, SValueDiagnostic& diagnostic) noexcept
{
    m_schema = &schema;
    m_document = &document;
    m_type = type;
    m_category = category;
    m_named.clear();
    m_next = 0u;
    m_selected = {};
    m_cursor = {};
    const auto fail = [&diagnostic](const EReason reason, const SOccurrence at, const CSchemaIndex selected)
    {
        if (diagnostic.reason == EReason::none) diagnostic = { reason, at, selected };
        return false;
    };
    CSchemaIndex previous;
    bool ordered = true;
    for (SOccurrence child = m_document->first_child(source); child.is_valid(); child = m_document->next_sibling(child))
    {
        if (!m_document->is_object_entry(child))
        {
            return fail(EReason::invalid_input, child, type);
        }
        const CSchemaIndex selected = component(child);
        if (!selected)
        {
            return fail(EReason::unknown_property, child, type);
        }
        ordered &= selected.query_value() > previous.query_value();
        previous = selected;
    }

    //  Both document representations guarantee unique object names. Ordered
    //  selections need only a forward cursor; unordered input gets one sorted
    //  index so misses and reverse order cannot cause repeated sibling scans.
    if (!ordered)
    {
        if (!m_named.reserve(m_document->child_count(source)))
        {
            return fail(EReason::allocation_failed, source, type);
        }
        for (SOccurrence child = m_document->first_child(source); child.is_valid(); child = m_document->next_sibling(child))
        {
            if (!m_named.push_back({ component(child), child }))
            {
                return fail(EReason::allocation_failed, source, type);
            }
        }
        std::sort(m_named.data(), (m_named.data() + m_named.size()),
            [](const SNamedInput& a, const SNamedInput& b)
            {
                return a.selected.query_value() < b.selected.query_value();
            });
        m_cursor = m_named[0u].source;
        m_selected = m_named[0u].selected;
    }
    else
    {
        m_cursor = m_document->first_child(source);
        if (m_cursor.is_valid()) m_selected = component(m_cursor);
    }
    return true;
}

SOccurrence CObjectSelection::child(const CSchemaIndex selected) noexcept
{
    if (!selected || (m_selected != selected))
    {
        return {};
    }
    const SOccurrence result = m_cursor;
    if (m_named.size())
    {
        ++m_next;
        m_cursor = m_next < m_named.size() ? m_named[m_next].source : SOccurrence{};
        m_selected = m_next < m_named.size() ? m_named[m_next].selected : CSchemaIndex{};
    }
    else
    {
        m_cursor = m_document->next_sibling(m_cursor);
        m_selected = m_cursor.is_valid() ? component(m_cursor) : CSchemaIndex{};
    }
    return result;
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

bool CValueWriter::prepare_compound(const CSchemaIndex type, const SType& layout,
    const SOccurrence source, const bool singleton_element, SCompoundInput& input) noexcept
{
    input.kind = source.is_valid() ? m_document.value_kind(source) : EDocumentValueKind::invalid;
    input.shorthand = !singleton_element && is_scalar_shorthand(m_schema, type, layout, m_document, source);
    if (source.is_valid() && !singleton_element && !input.shorthand &&
        (input.kind != EDocumentValueKind::object) && (input.kind != EDocumentValueKind::array))
    {
        return fail(EReason::invalid_input, source, type);
    }
    const bool bit_structure = layout.category == ECategory::bit_structure;
    if (singleton_element) input.singleton = bit_structure ?
        m_schema.find_field(type, m_document.name(source)) : m_schema.find_member(type, m_document.name(source));
    if (singleton_element && !input.singleton)
    {
        return fail(EReason::unknown_property, source, type);
    }
    if (!singleton_element && (input.kind == EDocumentValueKind::object) &&
        !input.named.prepare(m_schema, m_document, type, layout.category, source, m_diagnostic))
    {
        return false;
    }

    //  Structure positions may name compound selections; bit-field positions cannot.
    //  Preserve this check before count/completeness diagnostics.
    if (bit_structure && !singleton_element && (input.kind == EDocumentValueKind::array) && !validate_positional(type, source))
    {
        return false;
    }
    input.supplied = (singleton_element || input.shorthand) ? 1u : (source.is_valid() ? m_document.child_count(source) : 0u);
    if (!singleton_element && (input.kind == EDocumentValueKind::array) && (input.supplied > layout.count))
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((m_mode == EWriteMode::bulk_data) && (!source.is_valid() || (input.supplied != layout.count)))
    {
        return fail(EReason::missing_property, source, type);
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
    const SValueStorage storage, const std::size_t destination_size,
    const unsigned depth, const bool singleton_element) noexcept
{
    SCompoundInput input;
    if (!prepare_compound(type, layout, source, singleton_element, input))
    {
        return false;
    }
    SOccurrence positional = (!singleton_element && (input.kind == EDocumentValueKind::array)) ?
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
        const bool selected_singleton = singleton_element && (input.singleton == member_index);
        const SOccurrence child = (selected_singleton || input.shorthand) ? source :
            (!singleton_element && (input.kind == EDocumentValueKind::object)) ?
            input.named.child(member_index) :
            ((!singleton_element && (input.kind == EDocumentValueKind::array) && (ordinal < input.supplied)) ?
                positional : SOccurrence{});
        if (positional.is_valid() && !singleton_element && (input.kind == EDocumentValueKind::array))
        {
            positional = m_document.next_sibling(positional);
        }
        const bool child_singleton = !singleton_element && (input.kind == EDocumentValueKind::array) &&
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
        if ((m_mode == EWriteMode::bulk_data) && !child.is_valid())
        {
            return fail(EReason::missing_property, source, member.type);
        }
        if ((m_mode == EWriteMode::alternative) && !child.is_valid() && !m_checking)
        {
            continue;
        }
        if (!write(member.type, member.default_description, child, storage.at(static_cast<std::size_t>(member.offset)),
            static_cast<std::size_t>(member.size), (depth + 1u), child_singleton))
        {
            return false;
        }
    }
    return true;
}

bool CValueWriter::array(const CSchemaIndex type, const CSchemaIndex description, const SType& layout, const SOccurrence source,
    const SValueStorage storage, const std::size_t destination_size, const unsigned depth) noexcept
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
    if ((m_mode == EWriteMode::bulk_data) && (supplied != layout.count))
    {
        return fail(EReason::missing_property, source, type);
    }

    //  Omitted zero-byte elements need no physical default work. Authored
    //  elements still pass through recursive shape and count validation.
    const std::uint32_t visited = (m_mode == EWriteMode::bulk_data || element.size != 0u) ?
        (((m_mode == EWriteMode::alternative) && !m_checking) ? supplied : layout.count) : supplied;
    SOccurrence positional = source.is_valid() ? m_document.first_child(source) : SOccurrence{};
    for (std::uint32_t ordinal = 0u; ordinal < visited; ++ordinal)
    {
        const SOccurrence child = ordinal < supplied ? positional : SOccurrence{};
        if (positional.is_valid())
        {
            positional = m_document.next_sibling(positional);
        }
        if (!child.is_valid() && (m_mode == EWriteMode::alternative) && !m_checking)
        {
            continue;
        }
        CSchemaIndex element_default;
        if ((m_mode != EWriteMode::bulk_data) &&
            !m_schema.default_element(type, description, ordinal, element_default))
        {
            return fail(EReason::invalid_default, source, type);
        }
        const std::uint64_t offset = layout.stride * static_cast<std::uint64_t>(ordinal);
        if ((offset > destination_size) || (element.size > destination_size - static_cast<std::size_t>(offset)))
        {
            return fail(EReason::invalid_layout, child, type);
        }
        if (!write(layout.element_or_storage, element_default, child, storage.at(static_cast<std::size_t>(offset)),
            static_cast<std::size_t>(element.size), (depth + 1u),
            (child.is_valid() && m_document.is_object_entry(child))))
        {
            return false;
        }
    }
    return true;
}

bool CValueWriter::bit_structure(const CSchemaIndex type, const SType& layout, const SOccurrence source,
    const SValueStorage storage, const bool singleton_element) noexcept
{
    SCompoundInput input;
    if (!prepare_compound(type, layout, source, singleton_element, input))
    {
        return false;
    }
    std::uint64_t word = m_mode == EWriteMode::alternative ?
        read_scalar_bits((m_checking ? storage.base : storage.bytes), static_cast<std::size_t>(layout.size)) : 0u;
    std::uint64_t used_mask{};
    SOccurrence positional = (!singleton_element && (input.kind == EDocumentValueKind::array)) ?
        m_document.first_child(source) : SOccurrence{};
    for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
    {
        SField field;
        const CSchemaIndex field_index = m_schema.field_at(type, ordinal);
        if (!m_schema.field(field_index, field))
        {
            return fail(EReason::invalid_layout, source, type);
        }
        used_mask |= field.mask;
        const bool selected_singleton = singleton_element && (input.singleton == field_index);
        const SOccurrence child = (selected_singleton || input.shorthand) ? source :
            (!singleton_element && (input.kind == EDocumentValueKind::object)) ?
            input.named.child(field_index) :
            ((!singleton_element && (input.kind == EDocumentValueKind::array) && (ordinal < input.supplied)) ?
                positional : SOccurrence{});
        if (positional.is_valid() && !singleton_element && (input.kind == EDocumentValueKind::array))
        {
            positional = m_document.next_sibling(positional);
        }
        if ((m_mode == EWriteMode::bulk_data) && !child.is_valid())
        {
            return fail(EReason::missing_property, source, field.type);
        }
        if ((m_mode == EWriteMode::alternative) && !child.is_valid())
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
    if (m_checking)
    {
        m_matches &= ((word ^ read_scalar_bits(storage.bytes, static_cast<std::size_t>(layout.size))) & used_mask) == 0u;
    }
    else
    {
        write_scalar_bits(storage.writable, static_cast<std::size_t>(layout.size), word);
    }
    return true;
}

bool CValueWriter::write(const CSchemaIndex type, const CSchemaIndex description, const SOccurrence source,
    const SValueStorage storage, const std::size_t destination_size,
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
    if ((layout.size > destination_size) || ((layout.size != 0u) && !storage.bytes))
    {
        return fail(EReason::invalid_range, source, type);
    }
    if ((m_mode == EWriteMode::alternative) && !source.is_valid())
    {
        if (m_checking)
        {
            m_matches &= compare_encoded(m_schema, type, storage.base, destination_size, storage.bytes, destination_size);
        }
        return true;
    }
    switch (layout.category)
    {
        case ECategory::primitive:
        case ECategory::enumeration:
        {
            if ((m_mode == EWriteMode::bulk_data) && !source.is_valid())
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
            const std::size_t size = static_cast<std::size_t>(layout.size);
            const std::uint64_t bits = scalar_bits(value, layout.primitive);
            if (m_checking)
            {
                const std::uint64_t expected = bits & ((size == 8u) ? UINT64_MAX : ((UINT64_C(1) << (size * 8u)) - 1u));
                const std::uint64_t actual = read_scalar_bits(storage.bytes, size);
                m_matches &= (expected == actual) || (is_nan_bits(layout.primitive, expected) && is_nan_bits(layout.primitive, actual));
            }
            else
            {
                write_scalar_bits(storage.writable, size, bits);
            }
            return true;
        }
        case ECategory::structure:
            return structure(type, layout, source, storage, destination_size, depth, singleton_element);
        case ECategory::array:
            return array(type, description, layout, source, storage, destination_size, depth);
        case ECategory::bit_structure:
            return bit_structure(type, layout, source, storage, singleton_element);
        default:
            return fail(EReason::unknown_type, source, type);
    }
}

//==============================================================================
//  Construction preflight
//==============================================================================

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

//==============================================================================
//  Encoded-value comparison
//==============================================================================

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

//==============================================================================
//  Construction and comparison entry points
//==============================================================================

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
    CValueWriter writer{ schema, document,
        ((mode == EConstructionMode::complete_bulk_data) ? EWriteMode::bulk_data : EWriteMode::instance), diagnostic };
    return writer.write(type, {}, declaration, { destination, destination, nullptr }, static_cast<std::size_t>(layout.size), 0u, singleton_element);
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
    CValueWriter writer{ schema, document, EWriteMode::alternative, diagnostic };
    return writer.write(type, {}, declaration, { destination, destination, nullptr }, static_cast<std::size_t>(layout.size), 0u);
}

bool check_value(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const bytes,
    const std::size_t size, const EConstructionMode mode, bool& matches, SValueDiagnostic& diagnostic,
    const bool singleton_element) noexcept
{
    matches = false;
    SType layout;
    if (!preflight(schema, document, type, declaration, bytes, size, diagnostic, layout))
    {
        return false;
    }
    if (singleton_element && (layout.category != ECategory::structure) && (layout.category != ECategory::bit_structure))
    {
        diagnostic = { EReason::invalid_input, declaration, type };
        return false;
    }
    CValueWriter writer{ schema, document,
        ((mode == EConstructionMode::complete_bulk_data) ? EWriteMode::bulk_data : EWriteMode::instance), diagnostic, true };
    if (!writer.write(type, {}, declaration, { nullptr, bytes, nullptr }, static_cast<std::size_t>(layout.size), 0u, singleton_element))
    {
        return false;
    }
    matches = writer.matches();
    return true;
}

bool check_alternative(const CResolvedSchema& schema, const CDocumentRead& document,
    const CSchemaIndex type, const SOccurrence declaration, const std::uint8_t* const base,
    const std::size_t base_size, const std::uint8_t* const bytes, const std::size_t size,
    bool& matches, SValueDiagnostic& diagnostic) noexcept
{
    matches = false;
    SType layout;
    if (!preflight(schema, document, type, declaration, base, base_size, diagnostic, layout) ||
        !preflight(schema, document, type, declaration, bytes, size, diagnostic, layout))
    {
        return false;
    }
    CValueWriter writer{ schema, document, EWriteMode::alternative, diagnostic, true };
    if (!writer.write(type, {}, declaration, { nullptr, bytes, base }, static_cast<std::size_t>(layout.size), 0u))
    {
        return false;
    }
    matches = writer.matches();
    return true;
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

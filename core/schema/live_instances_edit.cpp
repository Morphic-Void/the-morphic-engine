
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    live_instances_edit.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    01 Oct 26
//
//  Canonical capture and selected binary propagation for live instances.

#include "schema/live_instances.hpp"
#include "schema/value_codec.hpp"
#include "types/fp16data_t.hpp"
#include "debug/macros.hpp"
#include "memory/memory_policies.hpp"

#include <cmath>
#include <cstring>
#include <limits>

namespace schema
{

//  Scalar decoding and selection tree helpers.

constexpr unsigned k_max_depth = 256u;

[[nodiscard]] static bool stabilise_name(const CStringView source, CByteBuffer& storage, CStringView& stable) noexcept
{
    if (source.empty())
    {
        return false;
    }
    if (source.length() == 0u)
    {
        stable = CStringView{ "" };
        return true;
    }
    if (!storage.allocate(source.length(), 1u) || !storage.set_size(source.length()))
    {
        return false;
    }
    std::memcpy(storage.data(), source.string(), source.length());
    stable = CStringView{ storage.data(), source.length() };
    return true;
}

[[nodiscard]] static std::uint64_t read_bits(const std::uint8_t* const bytes, const std::size_t size) noexcept
{
    std::uint64_t value{};
    for (std::size_t i = 0u; i < size; ++i)
    {
        value |= static_cast<std::uint64_t>(bytes[i]) << (8u * i);
    }
    return value;
}

static void write_bits(std::uint8_t* const bytes, const std::size_t size, const std::uint64_t value) noexcept
{
    for (std::size_t i = 0u; i < size; ++i)
    {
        bytes[i] = static_cast<std::uint8_t>(value >> (8u * i));
    }
}

[[nodiscard]] static bool signed_primitive(const EPrimitive value) noexcept
{
    return (value == EPrimitive::i8) || (value == EPrimitive::i16) ||
        (value == EPrimitive::i32) || (value == EPrimitive::i64);
}

[[nodiscard]] static std::int64_t sign_extended(const std::uint64_t value, const unsigned width) noexcept
{
    if (width == 64u)
    {
        return static_cast<std::int64_t>(value);
    }
    const std::uint64_t sign = std::uint64_t{ 1u } << (width - 1u);
    return static_cast<std::int64_t>((value ^ sign) - sign);
}

[[nodiscard]] static detail::SOccurrence selected_child(const CResolvedSchema& schema,
    const detail::CDocumentRead& document, const CSchemaIndex type, const SType& layout,
    const detail::SOccurrence selection, const std::uint32_t ordinal) noexcept
{
    if (!selection.is_valid())
    {
        return {};
    }
    if (document.value_kind(selection) != EDocumentValueKind::object)
    {
        return {};
    }
    if (layout.category == ECategory::structure)
    {
        SMember member;
        return schema.member(schema.member_at(type, ordinal), member) ?
            document.object_child(selection, schema.name(member.name)) : detail::SOccurrence{};
    }
    if (layout.category == ECategory::bit_structure)
    {
        SField field;
        return schema.field(schema.field_at(type, ordinal), field) ?
            document.object_child(selection, schema.name(field.name)) : detail::SOccurrence{};
    }
    return {};
}

[[nodiscard]] static CNodeKey decode_scalar(CLiveDocument& target, const CResolvedSchema& schema,
    const CSchemaIndex type, const std::uint64_t bits, const unsigned width,
    const CStringView& name, EInstanceLoadReason& error) noexcept
{
    SType layout;
    if (!schema.type(type, layout))
    {
        error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if (layout.category == ECategory::enumeration)
    {
        SScalar value;
        value.kind = signed_primitive(layout.primitive) ? EScalar::signed_integer : EScalar::unsigned_integer;
        if (value.kind == EScalar::signed_integer)
        {
            value.value.signed_value = sign_extended(bits, width);
        }
        else
        {
            value.value.unsigned_value = bits;
        }
        SLabel label;
        const CSchemaIndex label_index = schema.first_label_for_value(type, value);
        if (!label_index || !schema.label(label_index, label))
        {
            error = EInstanceLoadReason::invalid_declaration;
            return {};
        }
        return target.create_string(schema.name(label.name), name);
    }
    if (layout.category != ECategory::primitive)
    {
        error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if (signed_primitive(layout.primitive))
    {
        return target.create_signed_integer(sign_extended(bits, width), name);
    }
    if (layout.primitive == EPrimitive::b8)
    {
        return target.create_boolean((bits != 0u), name);
    }
    if ((layout.primitive == EPrimitive::f16) || (layout.primitive == EPrimitive::f32) || (layout.primitive == EPrimitive::f64))
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
            return target.create_string(CStringView{ "nan" }, name);
        }
        if (std::isinf(value))
        {
            return target.create_string(CStringView{ value < 0.0 ? "-inf" : "inf" }, name);
        }
        return target.create_floating_point(value, name);
    }
    return target.create_unsigned_integer(bits, name);
}

[[nodiscard]] static CNodeKey decode_selection(CLiveDocument& target, const CResolvedSchema& schema,
    const CSchemaIndex type, const detail::CDocumentRead& document, const detail::SOccurrence selection,
    const bool full, const std::uint8_t* const bytes, const CStringView& name,
    EInstanceLoadReason& error, const unsigned depth = 0u) noexcept
{
    if (depth >= k_max_depth)
    {
        error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if (!full && !selection.is_valid())
    {
        return {};
    }
    SType layout;
    if (!schema.type(type, layout) || (layout.size && !bytes))
    {
        error = EInstanceLoadReason::invalid_declaration;
        return {};
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        const CNodeKey scalar = decode_scalar(target, schema, type,
            read_bits(bytes, static_cast<std::size_t>(layout.size)),
            static_cast<unsigned>(layout.size * 8u), name, error);
        if (!scalar && (error == EInstanceLoadReason::none))
        {
            error = EInstanceLoadReason::allocation_failed;
        }
        return scalar;
    }
    const EDocumentValueKind old_kind = full ? EDocumentValueKind::object : document.value_kind(selection);
    if (!full)
    {
        if (((layout.category == ECategory::array) && (old_kind != EDocumentValueKind::array)) ||
            ((layout.category != ECategory::array) && (old_kind != EDocumentValueKind::object) && (old_kind != EDocumentValueKind::array)) ||
            ((old_kind == EDocumentValueKind::array) && (document.child_count(selection) > layout.count)))
        {
            error = EInstanceLoadReason::invalid_declaration;
            return {};
        }
        if (old_kind == EDocumentValueKind::object)
        {
            for (detail::SOccurrence child = document.first_child(selection); child.is_valid();
                child = document.next_sibling(child))
            {
                if (!document.is_object_entry(child) ||
                    !(layout.category == ECategory::structure ?
                        schema.find_member(type, document.name(child)) :
                        schema.find_field(type, document.name(child))))
                {
                    error = EInstanceLoadReason::invalid_declaration;
                    return {};
                }
            }
        }
    }
    const bool positional = !full && (old_kind == EDocumentValueKind::array);
    const CNodeKey result = ((layout.category == ECategory::array) || positional) ?
        target.create_array(name) : target.create_object(name);
    if (!result)
    {
        error = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const std::uint32_t count = full ? layout.count :
        (((layout.category == ECategory::array) || positional) ? document.child_count(selection) : layout.count);
    detail::SOccurrence cursor = (positional || (!full && (layout.category == ECategory::array))) ?
        document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child_selection = full ? detail::SOccurrence{} :
            (cursor.is_valid() ? cursor : selected_child(schema, document, type, layout, selection, i));
        if (cursor.is_valid())
        {
            cursor = document.next_sibling(cursor);
        }
        if (!full && !child_selection.is_valid())
        {
            continue;
        }
        CNodeKey child;
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member))
            {
                error = EInstanceLoadReason::invalid_declaration;
                break;
            }
            child = decode_selection(target, schema, member.type, document, child_selection, full,
                (bytes ? (bytes + member.offset) : nullptr),
                (positional ? CStringView{} : schema.name(member.name)), error, (depth + 1u));
        }
        else if (layout.category == ECategory::array)
        {
            child = decode_selection(target, schema, layout.element_or_storage, document, child_selection, full,
                (bytes ? (bytes + (layout.stride * i)) : nullptr), {}, error, (depth + 1u));
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                error = EInstanceLoadReason::invalid_declaration;
                break;
            }
            const std::uint64_t word = read_bits(bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t raw = (word & field.mask) >> field.shift;
            child = decode_scalar(target, schema, field.type, raw, field.width,
                (positional ? CStringView{} : schema.name(field.name)), error);
        }
        if (!child || !target.append_child(result, child).succeeded())
        {
            if (child && target.is_detached(child))
            {
                (void)target.erase(child);
            }
            (void)target.erase(result);
            if (error == EInstanceLoadReason::none)
            {
                error = EInstanceLoadReason::allocation_failed;
            }
            return {};
        }
    }
    if (error != EInstanceLoadReason::none)
    {
        (void)target.erase(result);
        return {};
    }
    return result;
}

[[nodiscard]] static bool overlay_selection(const CResolvedSchema& schema, const CSchemaIndex type,
    const detail::CDocumentRead& document, const detail::SOccurrence selection,
    const std::uint8_t* const selected_bytes, std::uint8_t* const inherited_bytes,
    const unsigned depth = 0u) noexcept
{
    if (!selection.is_valid())
    {
        return true;
    }
    if (depth >= k_max_depth)
    {
        return false;
    }
    SType layout;
    if (!schema.type(type, layout))
    {
        return false;
    }
    if ((layout.category == ECategory::primitive) || (layout.category == ECategory::enumeration))
    {
        if (layout.size)
        {
            std::memcpy(inherited_bytes, selected_bytes, static_cast<std::size_t>(layout.size));
        }
        return true;
    }
    const std::uint32_t count = ((layout.category == ECategory::array) ||
        (document.value_kind(selection) == EDocumentValueKind::array)) ? document.child_count(selection) : layout.count;
    detail::SOccurrence cursor = (document.value_kind(selection) == EDocumentValueKind::array) ?
        document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child = cursor.is_valid() ? cursor :
            selected_child(schema, document, type, layout, selection, i);
        if (cursor.is_valid())
        {
            cursor = document.next_sibling(cursor);
        }
        if (!child.is_valid())
        {
            continue;
        }
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member) ||
                !overlay_selection(schema, member.type, document, child,
                    (selected_bytes ? (selected_bytes + member.offset) : nullptr),
                    (inherited_bytes ? (inherited_bytes + member.offset) : nullptr), (depth + 1u)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::array)
        {
            if (!overlay_selection(schema, layout.element_or_storage, document, child,
                (selected_bytes ? (selected_bytes + (layout.stride * i)) : nullptr),
                (inherited_bytes ? (inherited_bytes + (layout.stride * i)) : nullptr), (depth + 1u)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                return false;
            }
            const std::uint64_t old_word = read_bits(selected_bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t new_word = read_bits(inherited_bytes, static_cast<std::size_t>(layout.size));
            write_bits(inherited_bytes, static_cast<std::size_t>(layout.size),
                ((new_word & ~field.mask) | (old_word & field.mask)));
        }
    }
    return true;
}

struct SStagedEdit
{
    std::uint32_t index{}, offset{};
    CNodeKey declaration;
};

struct SResolvedSelectionStep
{
    EInstanceSelectionStepKind kind{};
    std::uint32_t ordinal{};
    CSchemaIndex child_type;
    CStringView name;
};

[[nodiscard]] static bool overlay_except(const CResolvedSchema& schema, const CSchemaIndex type,
    const detail::CDocumentRead& document, const detail::SOccurrence selection,
    const std::uint8_t* const old_bytes, std::uint8_t* const new_bytes,
    const TPodVector<SResolvedSelectionStep>& path, const std::size_t depth) noexcept
{
    if (!selection.is_valid())
    {
        return true;
    }
    if (depth >= path.size())
    {
        return overlay_selection(schema, type, document, selection, old_bytes, new_bytes);
    }
    SType layout;
    if (!schema.type(type, layout) || (layout.category == ECategory::primitive) ||
        (layout.category == ECategory::enumeration))
    {
        return false;
    }
    const bool positional = document.value_kind(selection) == EDocumentValueKind::array;
    const std::uint32_t count = positional ? document.child_count(selection) : layout.count;
    detail::SOccurrence cursor = positional ? document.first_child(selection) : detail::SOccurrence{};
    for (std::uint32_t i = 0u; i < count; ++i)
    {
        const detail::SOccurrence child = cursor.is_valid() ? cursor :
            selected_child(schema, document, type, layout, selection, i);
        if (cursor.is_valid())
        {
            cursor = document.next_sibling(cursor);
        }
        if (!child.is_valid())
        {
            continue;
        }
        if ((i == path[depth].ordinal) && ((depth + 1u) == path.size()))
        {
            continue;
        }
        const bool descend = (i == path[depth].ordinal);
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member))
            {
                return false;
            }
            const std::uint8_t* const source = old_bytes ? old_bytes + member.offset : nullptr;
            std::uint8_t* const destination = new_bytes ? new_bytes + member.offset : nullptr;
            if (!(descend ? overlay_except(schema, member.type, document, child, source, destination, path, (depth + 1u)) :
                overlay_selection(schema, member.type, document, child, source, destination)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::array)
        {
            const std::uint8_t* const source = old_bytes ? (old_bytes + (layout.stride * i)) : nullptr;
            std::uint8_t* const destination = new_bytes ? (new_bytes + (layout.stride * i)) : nullptr;
            if (!(descend ? overlay_except(schema, layout.element_or_storage, document, child, source, destination, path, (depth + 1u)) :
                overlay_selection(schema, layout.element_or_storage, document, child, source, destination)))
            {
                return false;
            }
        }
        else if (layout.category == ECategory::bit_structure)
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                return false;
            }
            const std::uint64_t before = read_bits(old_bytes, static_cast<std::size_t>(layout.size));
            const std::uint64_t after = read_bits(new_bytes, static_cast<std::size_t>(layout.size));
            write_bits(new_bytes, static_cast<std::size_t>(layout.size),
                ((after & ~field.mask) | (before & field.mask)));
        }
    }
    return true;
}

[[nodiscard]] static bool resolve_steps(const CResolvedSchema& schema, const CSchemaIndex root,
    const SInstanceSelectionStep* const steps, const std::size_t count,
    TPodVector<SResolvedSelectionStep>& resolved) noexcept
{
    CSchemaIndex current = root;
    for (std::size_t i = 0u; i < count; ++i)
    {
        SType layout;
        if (!schema.type(current, layout))
        {
            return false;
        }
        SResolvedSelectionStep step;
        step.kind = steps[i].kind;
        if ((layout.category == ECategory::array) && (steps[i].kind == EInstanceSelectionStepKind::element))
        {
            if (steps[i].element >= layout.count)
            {
                return false;
            }
            step.ordinal = steps[i].element;
            step.child_type = layout.element_or_storage;
        }
        else if ((layout.category == ECategory::structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CSchemaIndex member_index = schema.find_member(current, steps[i].member);
            SMember member;
            if (!member_index || !schema.member(member_index, member))
            {
                return false;
            }
            step.child_type = member.type;
            step.name = schema.name(member.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.member_at(current, ordinal) == member_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else if ((layout.category == ECategory::bit_structure) && (steps[i].kind == EInstanceSelectionStepKind::member))
        {
            const CSchemaIndex field_index = schema.find_field(current, steps[i].member);
            SField field;
            if (!field_index || !schema.field(field_index, field))
            {
                return false;
            }
            step.child_type = field.type;
            step.name = schema.name(field.name);
            for (std::uint32_t ordinal = 0u; ordinal < layout.count; ++ordinal)
            {
                if (schema.field_at(current, ordinal) == field_index)
                {
                    step.ordinal = ordinal;
                    break;
                }
            }
        }
        else
        {
            return false;
        }
        if (!resolved.push_back(step))
        {
            return false;
        }
        current = step.child_type;
    }
    return true;
}

[[nodiscard]] static CNodeKey copy_edit_value(CLiveDocument& target, const detail::CDocumentRead& source,
    const detail::SOccurrence value, const CStringView& name, const unsigned depth = 0u) noexcept
{
    if ((depth >= k_max_depth) || !source.contains(value))
    {
        return {};
    }
    CNodeKey copied;
    switch (source.value_kind(value))
    {
        case EDocumentValueKind::empty: copied = target.create_empty(name); break;
        case EDocumentValueKind::null_value: copied = target.create_null(name); break;
        case EDocumentValueKind::boolean:
        {
            bool scalar{};
            if (!source.boolean_value(value, scalar))
            {
                return {};
            }
            copied = target.create_boolean(scalar, name);
            break;
        }
        case EDocumentValueKind::integer:
        {
            CIntegerMetadata metadata;
            std::int64_t signed_value{};
            std::uint64_t unsigned_value{};
            if (!source.integer_metadata(value, metadata))
            {
                return {};
            }
            copied = source.signed_integer_value(value, signed_value) ?
                target.create_signed_integer(signed_value, metadata, name) :
                ((source.unsigned_integer_value(value, unsigned_value) ?
                    target.create_unsigned_integer(unsigned_value, metadata, name) : CNodeKey{}));
            break;
        }
        case EDocumentValueKind::floating_point:
        {
            double scalar{};
            if (!source.floating_point_value(value, scalar))
            {
                return {};
            }
            copied = target.create_floating_point(scalar, name);
            break;
        }
        case EDocumentValueKind::string: copied = target.create_string(source.string_value(value), name); break;
        case EDocumentValueKind::array: copied = target.create_array(name); break;
        case EDocumentValueKind::object: copied = target.create_object(name); break;
        default: return {};
    }
    if (!copied)
    {
        return {};
    }
    for (detail::SOccurrence child = source.first_child(value); child.is_valid(); child = source.next_sibling(child))
    {
        const CNodeKey item = copy_edit_value(target, source, child,
            (source.is_object_entry(child) ? source.name(child) : CStringView{}), (depth + 1u));
        if (!item || !target.append_child(copied, item).succeeded())
        {
            if (item && target.is_detached(item))
            {
                (void)target.erase(item);
            }
            (void)target.erase(copied);
            return {};
        }
    }
    return copied;
}

[[nodiscard]] static CNodeKey new_container(CLiveDocument& document, const SType& layout, const CStringView& name) noexcept
{
    return layout.category == ECategory::array ? document.create_array(name) :
        (((layout.category == ECategory::structure) || (layout.category == ECategory::bit_structure)) ?
            document.create_object(name) : CNodeKey{});
}

[[nodiscard]] static CNodeKey child_at(CLiveDocument& document, const CNodeKey parent, const std::uint32_t ordinal) noexcept
{
    CNodeKey child = document.first_child(parent);
    for (std::uint32_t i = 0u; (i < ordinal) && child; ++i)
    {
        child = document.next_sibling(child);
    }
    return child;
}

[[nodiscard]] static CNodeKey convert_positional(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex type, const CNodeKey positional) noexcept
{
    SType layout;
    if (!schema.type(type, layout) || ((layout.category != ECategory::structure) && (layout.category != ECategory::bit_structure)))
    {
        return {};
    }
    const CNodeKey named = document.create_object(document.name(positional));
    if (!named)
    {
        return {};
    }
    const detail::CDocumentRead query{ document };
    CNodeKey source_child = document.first_child(positional);
    for (std::uint32_t i = 0u; source_child; ++i)
    {
        CStringView name;
        if (i >= layout.count)
        {
            (void)document.erase(named);
            return {};
        }
        if (layout.category == ECategory::structure)
        {
            SMember member;
            if (!schema.member(schema.member_at(type, i), member))
            {
                (void)document.erase(named);
                return {};
            }
            name = schema.name(member.name);
        }
        else
        {
            SField field;
            if (!schema.field(schema.field_at(type, i), field))
            {
                (void)document.erase(named);
                return {};
            }
            name = schema.name(field.name);
        }
        const CNodeKey copied = copy_edit_value(document, query, detail::SOccurrence{ source_child }, name);
        if (!copied || !document.append_child(named, copied).succeeded())
        {
            if (copied && document.is_detached(copied))
            {
                (void)document.erase(copied);
            }
            (void)document.erase(named);
            return {};
        }
        source_child = document.next_sibling(source_child);
    }
    return named;
}

[[nodiscard]] static bool edit_selection_tree(CLiveDocument& document, const CResolvedSchema& schema,
    const CSchemaIndex root_type, CNodeKey& root, const TPodVector<SResolvedSelectionStep>& steps,
    CNodeKey replacement, const bool remove) noexcept
{
    if (steps.size() == 0u)
    {
        if (remove)
        {
            return false;
        }
        if (root)
        {
            (void)document.erase(root);
        }
        if (!document.set_name(replacement, CStringView{ "declaration" }))
        {
            return false;
        }
        root = replacement;
        return true;
    }
    if (!root)
    {
        if (remove)
        {
            return false;
        }
        SType type;
        if (!schema.type(root_type, type))
        {
            return false;
        }
        root = new_container(document, type, CStringView{ "declaration" });
        if (!root)
        {
            return false;
        }
    }
    CNodeKey current = root;
    CSchemaIndex current_type = root_type;
    for (std::size_t depth = 0u; depth < steps.size(); ++depth)
    {
        const SResolvedSelectionStep& step = steps[depth];
        SType type;
        if (!schema.type(current_type, type))
        {
            return false;
        }
        if ((step.kind == EInstanceSelectionStepKind::member) &&
            (document.value_type(current) == ELiveValueType::array))
        {
            const CNodeKey converted = convert_positional(document, schema, current_type, current);
            if (!converted)
            {
                return false;
            }
            const CNodeKey old_payload = document.detach_payload(current);
            if (!old_payload)
            {
                (void)document.erase(converted);
                return false;
            }
            if (!document.set_name(converted, CStringView{}) ||
                !document.attach_payload(current, converted).is_valid())
            {
                (void)document.attach_payload(current, old_payload);
                if (document.contains(converted) && document.is_detached(converted))
                {
                    (void)document.erase(converted);
                }
                return false;
            }
            (void)document.erase(old_payload);
        }
        const bool array = step.kind == EInstanceSelectionStepKind::element;
        if (array ? (document.value_type(current) != ELiveValueType::array) :
            (document.value_type(current) != ELiveValueType::object))
        {
            return false;
        }
        const std::uint32_t count = document.child_count(current);
        if (array && (step.ordinal > count))
        {
            return false;
        }
        CNodeKey child = array ? child_at(document, current, step.ordinal) :
            document.object_child(current, step.name);
        const bool last = depth + 1u == steps.size();
        if (last)
        {
            if (remove)
            {
                if (!child || (array && ((step.ordinal + 1u) != count)))
                {
                    return false;
                }
                return document.erase(child);
            }
            if (child)
            {
                const CNodeKey old_payload = document.detach_payload(child);
                if (!old_payload)
                {
                    return false;
                }
                if (!document.set_name(replacement, CStringView{}) ||
                    !document.attach_payload(child, replacement).is_valid())
                {
                    (void)document.attach_payload(child, old_payload);
                    return false;
                }
                (void)document.erase(old_payload);
            }
            else
            {
                if (!array && !document.set_name(replacement, step.name))
                {
                    return false;
                }
                if (!document.append_child(current, replacement).succeeded())
                {
                    return false;
                }
            }
            return true;
        }
        if (!child)
        {
            if (remove)
            {
                return false;
            }
            SType child_layout;
            if (!schema.type(step.child_type, child_layout))
            {
                return false;
            }
            child = new_container(document, child_layout, (array ? CStringView{} : step.name));
            if (!child || !document.append_child(current, child).succeeded())
            {
                if (child && document.is_detached(child))
                {
                    (void)document.erase(child);
                }
                return false;
            }
        }
        current = child;
        current_type = step.child_type;
    }
    return false;
}

bool CLiveInstances::edit_existing(const std::uint32_t index, const CNodeKey selection,
    const CByteConstView& complete, const bool full_declaration,
    SInstanceDiagnostic& diagnostic) noexcept
{
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || (index >= m_records.size()) ||
        (m_records[index].extent && (!complete.is_ready() || (complete.size() < m_records[index].extent))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    TPodVector<SStagedEdit> changes;
    std::uint64_t total{};
    for (std::uint32_t i = index; i < m_records.size(); ++i)
    {
        std::uint32_t ancestor = i;
        while ((ancestor != k_no_parent) && (ancestor != index))
        {
            ancestor = m_records[ancestor].parent;
        }
        if (ancestor != index)
        {
            continue;
        }
        total = (total + 127u) & ~std::uint64_t{ 127u };
        if ((total > UINT32_MAX) || (m_records[i].extent > UINT32_MAX - total) ||
            (m_records[i].extent > memory::k_byte_size_ceiling - total) ||
            !changes.push_back({ i, static_cast<std::uint32_t>(total), {} }))
        {
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            return false;
        }
        total += m_records[i].extent;
    }
    CByteBuffer staged;
    if (total && (!staged.allocate(static_cast<std::size_t>(total), 128u) ||
        !staged.set_size(static_cast<std::size_t>(total))))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const detail::CDocumentRead document{ m_document };
    const auto cleanup = [&]() noexcept
    {
        for (std::size_t i = 0u; i < changes.size(); ++i)
        {
            const CNodeKey node = changes[i].declaration;
            if (node && m_document.contains(node) && m_document.is_detached(node))
            {
                (void)m_document.erase(node);
            }
        }
    };
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        SStagedEdit& change = changes[j];
        const SRecord& record = m_records[change.index];
        std::uint8_t* const target = record.extent ? (staged.data() + change.offset) : nullptr;
        if (change.index == index)
        {
            if (record.extent)
            {
                std::memcpy(target, complete.data(), record.extent);
            }
        }
        else
        {
            const SStagedEdit* parent = nullptr;
            for (std::size_t p = 0u; p < j; ++p)
            {
                if (changes[p].index == record.parent)
                {
                    parent = &changes[p];
                    break;
                }
            }
            if (!parent)
            {
                diagnostic.reason = EInstanceLoadReason::invalid_input;
                cleanup();
                return false;
            }
            if (record.extent)
            {
                std::memcpy(target, (staged.data() + parent->offset), record.extent);
            }
            if (!overlay_selection(*schema, record.type, document,
                detail::SOccurrence{ record.declaration },
                (record.extent ? (m_payload.data() + record.offset) : nullptr), target))
            {
                diagnostic.reason = EInstanceLoadReason::invalid_declaration;
                cleanup();
                return false;
            }
        }
        const CNodeKey selected = change.index == index ? selection : record.declaration;
        EInstanceLoadReason error{ EInstanceLoadReason::none };
        change.declaration = decode_selection(m_document, *schema, record.type, document,
            detail::SOccurrence{ selected }, ((change.index == index) && full_declaration),
            target, CStringView{ "declaration" }, error);
        if (error != EInstanceLoadReason::none)
        {
            diagnostic.reason = error;
            cleanup();
            return false;
        }
    }
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        const SStagedEdit& change = changes[j];
        SRecord& record = m_records[change.index];
        const CNodeKey old = record.declaration;
        if ((old && !m_document.detach(old)) ||
            (change.declaration && !m_document.append_child(record.entry, change.declaration).succeeded()))
        {
            disable();
            diagnostic.reason = EInstanceLoadReason::allocation_failed;
            cleanup();
            return false;
        }
        record.declaration = change.declaration;
        if (old)
        {
            (void)m_document.erase(old);
        }
    }
    for (std::size_t j = 0u; j < changes.size(); ++j)
    {
        const SStagedEdit& change = changes[j];
        const SRecord& record = m_records[change.index];
        if (record.extent)
        {
            std::memcpy((m_payload.data() + record.offset), (staged.data() + change.offset), record.extent);
        }
    }
    return true;
}

CInstanceHandle CLiveInstances::capture_base(const CStringView& type, const CStringView& name,
    const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const CResolvedSchema* const schema = m_binding.resolved();
    if (!loaded_ready() || !schema || type.empty() || name.empty())
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return {};
    }
    CByteBuffer type_storage, name_storage;
    CStringView stable_type, stable_name;
    if (!stabilise_name(type, type_storage, stable_type) ||
        !stabilise_name(name, name_storage, stable_name))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return {};
    }
    const CSchemaIndex type_index = schema->find_type(stable_type);
    SType layout;
    if (!type_index || !schema->type(type_index, layout))
    {
        diagnostic.reason = EInstanceLoadReason::unknown_type;
        return {};
    }
    if (layout.size && (!complete.is_ready() || (complete.size() < layout.size) ||
        (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return {};
    }
    const CInstanceHandle existing = find_base(stable_type, stable_name);
    std::uint32_t index{};
    if (record_index(existing, index))
    {
        return edit_existing(index, {}, complete, true, diagnostic) ? existing : CInstanceHandle{};
    }
    EInstanceLoadReason error{ EInstanceLoadReason::none };
    const detail::CDocumentRead document{ m_document };
    const CNodeKey declaration = decode_selection(m_document, *schema, type_index, document, {}, true,
        complete.data(), CStringView{ "declaration" }, error);
    if (error != EInstanceLoadReason::none)
    {
        diagnostic.reason = error;
        return {};
    }
    const CInstanceHandle result = append_instance(type_index, k_no_parent, stable_type, stable_name, document,
        detail::SOccurrence{ declaration }, complete, diagnostic);
    if (declaration)
    {
        (void)m_document.erase(declaration);
    }
    return result;
}

bool CLiveInstances::capture_specialisation(const CInstanceHandle instance,
    const CByteConstView& complete, SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const SRecord& record = m_records[index];
    SType layout;
    if (!m_binding.resolved()->type(record.type, layout) ||
        (record.extent && (!complete.is_ready() || (complete.size() < record.extent) ||
         (reinterpret_cast<std::uintptr_t>(complete.data()) & (layout.alignment - 1u)))))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_range;
        return false;
    }
    CByteBuffer captured;
    if (record.extent && (!captured.allocate(record.extent, 128u) || !captured.set_size(record.extent)))
    {
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    if (record.extent)
    {
        std::memcpy(captured.data(), (m_payload.data() + record.offset), record.extent);
    }
    const detail::CDocumentRead document{ m_document };
    if (!overlay_selection(*m_binding.resolved(), record.type, document,
        detail::SOccurrence{ record.declaration }, complete.data(), captured.data()))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    return edit_existing(index, record.declaration, captured.const_view(), false, diagnostic);
}

bool CLiveInstances::set_selection(const CInstanceHandle instance,
    const SInstanceSelectionStep* const steps, const std::size_t step_count,
    const CInstanceDocumentQuery& source, const CInstanceHandle value,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    const detail::SOccurrence source_value = detail::SInstanceHandleAccess::occurrence(value);
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent) ||
        (step_count && !steps) || !source.m_query.contains(source_value))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    const SRecord& record = m_records[index];
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(*schema, record.type, steps, step_count, resolved))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const detail::CDocumentRead local{ m_document };
    CNodeKey replacement = copy_edit_value(m_document, source.m_query, source_value,
        (step_count ? CStringView{} : CStringView{ "declaration" }));
    EInstanceLoadReason decode_error{ EInstanceLoadReason::none };
    CNodeKey staged = record.declaration ? decode_selection(m_document, *schema, record.type,
        local, detail::SOccurrence{ record.declaration }, false,
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        CStringView{ "declaration" }, decode_error) : CNodeKey{};
    if (!replacement || (record.declaration && !staged))
    {
        if (replacement)
        {
            (void)m_document.erase(replacement);
        }
        if (staged)
        {
            (void)m_document.erase(staged);
        }
        diagnostic.reason = decode_error == EInstanceLoadReason::none ?
            EInstanceLoadReason::allocation_failed : decode_error;
        return false;
    }
    if (!edit_selection_tree(m_document, *schema, record.type, staged, resolved, replacement, false))
    {
        if (staged)
        {
            (void)m_document.erase(staged);
        }
        if (replacement && m_document.contains(replacement) && m_document.is_detached(replacement))
        {
            (void)m_document.erase(replacement);
        }
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    CByteBuffer encoded;
    if (record.extent && (!encoded.allocate(record.extent, 128u) || !encoded.set_size(record.extent)))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const SRecord& parent = m_records[record.parent];
    detail::SValueDiagnostic error;
    const bool constructed = detail::construct_alternative(*schema, local, record.type,
        detail::SOccurrence{ staged }, (parent.extent ? (m_payload.data() + parent.offset) : nullptr),
        parent.extent, encoded.data(), encoded.size(), error);
    if (!constructed)
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    if (step_count && !overlay_except(*schema, record.type, local,
        detail::SOccurrence{ record.declaration },
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        encoded.data(), resolved, 0u))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const bool applied = edit_existing(index, staged, encoded.const_view(), false, diagnostic);
    (void)m_document.erase(staged);
    return applied;
}

bool CLiveInstances::remove_selection(const CInstanceHandle instance,
    const SInstanceSelectionStep* const steps, const std::size_t step_count,
    SInstanceDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    std::uint32_t index{};
    if (!record_index(instance, index) || (m_records[index].parent == k_no_parent) ||
        !step_count || !steps || !m_records[index].declaration)
    {
        diagnostic.reason = EInstanceLoadReason::invalid_input;
        return false;
    }
    const CResolvedSchema* const schema = m_binding.resolved();
    const SRecord& record = m_records[index];
    TPodVector<SResolvedSelectionStep> resolved;
    if (!resolve_steps(*schema, record.type, steps, step_count, resolved))
    {
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const detail::CDocumentRead local{ m_document };
    EInstanceLoadReason decode_error{ EInstanceLoadReason::none };
    CNodeKey staged = decode_selection(m_document, *schema, record.type,
        local, detail::SOccurrence{ record.declaration }, false,
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        CStringView{ "declaration" }, decode_error);
    if (!staged)
    {
        diagnostic.reason = decode_error == EInstanceLoadReason::none ?
            EInstanceLoadReason::allocation_failed : decode_error;
        return false;
    }
    if (!edit_selection_tree(m_document, *schema, record.type, staged, resolved, {}, true))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    CByteBuffer encoded;
    if (record.extent && (!encoded.allocate(record.extent, 128u) || !encoded.set_size(record.extent)))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::allocation_failed;
        return false;
    }
    const SRecord& parent = m_records[record.parent];
    detail::SValueDiagnostic error;
    if (!detail::construct_alternative(*schema, local, record.type,
        detail::SOccurrence{ staged }, (parent.extent ? (m_payload.data() + parent.offset) : nullptr),
        parent.extent, encoded.data(), encoded.size(), error))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    if (!overlay_except(*schema, record.type, local,
        detail::SOccurrence{ record.declaration },
        (record.extent ? (m_payload.data() + record.offset) : nullptr),
        encoded.data(), resolved, 0u))
    {
        (void)m_document.erase(staged);
        diagnostic.reason = EInstanceLoadReason::invalid_declaration;
        return false;
    }
    const bool applied = edit_existing(index, staged, encoded.const_view(), false, diagnostic);
    (void)m_document.erase(staged);
    return applied;
}

} // namespace schema

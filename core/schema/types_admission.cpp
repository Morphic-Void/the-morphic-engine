
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    types_admission.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    25 Oct 26
//
//  Admit incomplete declarations without resolving them or joining type fragments.

#include "schema/types_admission.hpp"
#include "data_model/document_copy.hpp"
#include "schema/type_compatibility.hpp"

#include <algorithm>

namespace schema::detail
{

enum class EDefinitionShape : std::uint8_t
{
    invalid = 0u, enumeration, structure, bit_structure, association, interface_type,
    member, field, interface_member, array, type_reference, detail, members, fields,
    interface_members, labels, decorations, literal, string, number, boolean, target
};

[[nodiscard]] static EDefinitionShape category(const CStringView& name) noexcept
{
    if (name == CStringView{ "enumerations" }) return EDefinitionShape::enumeration;
    if (name == CStringView{ "structures" }) return EDefinitionShape::structure;
    if (name == CStringView{ "bit_structures" }) return EDefinitionShape::bit_structure;
    if (name == CStringView{ "associations" }) return EDefinitionShape::association;
    if (name == CStringView{ "interfaces" }) return EDefinitionShape::interface_type;
    return EDefinitionShape::invalid;
}

[[nodiscard]] static EDefinitionShape property(const EDefinitionShape shape, const CStringView& name) noexcept
{
    if ((name == CStringView{ "decorations" }) && (shape != EDefinitionShape::detail)) return EDefinitionShape::decorations;
    if ((name == CStringView{ "storage" }) && ((shape == EDefinitionShape::enumeration) || (shape == EDefinitionShape::bit_structure))) return EDefinitionShape::string;
    if ((name == CStringView{ "values" }) && (shape == EDefinitionShape::enumeration)) return EDefinitionShape::labels;
    if (name == CStringView{ "members" })
    {
        if (shape == EDefinitionShape::structure) return EDefinitionShape::members;
        if (shape == EDefinitionShape::bit_structure) return EDefinitionShape::fields;
        if (shape == EDefinitionShape::interface_type) return EDefinitionShape::interface_members;
    }
    if ((name == CStringView{ "detail" }) && (shape == EDefinitionShape::structure)) return EDefinitionShape::detail;
    if ((name == CStringView{ "target" }) && (shape == EDefinitionShape::association)) return EDefinitionShape::target;
    if (name == CStringView{ "type" })
    {
        if (shape == EDefinitionShape::member) return EDefinitionShape::type_reference;
        if ((shape == EDefinitionShape::field) || (shape == EDefinitionShape::interface_member)) return EDefinitionShape::string;
    }
    if ((name == CStringView{ "default" }) && ((shape == EDefinitionShape::member) || (shape == EDefinitionShape::field))) return EDefinitionShape::literal;
    if ((name == CStringView{ "offset" }) && (shape == EDefinitionShape::member)) return EDefinitionShape::number;
    if ((name == CStringView{ "mask" }) && (shape == EDefinitionShape::field)) return EDefinitionShape::number;
    if ((name == CStringView{ "interpretation" }) && (shape == EDefinitionShape::field)) return EDefinitionShape::string;
    if ((name == CStringView{ "count" }) && ((shape == EDefinitionShape::array) || (shape == EDefinitionShape::interface_member))) return EDefinitionShape::number;
    if ((name == CStringView{ "element" }) && (shape == EDefinitionShape::array)) return EDefinitionShape::type_reference;
    if ((name == CStringView{ "stride" }) && (shape == EDefinitionShape::array)) return EDefinitionShape::number;
    if ((name == CStringView{ "alignment" }) && ((shape == EDefinitionShape::array) || (shape == EDefinitionShape::detail))) return EDefinitionShape::number;
    if ((name == CStringView{ "size" }) && (shape == EDefinitionShape::detail)) return EDefinitionShape::number;
    if ((name == CStringView{ "internal" }) && (shape == EDefinitionShape::detail)) return EDefinitionShape::boolean;
    return EDefinitionShape::invalid;
}

[[nodiscard]] static bool merge_content(const EDefinitionShape shape, const CStringView& name) noexcept
{
    return property(shape, name) == EDefinitionShape::decorations || property(shape, name) == EDefinitionShape::literal ||
        ((shape == EDefinitionShape::detail) && (name == CStringView{ "internal" }));
}

[[nodiscard]] static bool fail(STypesAdmissionDiagnostic& error, const EReason reason,
    const SOccurrence first, const SOccurrence second = {}) noexcept
{
    error = { reason, first, second };
    return false;
}

[[nodiscard]] static EDefinitionShape child_shape(const EDefinitionShape shape, const CStringView& name) noexcept
{
    if (shape == EDefinitionShape::members) return EDefinitionShape::member;
    if (shape == EDefinitionShape::fields) return EDefinitionShape::field;
    if (shape == EDefinitionShape::interface_members) return EDefinitionShape::interface_member;
    if (shape == EDefinitionShape::labels) return EDefinitionShape::number;
    if (shape == EDefinitionShape::decorations) return EDefinitionShape::string;
    return property(shape, name);
}

[[nodiscard]] static bool admit_node(const CDocumentRead& source, const SOccurrence value,
    EDefinitionShape& shape, STypesAdmissionDiagnostic& error) noexcept
{
    const auto kind = source.value_kind(value);
    if (shape == EDefinitionShape::literal) return true;
    if (shape == EDefinitionShape::number)
    {
        return ((kind == EDocumentValueKind::integer) || (kind == EDocumentValueKind::floating_point)) || fail(error, EReason::invalid_input, value);
    }
    if (shape == EDefinitionShape::boolean) return (kind == EDocumentValueKind::boolean) || fail(error, EReason::invalid_input, value);
    if (shape == EDefinitionShape::target)
    {
        return ((kind == EDocumentValueKind::string) || (kind == EDocumentValueKind::null_value)) || fail(error, EReason::invalid_input, value);
    }
    if (shape == EDefinitionShape::type_reference)
    {
        if (kind == EDocumentValueKind::string) return true;
        shape = EDefinitionShape::array;
    }
    if (shape == EDefinitionShape::string) return (kind == EDocumentValueKind::string) || fail(error, EReason::invalid_input, value);
    const bool list = (shape == EDefinitionShape::members) || (shape == EDefinitionShape::fields) || (shape == EDefinitionShape::interface_members);
    if (kind != (list ? EDocumentValueKind::array : EDocumentValueKind::object)) return fail(error, EReason::invalid_input, value);
    TPodVector<SOccurrence> names;
    for (SOccurrence child = source.first_child(value); child; child = source.next_sibling(child))
    {
        if (list || (shape == EDefinitionShape::labels))
        {
            if (!source.is_object_entry(child) || !valid_identifier(source.name(child))) return fail(error, EReason::invalid_identifier, child);
            if (list && !names.push_back(child)) return fail(error, EReason::allocation_failed, child);
        }
        else if (shape == EDefinitionShape::decorations)
        {
            if (!source.name(child).length()) return fail(error, EReason::invalid_identifier, child);
        }
        else
        {
            if (property(shape, source.name(child)) == EDefinitionShape::invalid) return fail(error, EReason::unknown_property, child);
        }
    }
    if (names.size() > 1u)
    {
        std::sort(names.data(), names.data() + names.size(), [&](const SOccurrence a, const SOccurrence b) noexcept
        {
            return source.name(a).relationship(source.name(b)) < 0;
        });
        for (std::size_t i = 1u; i < names.size(); ++i)
        {
            if (source.name(names[i - 1u]) == source.name(names[i])) return fail(error, EReason::duplicate_declaration, names[i], names[i - 1u]);
        }
    }
    return true;
}

[[nodiscard]] static bool admit(const CDocumentRead& source, const SOccurrence root,
    EDefinitionShape shape, STypesAdmissionDiagnostic& error, unsigned depth) noexcept
{
    const unsigned root_depth = depth;
    EDefinitionShape parents[document_limits::k_max_recursion_depth]{};
    auto value = root;
    while (value)
    {
        if (depth >= document_limits::k_max_recursion_depth) return fail(error, EReason::storage_limit, value);
        if (!admit_node(source, value, shape, error)) return false;
        const auto child = shape == EDefinitionShape::literal ? SOccurrence{} : source.first_child(value);
        if (child)
        {
            parents[depth++] = shape;
            value = child;
            shape = child_shape(shape, source.name(child));
            continue;
        }
        while (depth != root_depth && !source.next_sibling(value))
        {
            value = source.parent(value);
            --depth;
        }
        if (depth == root_depth) break;
        value = source.next_sibling(value);
        shape = child_shape(parents[depth - 1u], source.name(value));
    }
    return true;
}

[[nodiscard]] static bool catalogue(const CDocumentRead& source, const SOccurrence section,
    TPodVector<SAdmittedDefinition>& definitions, STypesAdmissionDiagnostic& error) noexcept
{
    if (!section) return true;
    if (source.value_kind(section) != EDocumentValueKind::object) return fail(error, EReason::invalid_input, section);
    for (SOccurrence group = source.first_child(section); group; group = source.next_sibling(group))
    {
        const EDefinitionShape shape = category(source.name(group));
        if (shape == EDefinitionShape::invalid) return fail(error, EReason::unknown_property, group);
        if (source.value_kind(group) != EDocumentValueKind::object) return fail(error, EReason::invalid_input, group);
        for (SOccurrence item = source.first_child(group); item; item = source.next_sibling(item))
        {
            const CStringView name = source.name(item);
            if (!valid_identifier(name)) return fail(error, EReason::invalid_identifier, item);
            for (unsigned i = 1u; i <= static_cast<unsigned>(EPrimitive::b8); ++i)
            {
                if (name == primitive_name(static_cast<EPrimitive>(i))) return fail(error, EReason::duplicate_declaration, item);
            }
            if (!admit(source, item, shape, error, 3u)) return false;
            if (!definitions.push_back({ name, item, shape })) return fail(error, EReason::allocation_failed, item);
        }
    }
    if (definitions.size() > 1u)
    {
        std::sort(definitions.data(), definitions.data() + definitions.size(), [](const SAdmittedDefinition& a, const SAdmittedDefinition& b) noexcept
        {
            return a.name.relationship(b.name) < 0;
        });
        for (std::size_t i = 1u; i < definitions.size(); ++i)
        {
            if (definitions[i - 1u].name == definitions[i].name) return fail(error, EReason::duplicate_declaration, definitions[i].source, definitions[i - 1u].source);
        }
    }
    return true;
}

[[nodiscard]] static bool same_scalar(const CDocumentRead& a, const SOccurrence left,
    const CDocumentRead& b, const SOccurrence right) noexcept
{
    using document_translation::detail::SCopiedValue;
    SCopiedValue x, y;
    if (!document_translation::detail::read_copied_value(a, left, x) || !document_translation::detail::read_copied_value(b, right, y) || (x.kind != y.kind)) return false;
    if (x.kind == ELiveValueType::string) return a.string_value(left) == b.string_value(right);
    if ((x.kind == ELiveValueType::integer) && (x.metadata.domain != y.metadata.domain))
    {
        const auto& signed_value = (x.metadata.domain == EIntegerDomain::signed_value) ? x : y;
        const auto& unsigned_value = (x.metadata.domain == EIntegerDomain::unsigned_value) ? x : y;
        const std::int64_t value = live_signed_integer_from_bits(signed_value.bits);
        return (value >= 0) && (static_cast<std::uint64_t>(value) == unsigned_value.bits);
    }
    return x.bits == y.bits;
}

//  Use bounded explicit frames for nested array descriptors. Each matching
//  property is compared once; the reverse object pass checks omissions only.
[[nodiscard]] static bool compare_definition(const CDocumentRead& a, const SOccurrence left,
    const CDocumentRead& b, const SOccurrence right, const EDefinitionShape shape, const bool proof,
    const unsigned depth) noexcept
{
    struct SFrame
    {
        SOccurrence left, right, next_left, next_right;
        EDefinitionShape shape;
        bool entered{}, ordered{}, reverse{};
    };
    SFrame frames[document_limits::k_max_recursion_depth]{};
    unsigned count = 1u;
    frames[0] = { left, right, {}, {}, shape };
    while (count)
    {
        auto& frame = frames[count - 1u];
        if (!frame.entered)
        {
            if (depth + count > document_limits::k_max_recursion_depth) return false;
            if (frame.shape == EDefinitionShape::detail)
            {
                if (!proof)
                {
                    for (const char* name : { "size", "alignment" })
                    {
                        const auto x = a.object_child(frame.left, CStringView{ name }), y = b.object_child(frame.right, CStringView{ name });
                        if (static_cast<bool>(x) != static_cast<bool>(y) || (x && !same_scalar(a, x, b, y))) return false;
                    }
                }
                --count;
                continue;
            }
            if (!frame.left || !frame.right)
            {
                if (frame.left || frame.right) return false;
                --count;
                continue;
            }
            if (proof && frame.shape == EDefinitionShape::number) { --count; continue; }
            const auto kind = a.value_kind(frame.left);
            if (kind != b.value_kind(frame.right)) return false;
            if (frame.shape == EDefinitionShape::type_reference) frame.shape = kind == EDocumentValueKind::string ? EDefinitionShape::string : EDefinitionShape::array;
            frame.ordered = frame.shape == EDefinitionShape::members || frame.shape == EDefinitionShape::fields ||
                frame.shape == EDefinitionShape::interface_members || frame.shape == EDefinitionShape::labels;
            if (frame.ordered)
            {
                if (a.child_count(frame.left) != b.child_count(frame.right)) return false;
            }
            else if (kind != EDocumentValueKind::object)
            {
                if (!same_scalar(a, frame.left, b, frame.right)) return false;
                --count;
                continue;
            }
            frame.next_left = a.first_child(frame.left);
            frame.next_right = b.first_child(frame.right);
            frame.entered = true;
        }
        SOccurrence x, y;
        EDefinitionShape next_shape{};
        if (frame.ordered)
        {
            if (!frame.next_left) { --count; continue; }
            x = frame.next_left;
            y = frame.next_right;
            if (!(a.name(x) == b.name(y))) return false;
            frame.next_left = a.next_sibling(x);
            frame.next_right = b.next_sibling(y);
            next_shape = child_shape(frame.shape, a.name(x));
        }
        else
        {
            if (!frame.reverse && !frame.next_left) frame.reverse = true;
            if (frame.reverse && !frame.next_right) { --count; continue; }
            const auto& source = frame.reverse ? b : a;
            const auto item = frame.reverse ? frame.next_right : frame.next_left;
            const auto name = source.name(item);
            if (frame.reverse) frame.next_right = b.next_sibling(item);
            else frame.next_left = a.next_sibling(item);
            if (merge_content(frame.shape, name)) continue;
            const auto other = frame.reverse ? a.object_child(frame.left, name) : b.object_child(frame.right, name);
            if (frame.reverse && other) continue;
            if (!other && frame.shape == EDefinitionShape::interface_member && name == CStringView{ "count" })
            {
                std::uint64_t unsigned_value{};
                std::int64_t signed_value{};
                double floating{};
                if (proof || (source.unsigned_integer_value(item, unsigned_value) && unsigned_value == 1u) ||
                    (source.signed_integer_value(item, signed_value) && signed_value == 1) ||
                    (source.floating_point_value(item, floating) && floating == 1.0)) continue;
                return false;
            }
            const bool optional = (frame.shape == EDefinitionShape::member && name == CStringView{ "offset" }) ||
                (frame.shape == EDefinitionShape::array && (name == CStringView{ "stride" } || name == CStringView{ "alignment" })) ||
                (frame.shape == EDefinitionShape::field && name == CStringView{ "interpretation" });
            if (proof && optional && !other) continue;
            x = frame.reverse ? other : item;
            y = frame.reverse ? item : other;
            next_shape = property(frame.shape, name);
        }
        if (count == document_limits::k_max_recursion_depth) return false;
        frames[count++] = { x, y, {}, {}, next_shape };
    }
    return true;
}

bool admit_types_section(const CDocumentRead& source, const SOccurrence section,
    STypesAdmissionDiagnostic& diagnostic, STypesCatalogue& types) noexcept
{
    diagnostic = {};
    return catalogue(source, section, types.definitions, diagnostic);
}

[[nodiscard]] static bool defining_reference(const EDefinitionShape shape, const CStringView& name) noexcept
{
    return (name == CStringView{ "type" } && (shape == EDefinitionShape::member || shape == EDefinitionShape::field || shape == EDefinitionShape::interface_member)) ||
        (name == CStringView{ "storage" } && (shape == EDefinitionShape::enumeration || shape == EDefinitionShape::bit_structure)) ||
        (name == CStringView{ "target" } && shape == EDefinitionShape::association) || (name == CStringView{ "element" } && shape == EDefinitionShape::array);
}

[[nodiscard]] static bool queue_reference(const CStringView& name, const TPodVector<SAdmittedDefinition>& catalogue,
    TPodVector<std::size_t>& queue, EReason& reason) noexcept
{
    for (unsigned i = 1u; i <= static_cast<unsigned>(EPrimitive::b8); ++i)
    {
        if (name == primitive_name(static_cast<EPrimitive>(i))) return true;
    }
    std::size_t begin{}, end = catalogue.size();
    while (begin < end)
    {
        const std::size_t middle = begin + (end - begin) / 2u;
        if (catalogue[middle].name.relationship(name) < 0) begin = middle + 1u;
        else end = middle;
    }
    if (begin == catalogue.size() || !(catalogue[begin].name == name)) { reason = EReason::unknown_type; return false; }
    for (std::size_t i = 0u; i < queue.size(); ++i) if (queue[i] == begin) return true;
    if (!queue.push_back(begin)) { reason = EReason::allocation_failed; return false; }
    return true;
}

[[nodiscard]] static CNodeKey project_definition(CLiveDocument& output, const CDocumentRead& source,
    const SOccurrence value, EDefinitionShape shape, const TPodVector<SAdmittedDefinition>& catalogue,
    TPodVector<std::size_t>& queue, EReason& reason) noexcept
{
    using document_translation::detail::CDocumentConstruction;
    struct SFrame { SOccurrence next; CNodeKey target; EDefinitionShape shape; };
    SFrame frames[document_limits::k_max_recursion_depth]{};
    const auto root = document_translation::detail::create_value_copy(output, source, value, source.name(value));
    if (!root) { reason = EReason::allocation_failed; return {}; }
    if (shape == EDefinitionShape::type_reference) shape = source.value_kind(value) == EDocumentValueKind::string ? EDefinitionShape::string : EDefinitionShape::array;
    unsigned count = 1u;
    frames[0] = { source.first_child(value), root, shape };
    while (count)
    {
        auto& frame = frames[count - 1u];
        const auto child = frame.next;
        if (!child) { --count; continue; }
        frame.next = source.next_sibling(child);
        const bool ordered = frame.shape == EDefinitionShape::members || frame.shape == EDefinitionShape::fields ||
            frame.shape == EDefinitionShape::interface_members || frame.shape == EDefinitionShape::labels;
        if (!ordered && merge_content(frame.shape, source.name(child))) continue;
        if (defining_reference(frame.shape, source.name(child)) && source.value_kind(child) == EDocumentValueKind::string &&
            !queue_reference(source.string_value(child), catalogue, queue, reason)) return {};
        const auto item = document_translation::detail::create_value_copy(output, source, child, source.name(child));
        if (!item || !CDocumentConstruction::append(output, frame.target, item))
        {
            reason = EReason::allocation_failed;
            return {};
        }
        const auto descendant = source.first_child(child);
        if (!descendant) continue;
        if (count == document_limits::k_max_recursion_depth) { reason = EReason::storage_limit; return {}; }
        auto next_shape = child_shape(frame.shape, source.name(child));
        if (next_shape == EDefinitionShape::type_reference) next_shape = EDefinitionShape::array;
        frames[count++] = { descendant, item, next_shape };
    }
    return root;
}

[[nodiscard]] static bool project_closure(CLiveDocument& output, const CDocumentRead& source,
    const TPodVector<SAdmittedDefinition>& catalogue, const std::size_t root, EReason& reason) noexcept
{
    using document_translation::detail::CDocumentConstruction;
    TPodVector<std::size_t> queue;
    if (!output.initialise() || !queue.push_back(root)) { reason = EReason::allocation_failed; return false; }
    const auto types = output.create_object(CStringView{ "types" });
    if (!types || !CDocumentConstruction::append(output, output.root(), types)) { reason = EReason::allocation_failed; return false; }
    for (std::size_t i = 0u; i < queue.size(); ++i)
    {
        const auto& definition = catalogue[queue[i]];
        const auto group_name = source.name(source.parent(definition.source));
        auto group = output.object_child(types, group_name);
        if (!group)
        {
            group = output.create_object(group_name);
            if (!group || !CDocumentConstruction::append(output, types, group)) { reason = EReason::allocation_failed; return false; }
        }
        const auto projected = project_definition(output, source, definition.source, definition.shape, catalogue, queue, reason);
        if (!projected || !CDocumentConstruction::append(output, group, projected))
        {
            if (reason == EReason::none) reason = EReason::allocation_failed;
            return false;
        }
    }
    return true;
}

[[nodiscard]] static bool prove_definition(const CDocumentRead& a, const TPodVector<SAdmittedDefinition>& first, const std::size_t i,
    const CDocumentRead& b, const TPodVector<SAdmittedDefinition>& second, const std::size_t j, STypesAdmissionDiagnostic& error) noexcept
{
    CLiveDocument left, right;
    EReason reason{};
    if (!project_closure(left, a, first, i, reason) || !project_closure(right, b, second, j, reason))
    {
        error.reason = reason;
        error.unestablished = true;
        return false;
    }
    CResolvedTypes left_types, right_types;
    SDiagnostic diagnostic;
    if (!left_types.resolve(left, diagnostic) || !right_types.resolve(right, diagnostic))
    {
        error.reason = diagnostic.reason;
        error.unestablished = true;
        return false;
    }
    CDeclarationCompatibility proof{ left_types, right_types, EDeclarationMatch::compatibility };
    const auto result = proof.compare(left_types.find_declaration(first[i].name), right_types.find_declaration(second[j].name));
    error.reason = result == ETypeMatchResult::allocation_failed ? EReason::allocation_failed : EReason::invalid_input;
    return result == ETypeMatchResult::match;
}

bool compatible_type_sections(const CDocumentRead& first, const STypesCatalogue& first_types,
    const CDocumentRead& second, const STypesCatalogue& second_types, STypesAdmissionDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const auto& a = first_types.definitions;
    const auto& b = second_types.definitions;
    std::size_t i = 0u, j = 0u;
    while ((i < a.size()) && (j < b.size()))
    {
        const auto order = a[i].name.relationship(b[j].name);
        if (order < 0) { ++i; continue; }
        if (order > 0) { ++j; continue; }
        if (a[i].shape != b[j].shape)
        {
            return fail(diagnostic, EReason::invalid_input, a[i].source, b[j].source);
        }
        if (!compare_definition(first, a[i].source, second, b[j].source, a[i].shape, false, 3u))
        {
            diagnostic.first = a[i].source;
            diagnostic.second = b[j].source;
            if (!compare_definition(first, a[i].source, second, b[j].source, a[i].shape, true, 3u))
            {
                diagnostic.reason = EReason::invalid_input;
                return false;
            }
            if (!prove_definition(first, a, i, second, b, j, diagnostic)) return false;
            diagnostic = {};
        }
        ++i;
        ++j;
    }
    return true;
}

}   // namespace schema::detail

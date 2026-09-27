
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_declarations.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    26 Sep 26
//
//  Emit C++17 data declarations from an immutable resolved schema.

#include "schema/resolved_schema.hpp"

#include <charconv>
#include <cstring>
#include <utility>

namespace schema
{

bool valid_identifier(const CStringView& name) noexcept;

namespace declaration_writer
{

static constexpr unsigned k_max_dependency_depth = 256u;
static constexpr std::uint64_t k_decimal_limit = 65535u;

class CGenerator
{
public:
    CGenerator(const CResolvedSchema& schema, const CStringView& namespace_name, SDiagnostic& error) noexcept;
    bool run(CByteBuffer& output) noexcept;

private:
    //  Checked output and declaration traversal.
    bool fail(const EReason reason) noexcept;
    bool text(const CStringView& value) noexcept;
    bool text(const char* const value) noexcept;
    bool number(const std::uint64_t value) noexcept;
    bool hexadecimal(const std::uint64_t value, const unsigned digits = 0u) noexcept;
    bool literal(const SScalar& value) noexcept;
    bool mask(const std::uint64_t value, const SType& storage) noexcept;
    bool primitive(const EPrimitive primitive) noexcept;
    bool local_type_name(const CStringView& name) noexcept;
    bool type_name(const CSchemaIndex type_index) noexcept;
    bool extents(const CSchemaIndex type_index) noexcept;
    bool dependency(const CSchemaIndex type_index, const unsigned depth) noexcept;
    bool definition(const std::uint32_t definition_ordinal, const unsigned depth) noexcept;

    //  Borrowed input and per-run output state.
    const CResolvedSchema& m_schema;
    CStringView m_namespace;
    SDiagnostic& m_diagnostic;
    CBakedValueIndex m_current_type, m_current_member;
    TPodVector<char> m_bytes;
    TPodVector<std::uint8_t> m_emitted;
    bool m_global_std_shadow{};
    CSchemaIndex m_enclosing_structure;
};

CGenerator::CGenerator(const CResolvedSchema& schema, const CStringView& namespace_name, SDiagnostic& error) noexcept
    : m_schema(schema), m_namespace(namespace_name), m_diagnostic(error)
{
}

bool CGenerator::run(CByteBuffer& output) noexcept
{
    if (!m_schema.is_ready())
    {
        return fail(EReason::invalid_input);
    }
    if (!valid_identifier(m_namespace) || (m_namespace.string()[0] == '_') || (m_namespace == CStringView{ "std" }) ||
        (m_namespace == CStringView{ "fp16data_t" }))
    {
        return fail(EReason::invalid_identifier);
    }
    if (m_schema.definition_count() > SIZE_MAX / sizeof(std::uint8_t))
    {
        return fail(EReason::storage_limit);
    }
    for (std::uint32_t definition_ordinal = 0u; definition_ordinal < m_schema.definition_count(); ++definition_ordinal)
    {
        if (!m_emitted.push_back(std::uint8_t{}))
        {
            return fail(EReason::allocation_failed);
        }
    }
    m_global_std_shadow = m_schema.find_type(CStringView{ "std" }).is_valid();
    const bool mixed_namespace = m_schema.definition_count() != 0u;
    if (!text("#pragma once\n#include <cstdint>\n#include \"types/fp16data_t.hpp\"\n\nnamespace ") ||
        !text(m_namespace) || !text("\n{\n"))
    {
        return false;
    }
    if (mixed_namespace && !text("\n"))
    {
        return false;
    }
    if (!text("using b8 = ") || !primitive(EPrimitive::i8) || !text(";\n"))
    {
        return false;
    }
    for (std::uint32_t definition_ordinal = 0u; definition_ordinal < m_schema.definition_count(); ++definition_ordinal)
    {
        if (!definition(definition_ordinal, 0u))
        {
            return false;
        }
    }
    if (mixed_namespace && !text("\n"))
    {
        return false;
    }
    if (!text("}\n") || !m_bytes.push_back(char{}))
    {
        return fail(EReason::allocation_failed);
    }
    if (!output.resize(m_bytes.size(), 1u))
    {
        return fail(EReason::allocation_failed);
    }
    std::memcpy(output.data(), m_bytes.data(), m_bytes.size());
    return true;
}

bool CGenerator::fail(const EReason reason) noexcept
{
    if (m_diagnostic.reason == EReason::none)
    {
        m_diagnostic.reason = reason;
        m_diagnostic.stage = EStage::generation;
        m_diagnostic.occurrence = m_current_member ? m_current_member : m_current_type;
        m_diagnostic.enclosing_type = m_current_type;
        m_diagnostic.enclosing_member = m_current_member;
    }
    return false;
}

bool CGenerator::text(const CStringView& value) noexcept
{
    if (value.length() > (SIZE_MAX - m_bytes.size()))
    {
        return fail(EReason::storage_limit);
    }
    return m_bytes.push_back(reinterpret_cast<const char*>(value.string()), value.length()) || fail(EReason::allocation_failed);
}

bool CGenerator::text(const char* const value) noexcept
{
    return text(CStringView{ value });
}

bool CGenerator::number(const std::uint64_t value) noexcept
{
    if (value > k_decimal_limit)
    {
        return hexadecimal(value);
    }
    char buffer[32];
    const std::to_chars_result converted = std::to_chars(buffer, (buffer + sizeof(buffer)), value);
    return text(CStringView{ buffer, static_cast<std::size_t>(converted.ptr - buffer) });
}

bool CGenerator::hexadecimal(const std::uint64_t value, const unsigned digits) noexcept
{
    char buffer[16];
    const std::to_chars_result converted = std::to_chars(buffer, (buffer + sizeof(buffer)), value, 16);
    const std::size_t length = static_cast<std::size_t>(converted.ptr - buffer);
    if (!text("0x"))
    {
        return false;
    }
    for (std::size_t position = length; position < digits; ++position)
    {
        if (!text("0"))
        {
            return false;
        }
    }
    return text(CStringView{ buffer, length });
}

bool CGenerator::literal(const SScalar& value) noexcept
{
    if (value.kind == EScalar::signed_integer)
    {
        if (value.value.signed_value == INT64_MIN)
        {
            return text("(-0x7fffffffffffffff - 1)");
        }
        if (value.value.signed_value < 0)
        {
            const std::uint64_t magnitude = static_cast<std::uint64_t>(-value.value.signed_value);
            if (!text("-") || !number(magnitude))
            {
                return false;
            }
            //  Unsuffixed hex in this range selects unsigned int on our targets.
            //  Force a signed operand before negation, including the i32 minimum.
            if ((magnitude > INT32_MAX) && (magnitude <= UINT32_MAX))
            {
                return text("ll");
            }
            return true;
        }
        return number(static_cast<std::uint64_t>(value.value.signed_value));
    }
    return number(value.value.unsigned_value) && text("u");
}

bool CGenerator::mask(const std::uint64_t value, const SType& storage) noexcept
{
    const bool signed_storage = (storage.primitive >= EPrimitive::i8) && (storage.primitive <= EPrimitive::i64);
    const std::uint64_t sign_bit = UINT64_C(1) << ((storage.size * 8u) - 1u);
    const bool signed_high_bit = signed_storage && ((value & sign_bit) != 0u);
    if (signed_high_bit && (!text("static_cast<") || !primitive(storage.primitive) || !text(">(")))
    {
        return false;
    }
    if (!hexadecimal(value, static_cast<unsigned>(storage.size * 2u)))
    {
        return false;
    }
    if ((!signed_storage || signed_high_bit) && !text("u"))
    {
        return false;
    }
    return !signed_high_bit || text(")");
}

bool CGenerator::primitive(const EPrimitive primitive) noexcept
{
    const char* const names[] = {
        "", "std::int8_t", "std::int16_t", "std::int32_t", "std::int64_t",
        "std::uint8_t", "std::uint16_t", "std::uint32_t", "std::uint64_t",
        "::fp16data_t", "float", "double" };
    if (primitive == EPrimitive::b8)
    {
        return local_type_name(CStringView{ "b8" });
    }
    if (m_global_std_shadow && (primitive >= EPrimitive::i8) && (primitive <= EPrimitive::u64) && !text("::"))
    {
        return false;
    }
    return text(names[static_cast<unsigned>(primitive)]);
}

bool CGenerator::local_type_name(const CStringView& name) noexcept
{
    //  Consider the complete class scope: a homonymous member can change the
    //  meaning of an earlier type use as well as hide the name in later uses.
    if (m_enclosing_structure && m_schema.find_member(m_enclosing_structure, name))
    {
        return text("::") && text(m_namespace) && text("::") && text(name);
    }
    return text(name);
}

bool CGenerator::type_name(const CSchemaIndex type_index) noexcept
{
    SType type_info;
    if (!m_schema.type(type_index, type_info))
    {
        return fail(EReason::invalid_input);
    }
    if (type_info.category == ECategory::primitive)
    {
        return primitive(type_info.primitive);
    }
    if ((type_info.category == ECategory::array) || (type_info.category == ECategory::bit_structure))
    {
        return type_name(type_info.element_or_storage);
    }
    return local_type_name(m_schema.document()->property_name(type_info.name));
}

bool CGenerator::extents(const CSchemaIndex type_index) noexcept
{
    SType type_info;
    if (!m_schema.type(type_index, type_info))
    {
        return fail(EReason::invalid_input);
    }
    if (type_info.category != ECategory::array)
    {
        return true;
    }
    return text("[") && number(type_info.count) && text("]") && extents(type_info.element_or_storage);
}

bool CGenerator::dependency(const CSchemaIndex type_index, const unsigned depth) noexcept
{
    SType type_info;
    if (!m_schema.type(type_index, type_info))
    {
        return fail(EReason::invalid_input);
    }
    if (depth >= k_max_dependency_depth)
    {
        return fail(EReason::storage_limit);
    }
    if (type_info.category == ECategory::primitive)
    {
        return true;
    }
    if (type_info.category == ECategory::array)
    {
        return dependency(type_info.element_or_storage, (depth + 1u));
    }
    for (std::uint32_t definition_ordinal = 0u; definition_ordinal < m_schema.definition_count(); ++definition_ordinal)
    {
        if (m_schema.definition_at(definition_ordinal) == type_index)
        {
            return definition(definition_ordinal, (depth + 1u));
        }
    }
    return fail(EReason::invalid_input);
}

bool CGenerator::definition(const std::uint32_t definition_ordinal, const unsigned depth) noexcept
{
    if (m_emitted[definition_ordinal])
    {
        return true;
    }
    if (depth >= k_max_dependency_depth)
    {
        return fail(EReason::storage_limit);
    }
    const CSchemaIndex type_index = m_schema.definition_at(definition_ordinal);
    SType type_info;
    if (!m_schema.type(type_index, type_info))
    {
        return fail(EReason::invalid_input);
    }
    m_current_type = type_info.source;
    m_current_member = {};
    if (type_info.category == ECategory::structure)
    {
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type_info.count; ++child_ordinal)
        {
            SMember member;
            if (!m_schema.member(m_schema.member_at(type_index, child_ordinal), member) || !dependency(member.type, (depth + 1u)))
            {
                return false;
            }
        }
    }
    else if (type_info.category == ECategory::bit_structure)
    {
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type_info.count; ++child_ordinal)
        {
            SField field;
            if (!m_schema.field(m_schema.field_at(type_index, child_ordinal), field) || !dependency(field.type, (depth + 1u)))
            {
                return false;
            }
        }
    }
    m_current_type = type_info.source;
    m_current_member = {};
    const CStringView name = m_schema.document()->property_name(type_info.name);
    //  Dependencies have already emitted their groups. Add one shared separator
    //  before this definition; the outer namespace always begins with an alias.
    if (!text("\n"))
    {
        return false;
    }
    if (type_info.category == ECategory::enumeration)
    {
        if (!text("enum class ") || !text(name) || !text(" : ") || !type_name(type_info.element_or_storage) || !text("\n{\n"))
        {
            return false;
        }
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type_info.count; ++child_ordinal)
        {
            SLabel label;
            if (!m_schema.label(m_schema.label_at(type_index, child_ordinal), label))
            {
                return fail(EReason::invalid_input);
            }
            if (!text("    ") || !text(m_schema.document()->property_name(label.name)) || !text(" = ") ||
                !literal(label.value) || !text(",\n"))
            {
                return false;
            }
        }
        if (!text("};\n"))
        {
            return false;
        }
    }
    else if (type_info.category == ECategory::bit_structure)
    {
        if (!text("namespace ") || !text(name) || !text("\n{\n"))
        {
            return false;
        }
        SType storage;
        if (!m_schema.type(type_info.element_or_storage, storage))
        {
            return fail(EReason::invalid_input);
        }
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type_info.count; ++child_ordinal)
        {
            SField field;
            if (!m_schema.field(m_schema.field_at(type_index, child_ordinal), field))
            {
                return fail(EReason::invalid_input);
            }
            m_current_member = field.source;
            if (!text("inline constexpr ") || !type_name(type_info.element_or_storage) || !text(" ") ||
                !text(m_schema.document()->property_name(field.name)) || !text(" = ") ||
                !mask(field.mask, storage) || !text(";\n"))
            {
                return false;
            }
        }
        m_current_member = {};
        if (!text("}\n"))
        {
            return false;
        }
    }
    else
    {
        if (!text("struct ") || !text(name) || !text("\n{\n"))
        {
            return false;
        }
        m_enclosing_structure = type_index;
        for (std::uint32_t child_ordinal = 0u; child_ordinal < type_info.count; ++child_ordinal)
        {
            SMember member;
            if (!m_schema.member(m_schema.member_at(type_index, child_ordinal), member))
            {
                return fail(EReason::invalid_input);
            }
            m_current_member = member.source;
            if (!text("    ") || !type_name(member.type) || !text(" ") ||
                !text(m_schema.document()->property_name(member.name)) || !extents(member.type) || !text(";\n"))
            {
                return false;
            }
        }
        m_enclosing_structure = {};
        m_current_member = {};
        if (!text("};\n"))
        {
            return false;
        }
    }
    m_emitted[definition_ordinal] = 1u;
    return true;
}

}   // namespace declaration_writer

bool generate_cpp(const CResolvedSchema& schema, const CStringView& name_space, CByteBuffer& output, SDiagnostic& diagnostic) noexcept
{
    output.deallocate();
    diagnostic = {};
    declaration_writer::CGenerator generator{ schema, name_space, diagnostic };
    if (!generator.run(output))
    {
        output.deallocate();
        return false;
    }
    return true;
}

}   // namespace schema

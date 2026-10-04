
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_output.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    04 Oct 26
//
//  Copy resolved schema definitions with canonical integer presentation.

#include "schema/integer_notation.hpp"
#include "data_model/document_copy.hpp"

#include <algorithm>
#include <utility>

namespace schema
{

//==============================================================================
//  Transient presentation rules
//==============================================================================

struct SIntegerPresentation
{
    CSchemaHandle source;
    EIntegerDomain domain{};
    EIntegerNotation notation{};
};

[[nodiscard]] static std::uint64_t presentation_key(const CSchemaHandle source) noexcept
{
    return detail::SSchemaHandleAccess::occurrence(source).query_value();
}

//  Reuse the document copier, overriding only integer observations. Rules are
//  sorted once; each lookup is logarithmic and no index survives the output call.
class CSchemaOutputQuery : public CSchemaDocumentQuery
{
public:
    CSchemaOutputQuery(const CSchemaDocumentQuery& source, const TPodVector<SIntegerPresentation>& rules) noexcept :
        CSchemaDocumentQuery(source), m_rules(rules) {}

    [[nodiscard]] bool integer_metadata(const CSchemaHandle value, CIntegerMetadata& result) const noexcept;
    [[nodiscard]] bool signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept;
    [[nodiscard]] bool unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept;

private:
    const TPodVector<SIntegerPresentation>& m_rules;
};

bool CSchemaOutputQuery::signed_integer_value(const CSchemaHandle value, std::int64_t& result) const noexcept
{
    if (CSchemaDocumentQuery::signed_integer_value(value, result))
    {
        return true;
    }
    std::uint64_t raw{};
    if (!CSchemaDocumentQuery::unsigned_integer_value(value, raw) || (raw > INT64_MAX))
    {
        return false;
    }
    result = static_cast<std::int64_t>(raw);
    return true;
}

bool CSchemaOutputQuery::unsigned_integer_value(const CSchemaHandle value, std::uint64_t& result) const noexcept
{
    if (CSchemaDocumentQuery::unsigned_integer_value(value, result))
    {
        return true;
    }
    std::int64_t raw{};
    if (!CSchemaDocumentQuery::signed_integer_value(value, raw) || (raw < 0))
    {
        return false;
    }
    result = static_cast<std::uint64_t>(raw);
    return true;
}

bool CSchemaOutputQuery::integer_metadata(const CSchemaHandle value, CIntegerMetadata& result) const noexcept
{
    std::size_t first = 0u, last = m_rules.size();
    const std::uint64_t key = presentation_key(value);
    while (first < last)
    {
        const std::size_t middle = first + ((last - first) / 2u);
        if (presentation_key(m_rules[middle].source) < key)
        {
            first = middle + 1u;
        }
        else
        {
            last = middle;
        }
    }
    if ((first == m_rules.size()) || (m_rules[first].source != value))
    {
        return CSchemaDocumentQuery::integer_metadata(value, result);
    }
    const SIntegerPresentation& rule = m_rules[first];
    if (rule.domain == EIntegerDomain::signed_value)
    {
        std::int64_t raw{};
        if (!signed_integer_value(value, raw))
        {
            return false;
        }
        result = signed_integer_metadata(raw, rule.notation);
    }
    else
    {
        std::uint64_t raw{};
        if (!unsigned_integer_value(value, raw))
        {
            return false;
        }
        result = unsigned_integer_metadata(raw, rule.notation);
    }
    return true;
}

//==============================================================================
//  Schema document output
//==============================================================================

[[nodiscard]] static bool append_layout_detail(CLiveDocument& document, const CNodeKey structure, const SType& layout) noexcept
{
    CNodeKey detail = document.object_child(structure, CStringView{ "detail" });
    if (!detail)
    {
        detail = document.create_object(CStringView{ "detail" });
        if (!detail || !document.append_child(structure, detail).succeeded())
        {
            return false;
        }
    }
    const auto append = [&](const char* const name, const std::uint32_t value) noexcept
    {
        if (document.object_child(detail, CStringView{ name }))
        {
            return true;
        }
        const CNodeKey child = create_structural_integer(document, value, CStringView{ name });
        return child && document.append_child(detail, child).succeeded();
    };
    return append("alignment", static_cast<std::uint32_t>(layout.alignment)) &&
        append("size", static_cast<std::uint32_t>(layout.size));
}

bool CResolvedSchema::prepare_output(CLiveDocument& destination, SDiagnostic& diagnostic) const noexcept
{
    diagnostic = {};
    if (!is_ready() || destination.is_ready())
    {
        diagnostic.reason = EReason::invalid_input;
        return false;
    }
    TPodVector<SIntegerPresentation> rules;
    const auto add = [&](const CSchemaHandle source, const EIntegerDomain domain, const EIntegerNotation notation) noexcept
    {
        if (!source || (m_document.value_kind(source) != EDocumentValueKind::integer))
        {
            return true;
        }
        if (!rules.push_back({ source, domain, notation }))
        {
            diagnostic.reason = EReason::allocation_failed;
            diagnostic.occurrence = source;
            return false;
        }
        return true;
    };
    const auto structural = [&](const CSchemaHandle owner, const char* const name) noexcept
    {
        return add(m_document.object_child(owner, CStringView{ name }),
            EIntegerDomain::unsigned_value, EIntegerNotation::decimal_or_hexadecimal);
    };
    const auto ordinary = [&](const CSchemaHandle source, const EPrimitive primitive) noexcept
    {
        if ((primitive < EPrimitive::i8) || (primitive > EPrimitive::u64))
        {
            return true;
        }
        return add(source, ((primitive <= EPrimitive::i64) ? EIntegerDomain::signed_value : EIntegerDomain::unsigned_value),
            integer_notation(primitive));
    };
    for (std::size_t i = 0u; i < m_types.size(); ++i)
    {
        const STypeRecord& type = m_types[i];
        if (type.category == ECategory::array)
        {
            if (!structural(type.source, "count"))
            {
                return false;
            }
        }
        else if (type.category == ECategory::structure)
        {
            const CSchemaHandle detail = m_document.object_child(type.source, CStringView{ "detail" });
            if (!structural(detail, "size") || !structural(detail, "alignment"))
            {
                return false;
            }
            for (std::uint32_t member = 0u; member < type.count; ++member)
            {
                if (!structural(m_members[type.first + member].source, "offset"))
                {
                    return false;
                }
            }
        }
        else if (type.category == ECategory::enumeration)
        {
            for (std::uint32_t label = 0u; label < type.count; ++label)
            {
                if (!ordinary(m_labels[type.first + label].source, type.primitive))
                {
                    return false;
                }
            }
        }
        else if (type.category == ECategory::bit_structure)
        {
            for (std::uint32_t field = 0u; field < type.count; ++field)
            {
                const CSchemaHandle mask = m_document.object_child(m_fields[type.first + field].source, CStringView{ "mask" });
                if (!add(mask, EIntegerDomain::unsigned_value, mask_notation(type.size)))
                {
                    return false;
                }
            }
        }
    }
    for (std::size_t i = 0u; i < m_defaults.size(); ++i)
    {
        const SDefaultRecord& value = m_defaults[i];
        if (!ordinary(value.source, type_record(value.type)->primitive))
        {
            return false;
        }
    }
    if (rules.size() > 1u)
    {
        std::sort(rules.data(), (rules.data() + rules.size()),
            [](const SIntegerPresentation& a, const SIntegerPresentation& b) noexcept
            {
                return presentation_key(a.source) < presentation_key(b.source);
            });
    }
    CLiveDocument staged;
    if (!staged.initialise() || !staged.set_root_type(ELiveValueType::object))
    {
        diagnostic.reason = EReason::allocation_failed;
        return false;
    }
    const CSchemaOutputQuery source{ m_document, rules };
    const CSchemaHandle types = source.object_child(source.root(), CStringView{ "types" });
    const CNodeKey copied = document_translation::copy_subtree(staged, source, types, CStringView{ "types" });
    if (!copied || !staged.append_child(staged.root(), copied).succeeded())
    {
        diagnostic.reason = EReason::translation_failed;
        diagnostic.occurrence = types;
        return false;
    }

    //  Copying preserves declaration order, so pair the two structure walks.
    //  Avoid a repeated name search across the destination's definition list.
    const CSchemaHandle source_structures = source.object_child(types, CStringView{ "structures" });
    const CNodeKey output_structures = staged.object_child(copied, CStringView{ "structures" });
    CNodeKey output = staged.first_child(output_structures);
    for (CSchemaHandle original = source.first_child(source_structures); original;
        original = source.next_sibling(original), output = staged.next_sibling(output))
    {
        SType layout;
        if (!type(map_occurrence(original), layout) || !append_layout_detail(staged, output, layout))
        {
            diagnostic.reason = EReason::allocation_failed;
            diagnostic.occurrence = original;
            return false;
        }
    }
    destination = std::move(staged);
    return true;
}

}   // namespace schema

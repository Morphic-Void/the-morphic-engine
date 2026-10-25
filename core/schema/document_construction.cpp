
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_construction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Oct 26
//
//  Internal sorted string preparation and admitted subtree construction.

#include "schema/document_construction.hpp"
#include "data_model/document_copy.hpp"

#include <algorithm>

namespace schema::detail
{

using document_translation::detail::CDocumentConstruction;

bool CDocumentStrings::name(const CStringView& text) noexcept
{
    return add(m_names, text);
}

bool CDocumentStrings::string(const CStringView& text) noexcept
{
    return add(m_strings, text);
}

bool CDocumentStrings::collect(const CDocumentRead& source, const SOccurrence value, const unsigned depth) noexcept
{
    if (!value)
    {
        return true;
    }
    if (depth >= document_limits::k_max_recursion_depth)
    {
        return fail(EDocumentBuildReason::storage_limit, value);
    }
    if ((source.is_object_entry(value) && !name(source.name(value))) ||
        ((source.value_kind(value) == EDocumentValueKind::string) && !string(source.string_value(value))))
    {
        return false;
    }
    const auto kind = source.value_kind(value);
    m_node_count += ((kind == EDocumentValueKind::object) || (kind == EDocumentValueKind::array)) ? 2u : 1u;
    for (SOccurrence child = source.first_child(value); child; child = source.next_sibling(child))
    {
        if (!collect(source, child, (depth + 1u)))
        {
            return false;
        }
    }
    return true;
}

bool CDocumentStrings::initialise(CLiveDocument& target, const std::uint64_t structural_nodes) noexcept
{
    constexpr auto limit = memory::t_max_elements<CLiveNode>();
    if ((m_node_count > limit) || (structural_nodes > (limit - m_node_count)))
    {
        return fail(EDocumentBuildReason::storage_limit);
    }
    return target.initialise(static_cast<std::size_t>(m_node_count + structural_nodes)) ||
        fail(EDocumentBuildReason::construction_failed);
}

bool CDocumentStrings::preload(CLiveDocument& target) noexcept
{
    return preload_domain(target, m_names, true) && preload_domain(target, m_strings, false);
}

bool CDocumentStrings::fail(const EDocumentBuildReason reason, const SOccurrence value) noexcept
{
    if (m_error.reason == EDocumentBuildReason::none) m_error = { reason, value };
    return false;
}

bool CDocumentStrings::add(TPodVector<CStringView>& values, const CStringView& text) noexcept
{
    if (values.size() == memory::t_max_elements<CStringView>())
    {
        return fail(EDocumentBuildReason::storage_limit);
    }
    return values.push_back(text) || fail(EDocumentBuildReason::allocation_failed);
}

bool CDocumentStrings::preload_domain(CLiveDocument& target, TPodVector<CStringView>& values, const bool names) noexcept
{
    if (values.size() > 1u)
    {
        std::sort(values.data(), (values.data() + values.size()),
            [](const CStringView& a, const CStringView& b) noexcept { return a.relationship(b) < 0; });
    }
    std::size_t count{}, bytes{};
    for (std::size_t i = 0u; i < values.size(); ++i)
    {
        if (!values[i].length() || (count && (values[i] == values[count - 1u])))
        {
            continue;
        }
        //  CStringBuffer stores a leading and trailing NUL for each string.
        if ((bytes > (memory::k_byte_size_ceiling - 2u)) ||
            (values[i].length() > (memory::k_byte_size_ceiling - bytes - 2u)))
        {
            return fail(EDocumentBuildReason::storage_limit);
        }
        bytes += values[i].length() + 2u;
        values[count++] = values[i];
    }
    if (!CDocumentConstruction::initialise_strings(target, names, count, bytes))
    {
        return fail(EDocumentBuildReason::construction_failed);
    }
    for (std::size_t i = 0u; i < count; ++i)
    {
        const bool imported = names ? CDocumentConstruction::import_name(target, values[i]).is_valid() :
            CDocumentConstruction::import_string(target, values[i]).is_valid();
        if (!imported)
        {
            return fail(EDocumentBuildReason::construction_failed);
        }
    }
    return true;
}

bool copy_document_sections(const SDocumentSection* sections, const std::size_t count,
    CLiveDocument& target, SDocumentBuildDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CDocumentStrings strings;
    for (std::size_t i = 0u; i < count; ++i)
    {
        if (sections[i].section && !strings.collect(*sections[i].source, sections[i].section, 1u))
        {
            diagnostic = strings.diagnostic();
            return false;
        }
    }
    if (!strings.initialise(target, 2u) || !strings.preload(target))
    {
        diagnostic = strings.diagnostic();
        return false;
    }
    for (std::size_t i = 0u; i < count; ++i)
    {
        const auto& input = sections[i];
        if (!input.section) continue;
        const CNodeKey copied = copy_document_subtree(*input.source, input.section, target, 1u, diagnostic);
        if (!copied || !CDocumentConstruction::append(target, target.root(), copied))
        {
            if (diagnostic.reason == EDocumentBuildReason::none) diagnostic.reason = EDocumentBuildReason::construction_failed;
            diagnostic.occurrence = input.section;
            diagnostic.stage = EDocumentBuildStage::copying;
            return false;
        }
    }
    return true;
}

CNodeKey copy_document_subtree(const CDocumentRead& source, const SOccurrence value,
    CLiveDocument& target, const unsigned depth, SDocumentBuildDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    auto create = [&](const SOccurrence item, const CStringView&, const bool) noexcept
    {
        document_translation::detail::SCopiedValue data;
        if (!document_translation::detail::read_copied_value(source, item, data))
        {
            return CNodeKey{};
        }
        if (data.kind == ELiveValueType::string)
        {
            const auto id = CDocumentConstruction::import_string(target, source.string_value(item));
            if (!id)
            {
                return CNodeKey{};
            }
            data.bits = id.query_value();
        }
        const CNodeKey result = CDocumentConstruction::create(target, data.kind, data.bits, data.metadata,
            CDocumentConstruction::import_name(target, source.name(item)), source.is_object_entry(item));
        if (result && (data.kind == ELiveValueType::string) && source.suppresses_newline_escaping(item))
        {
            (void)target.set_newline_escaping_suppressed(result, true);
        }
        return result;
    };
    const CNodeKey result = document_translation::detail::copy_subtree_impl<true>(target, source, value, source.name(value), depth, create, true);
    if (!result) diagnostic = { EDocumentBuildReason::construction_failed, value };
    return result;
}

}   // namespace schema::detail

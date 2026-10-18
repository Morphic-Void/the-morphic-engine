
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_merge.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Original-document merging with private construction and final resolution.

#include "schema/schema_merge.hpp"
#include "schema/type_compatibility.hpp"
#include "data_model/document_copy.hpp"

#include <algorithm>
#include <utility>

namespace schema
{

using document_translation::detail::CDocumentConstruction;
using document_translation::detail::SCopiedValue;

[[nodiscard]] static ESchemaMergeReason resolution_reason(const EReason reason, const ESchemaMergeReason fallback) noexcept
{
    switch (reason)
    {
        case EReason::allocation_failed:
        {
            return ESchemaMergeReason::allocation_failed;
        }
        case EReason::storage_limit:
        {
            return ESchemaMergeReason::storage_limit;
        }
        default:
        {
            return fallback;
        }
    }
}

[[nodiscard]] static bool validate_source(const CSchemaDocumentQuery& source, CResolvedSchema& resolved,
    const bool first, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    if (resolved.resolve(source, diagnostic.validation))
    {
        return true;
    }
    diagnostic.stage = first ? ESchemaMergeStage::first_source_validation : ESchemaMergeStage::second_source_validation;
    diagnostic.reason = resolution_reason(diagnostic.validation.reason, ESchemaMergeReason::invalid_source);
    (first ? diagnostic.first : diagnostic.second) = diagnostic.validation.occurrence;
    return false;
}

[[nodiscard]] static bool compatible(const CSchemaDocumentQuery& first, const CSchemaHandle types,
    const CResolvedSchema& a, const CResolvedSchema& b, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    CTypeCompatibility comparison{ a, b, ETypeMatch::representation };
    for (CSchemaHandle group = first.first_child(types); group; group = first.next_sibling(group))
    {
        for (CSchemaHandle item = first.first_child(group); item; item = first.next_sibling(item))
        {
            const CSchemaIndex other = b.find_type(first.name(item));
            if (!other)
            {
                continue;
            }
            const ETypeMatchResult result = comparison.compare(a.map_occurrence(item), other);
            if (result != ETypeMatchResult::match)
            {
                SType type;
                (void)b.type(other, type);
                diagnostic.stage = ESchemaMergeStage::compatibility;
                diagnostic.reason = (result == ETypeMatchResult::allocation_failed) ? ESchemaMergeReason::allocation_failed :
                    ((result == ETypeMatchResult::mismatch) ? ESchemaMergeReason::incompatible_type : ESchemaMergeReason::invalid_source);
                diagnostic.first = item;
                diagnostic.second = type.source;
                return false;
            }
        }
    }
    return true;
}

//  A source's valid storage representation remains unchanged. Only the winning
//  notation/prefix is selected; neither contributes to mathematical equality.
[[nodiscard]] static bool second_presentation_wins(const CIntegerMetadata a, const CIntegerMetadata b) noexcept
{
    const unsigned ab = live_integer_minimum_binary_digits(a.notation);
    const unsigned bb = live_integer_minimum_binary_digits(b.notation);
    const unsigned ah = (a.notation == EIntegerNotation::decimal_or_hexadecimal) ? 0u : live_integer_minimum_hex_digits(a.notation);
    const unsigned bh = (b.notation == EIntegerNotation::decimal_or_hexadecimal) ? 0u : live_integer_minimum_hex_digits(b.notation);
    const unsigned aw = ab ? ab : (ah * 4u), bw = bb ? bb : (bh * 4u);
    if (aw || bw)
    {
        return (bw > aw) || ((bw == aw) && bb && !ab);
    }
    return (b.notation == EIntegerNotation::decimal_or_hexadecimal) && (a.notation != b.notation);
}

[[nodiscard]] static bool equal_integers(const SCopiedValue& a, const SCopiedValue& b) noexcept
{
    if (a.metadata.domain == b.metadata.domain)
    {
        return a.bits == b.bits;
    }
    const SCopiedValue& signed_value = (a.metadata.domain == EIntegerDomain::signed_value) ? a : b;
    const SCopiedValue& unsigned_value = (a.metadata.domain == EIntegerDomain::unsigned_value) ? a : b;
    const std::int64_t value = live_signed_integer_from_bits(signed_value.bits);
    return (value >= 0) && (static_cast<std::uint64_t>(value) == unsigned_value.bits);
}

class CSchemaMerger
{
public:
    CSchemaMerger(const CSchemaDocumentQuery& first, const CSchemaDocumentQuery& second,
        CLiveDocument& target, SSchemaMergeDiagnostic& diagnostic) noexcept :
        m_first(first), m_second(second), m_target(target), m_diagnostic(diagnostic) {}

    [[nodiscard]] bool prepare(const CSchemaHandle first, const CSchemaHandle second) noexcept;
    [[nodiscard]] CNodeKey merge(const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept;

private:
    struct SChild
    {
        CSchemaHandle value;
        CPropertyNameId name;
        bool consumed{};
    };
    struct SWork
    {
        CSchemaHandle first, second;
        CNodeKey parent;
        unsigned depth{};
    };

    [[nodiscard]] CNodeKey value(const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept;
    [[nodiscard]] bool fail(const ESchemaMergeReason reason, const CSchemaHandle first = {}, const CSchemaHandle second = {}) noexcept;
    [[nodiscard]] bool collect(const CSchemaDocumentQuery& source, const CSchemaHandle value, const bool first,
        const unsigned depth, TPodVector<CStringView>& names, TPodVector<CStringView>& strings) noexcept;
    [[nodiscard]] bool preload(TPodVector<CStringView>& strings, const bool names) noexcept;
    [[nodiscard]] CNodeKey create(const CSchemaDocumentQuery& source, const CSchemaHandle value, const SCopiedValue& data,
        const bool suppress_newlines) noexcept;
    [[nodiscard]] CNodeKey copy(const CSchemaDocumentQuery& source, const CSchemaHandle value, const unsigned depth) noexcept;
    [[nodiscard]] bool objects(const CNodeKey target, const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept;
    [[nodiscard]] bool schedule(const CSchemaHandle first, const CSchemaHandle second, const CNodeKey parent, const unsigned depth) noexcept;

    const CSchemaDocumentQuery& m_first;
    const CSchemaDocumentQuery& m_second;
    CLiveDocument& m_target;
    SSchemaMergeDiagnostic& m_diagnostic;
    TPodVector<SWork> m_work;
};

bool CSchemaMerger::prepare(const CSchemaHandle first, const CSchemaHandle second) noexcept
{
    //  Both vectors die before construction. The document's own string indexes
    //  perform all subsequent lookups; no parallel text-to-ID map is retained.
    TPodVector<CStringView> names, strings;
    return collect(m_first, first, true, 0u, names, strings) &&
        collect(m_second, second, false, 0u, names, strings) &&
        preload(names, true) && preload(strings, false);
}

CNodeKey CSchemaMerger::merge(const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept
{
    const CNodeKey root = value(first, second, depth);
    if (!root)
    {
        return {};
    }
    //  Shared nesting uses an explicit work stack. Keeping the object matching
    //  frames on the C++ stack exceeds Debug stack space near the depth limit.
    while (m_work.size())
    {
        const SWork work = m_work.last();
        (void)m_work.discard_back();
        const CNodeKey child = value(work.first, work.second, work.depth);
        if (!child || !CDocumentConstruction::append(m_target, work.parent, child))
        {
            (void)fail(ESchemaMergeReason::construction_failed);
            return {};
        }
    }
    return root;
}

CNodeKey CSchemaMerger::value(const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept
{
    if (depth >= 256u)
    {
        (void)fail(ESchemaMergeReason::storage_limit, first, second);
        return {};
    }
    if (!first || !second)
    {
        return first ? copy(m_first, first, depth) : copy(m_second, second, depth);
    }
    SCopiedValue a, b;
    if (!document_translation::detail::read_copied_value(m_first, first, a) ||
        !document_translation::detail::read_copied_value(m_second, second, b))
    {
        (void)fail(ESchemaMergeReason::construction_failed, first, second);
        return {};
    }
    bool equal = (a.kind == b.kind) && (m_first.is_object_entry(first) == m_second.is_object_entry(second)) &&
        (m_first.name(first) == m_second.name(second));
    if (equal)
    {
        switch (a.kind)
        {
            case ELiveValueType::integer:
            {
                equal = equal_integers(a, b);
                if (second_presentation_wins(a.metadata, b.metadata))
                {
                    a.metadata.notation = b.metadata.notation;
                    a.metadata.prefix = b.metadata.prefix;
                }
                break;
            }
            case ELiveValueType::string:
            {
                equal = m_first.string_value(first) == m_second.string_value(second);
                break;
            }
            case ELiveValueType::array:
            {
                equal = m_first.child_count(first) == m_second.child_count(second);
                break;
            }
            default:
            {
                equal = a.bits == b.bits;
                break;
            }
        }
    }
    if (!equal)
    {
        (void)fail(ESchemaMergeReason::conflicting_value, first, second);
        return {};
    }

    //  An empty object contributes no fields. Copy directly without building a
    //  matching index, even when the other object is large.
    if (a.kind == ELiveValueType::object)
    {
        if (m_first.child_count(first) == 0u)
        {
            return copy(m_second, second, depth);
        }
        if (m_second.child_count(second) == 0u)
        {
            return copy(m_first, first, depth);
        }
    }
    const CNodeKey result = create(m_first, first, a,
        (m_first.suppresses_newline_escaping(first) && m_second.suppresses_newline_escaping(second)));
    if (!result)
    {
        return {};
    }
    if (a.kind == ELiveValueType::object)
    {
        return objects(result, first, second, depth) ? result : CNodeKey{};
    }
    if (a.kind == ELiveValueType::array)
    {
        const std::size_t begin = m_work.size();
        CSchemaHandle right = m_second.first_child(second);
        for (CSchemaHandle left = m_first.first_child(first); left; left = m_first.next_sibling(left))
        {
            if (!schedule(left, right, result, (depth + 1u)))
            {
                return {};
            }
            right = m_second.next_sibling(right);
        }
        if (m_work.size() > begin) std::reverse((m_work.data() + begin), (m_work.data() + m_work.size()));
    }
    return result;
}

bool CSchemaMerger::fail(const ESchemaMergeReason reason, const CSchemaHandle first, const CSchemaHandle second) noexcept
{
    if (m_diagnostic.reason == ESchemaMergeReason::none)
    {
        m_diagnostic.stage = ESchemaMergeStage::document_merge;
        m_diagnostic.reason = reason;
        m_diagnostic.first = first;
        m_diagnostic.second = second;
    }
    return false;
}

bool CSchemaMerger::collect(const CSchemaDocumentQuery& source, const CSchemaHandle value, const bool first,
    const unsigned depth, TPodVector<CStringView>& names, TPodVector<CStringView>& strings) noexcept
{
    if (depth >= 256u)
    {
        return fail(ESchemaMergeReason::storage_limit, (first ? value : CSchemaHandle{}), (first ? CSchemaHandle{} : value));
    }
    const bool named = source.is_object_entry(value);
    const bool string = source.value_kind(value) == EDocumentValueKind::string;
    if ((named && (names.size() == memory::t_max_elements<CStringView>())) ||
        (string && (strings.size() == memory::t_max_elements<CStringView>())))
    {
        return fail(ESchemaMergeReason::storage_limit);
    }
    if ((named && !names.push_back(source.name(value))) || (string && !strings.push_back(source.string_value(value))))
    {
        return fail(ESchemaMergeReason::allocation_failed);
    }
    for (CSchemaHandle child = source.first_child(value); child; child = source.next_sibling(child))
    {
        if (!collect(source, child, first, (depth + 1u), names, strings))
        {
            return false;
        }
    }
    return true;
}

bool CSchemaMerger::preload(TPodVector<CStringView>& strings, const bool names) noexcept
{
    if (strings.size() > 1u)
    {
        std::sort(strings.data(), (strings.data() + strings.size()),
            [](const CStringView& a, const CStringView& b) noexcept { return a.relationship(b) < 0; });
    }
    for (std::size_t i = 0u; i < strings.size(); ++i)
    {
        if (i && (strings[i] == strings[i - 1u]))
        {
            continue;
        }
        const bool imported = names ? CDocumentConstruction::import_name(m_target, strings[i]).is_valid() :
            CDocumentConstruction::import_string(m_target, strings[i]).is_valid();
        if (!imported)
        {
            return fail(ESchemaMergeReason::construction_failed);
        }
    }
    return true;
}

CNodeKey CSchemaMerger::create(const CSchemaDocumentQuery& source, const CSchemaHandle value, const SCopiedValue& data,
    const bool suppress_newlines) noexcept
{
    std::uint64_t bits = data.bits;
    if (data.kind == ELiveValueType::string)
    {
        const CStringValueId id = CDocumentConstruction::import_string(m_target, source.string_value(value));
        if (!id)
        {
            (void)fail(ESchemaMergeReason::construction_failed);
            return {};
        }
        bits = id.query_value();
    }
    const CNodeKey result = CDocumentConstruction::create(m_target, data.kind, bits, data.metadata,
        CDocumentConstruction::import_name(m_target, source.name(value)), source.is_object_entry(value));
    if (!result || ((data.kind == ELiveValueType::string) &&
        !m_target.set_newline_escaping_suppressed(result, suppress_newlines)))
    {
        (void)fail(ESchemaMergeReason::construction_failed);
        return {};
    }
    return result;
}

CNodeKey CSchemaMerger::copy(const CSchemaDocumentQuery& source, const CSchemaHandle value, const unsigned depth) noexcept
{
    auto create_value = [&](const CSchemaHandle item, const CStringView&, const bool) noexcept
    {
        SCopiedValue data;
        return document_translation::detail::read_copied_value(source, item, data) ?
            create(source, item, data, source.suppresses_newline_escaping(item)) : CNodeKey{};
    };
    const CNodeKey result = document_translation::detail::copy_subtree_impl<true>(
        m_target, source, value, source.name(value), depth, create_value, true);
    if (!result) (void)fail(ESchemaMergeReason::construction_failed);
    return result;
}

bool CSchemaMerger::objects(const CNodeKey target, const CSchemaHandle first, const CSchemaHandle second, const unsigned depth) noexcept
{
    const std::size_t begin = m_work.size();
    constexpr std::uint32_t small_count = 8u;
    const std::uint32_t count = m_second.child_count(second);
    const bool small = (count <= small_count) && (m_first.child_count(first) <= small_count);
    SChild local[small_count];
    TPodVector<SChild> storage;
    if (count > memory::t_max_elements<SChild>())
    {
        return fail(ESchemaMergeReason::storage_limit);
    }
    if (!small && !storage.resize(count))
    {
        return fail(ESchemaMergeReason::allocation_failed);
    }
    SChild* const children = small ? local : storage.data();
    std::uint32_t ordinal = 0u;
    for (CSchemaHandle child = m_second.first_child(second); child; child = m_second.next_sibling(child))
    {
        children[ordinal++] = { child, m_second.name_id(child), false };
    }
    if (!small && (count > 1u))
    {
        std::sort(children, (children + count), [](const SChild& a, const SChild& b) noexcept
        {
            return a.name.query_value() < b.name.query_value();
        });
    }
    const auto find = [&](const CPropertyNameId name) noexcept -> SChild*
    {
        if (small)
        {
            for (std::uint32_t i = 0u; i < count; ++i)
            {
                if (children[i].name == name)
                {
                    return children + i;
                }
            }
            return nullptr;
        }
        std::uint32_t low = 0u, high = count;
        while (low < high)
        {
            const std::uint32_t middle = low + ((high - low) / 2u);
            if (children[middle].name.query_value() < name.query_value())
            {
                low = middle + 1u;
            }
            else
            {
                high = middle;
            }
        }
        return ((low < count) && (children[low].name == name)) ? children + low : nullptr;
    };
    for (CSchemaHandle child = m_first.first_child(first); child; child = m_first.next_sibling(child))
    {
        SChild* const other = find(m_second.find_property_name_id(m_first.name(child)));
        if (other) other->consumed = true;
        if (!schedule(child, (other ? other->value : CSchemaHandle{}), target, (depth + 1u)))
        {
            return false;
        }
    }

    //  The index is name-sorted; encounter order comes from the original source.
    for (CSchemaHandle child = m_second.first_child(second); child; child = m_second.next_sibling(child))
    {
        const SChild* const item = find(m_second.name_id(child));
        if (item->consumed)
        {
            continue;
        }
        if (!schedule({}, child, target, (depth + 1u)))
        {
            return false;
        }
    }
    if (m_work.size() > begin) std::reverse((m_work.data() + begin), (m_work.data() + m_work.size()));
    return true;
}

bool CSchemaMerger::schedule(const CSchemaHandle first, const CSchemaHandle second, const CNodeKey parent, const unsigned depth) noexcept
{
    if (m_work.size() == memory::t_max_elements<SWork>())
    {
        return fail(ESchemaMergeReason::storage_limit);
    }
    return m_work.push_back({ first, second, parent, depth }) || fail(ESchemaMergeReason::allocation_failed);
}

bool merge_type_schemas(const CSchemaDocumentQuery& first, const CSchemaDocumentQuery& second,
    CLiveDocument& destination, SSchemaMergeDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    CLiveDocument staged;
    {
        CResolvedSchema a, b, result;
        if (!validate_source(first, a, true, diagnostic) || !validate_source(second, b, false, diagnostic))
        {
            return false;
        }
        const CSchemaHandle left = first.object_child(first.root(), CStringView{ "types" });
        const CSchemaHandle right = second.object_child(second.root(), CStringView{ "types" });
        if (!compatible(first, left, a, b, diagnostic))
        {
            return false;
        }
        diagnostic.stage = ESchemaMergeStage::document_merge;
        if (!staged.initialise())
        {
            diagnostic.reason = ESchemaMergeReason::construction_failed;
            return false;
        }
        CSchemaMerger merger{ first, second, staged, diagnostic };
        if (!merger.prepare(left, right))
        {
            return false;
        }
        const CNodeKey types = merger.merge(left, right, 0u);
        if (!types || !CDocumentConstruction::append(staged, staged.root(), types))
        {
            if (diagnostic.reason == ESchemaMergeReason::none) diagnostic.reason = ESchemaMergeReason::construction_failed;
            return false;
        }
        if (!result.resolve(staged, diagnostic.validation))
        {
            diagnostic.stage = ESchemaMergeStage::result_validation;
            diagnostic.reason = resolution_reason(diagnostic.validation.reason, ESchemaMergeReason::invalid_result);
            const EReason reason = diagnostic.validation.reason;
            const EStage stage = diagnostic.validation.stage;
            diagnostic.validation = {};
            diagnostic.validation.reason = reason;
            diagnostic.validation.stage = stage;
            return false;
        }
    }
    destination = std::move(staged);
    diagnostic = {};
    return true;
}

}   // namespace schema

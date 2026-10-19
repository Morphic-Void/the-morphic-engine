
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    document_merge.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Oct 26
//
//  Shared internal document merging and operation-wide string preparation.

#include "schema/document_merge.hpp"
#include "schema/document_construction.hpp"
#include "data_model/document_copy.hpp"

#include <algorithm>

namespace schema::detail
{

using document_translation::detail::CDocumentConstruction;
using document_translation::detail::SCopiedValue;

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

class CDocumentMerger
{
public:
    CDocumentMerger(const CDocumentRead& first, const CDocumentRead& second,
        CLiveDocument& target, SDocumentMergeDiagnostic& diagnostic, const bool bulk_prefix) noexcept :
        m_first(first), m_second(second), m_target(target), m_diagnostic(diagnostic), m_bulk_prefix(bulk_prefix) {}

    [[nodiscard]] bool prepare(const SOccurrence first, const SOccurrence second, const CStringView& section) noexcept;
    [[nodiscard]] CNodeKey merge(const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept;

private:
    struct SChild
    {
        SOccurrence value;
        CPropertyNameId name;
        bool consumed{};
    };
    struct SWork
    {
        SOccurrence first, second;
        CNodeKey parent;
        unsigned depth{};
    };

    [[nodiscard]] CNodeKey value(const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept;
    [[nodiscard]] bool fail(const EDocumentMergeReason reason, const SOccurrence first = {}, const SOccurrence second = {}) noexcept;
    [[nodiscard]] CNodeKey create(const CDocumentRead& source, const SOccurrence value, const SCopiedValue& data, const bool suppress_newlines) noexcept;
    [[nodiscard]] CNodeKey copy(const CDocumentRead& source, const SOccurrence value, const unsigned depth) noexcept;
    [[nodiscard]] bool objects(const CNodeKey target, const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept;
    [[nodiscard]] bool schedule(const SOccurrence first, const SOccurrence second, const CNodeKey parent, const unsigned depth) noexcept;

    const CDocumentRead& m_first;
    const CDocumentRead& m_second;
    CLiveDocument& m_target;
    SDocumentMergeDiagnostic& m_diagnostic;
    const bool m_bulk_prefix;
    TPodVector<SWork> m_work;
};

bool CDocumentMerger::prepare(const SOccurrence first, const SOccurrence second, const CStringView& section) noexcept
{
    CDocumentStrings strings;
    const auto failure = [&](const bool first_source) noexcept
    {
        const auto& error = strings.diagnostic();
        const auto reason = (error.reason == EDocumentBuildReason::allocation_failed) ? EDocumentMergeReason::allocation_failed :
            ((error.reason == EDocumentBuildReason::storage_limit) ? EDocumentMergeReason::storage_limit : EDocumentMergeReason::construction_failed);
        return fail(reason, (first_source ? error.occurrence : SOccurrence{}), (first_source ? SOccurrence{} : error.occurrence));
    };
    if (!strings.name(section) || !strings.collect(m_first, first, 0u))
    {
        return failure(true);
    }
    if (!strings.collect(m_second, second, 0u) || !strings.preload(m_target))
    {
        return failure(false);
    }
    return true;
}

CNodeKey CDocumentMerger::merge(const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept
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
            (void)fail(EDocumentMergeReason::construction_failed);
            return {};
        }
    }
    return root;
}

CNodeKey CDocumentMerger::value(const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept
{
    if (depth >= document_limits::k_max_recursion_depth)
    {
        (void)fail(EDocumentMergeReason::storage_limit, first, second);
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
        (void)fail(EDocumentMergeReason::construction_failed, first, second);
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
                equal = (m_bulk_prefix && (depth == 3u)) ||
                    (m_first.child_count(first) == m_second.child_count(second));
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
        (void)fail(EDocumentMergeReason::conflicting_value, first, second);
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
        SOccurrence right = m_second.first_child(second);
        SOccurrence left = m_first.first_child(first);
        while (left || right)
        {
            if (!schedule(left, right, result, (depth + 1u)))
            {
                return {};
            }
            left = m_first.next_sibling(left);
            right = m_second.next_sibling(right);
        }
        if (m_work.size() > begin) std::reverse((m_work.data() + begin), (m_work.data() + m_work.size()));
    }
    return result;
}

bool CDocumentMerger::fail(const EDocumentMergeReason reason, const SOccurrence first, const SOccurrence second) noexcept
{
    if (m_diagnostic.reason == EDocumentMergeReason::none)
    {
        m_diagnostic.reason = reason;
        m_diagnostic.first = first;
        m_diagnostic.second = second;
    }
    return false;
}

CNodeKey CDocumentMerger::create(const CDocumentRead& source, const SOccurrence value, const SCopiedValue& data, const bool suppress_newlines) noexcept
{
    std::uint64_t bits = data.bits;
    if (data.kind == ELiveValueType::string)
    {
        const CStringValueId id = CDocumentConstruction::import_string(m_target, source.string_value(value));
        if (!id)
        {
            (void)fail(EDocumentMergeReason::construction_failed);
            return {};
        }
        bits = id.query_value();
    }
    const CNodeKey result = CDocumentConstruction::create(m_target, data.kind, bits, data.metadata,
        CDocumentConstruction::import_name(m_target, source.name(value)), source.is_object_entry(value));
    if (!result || ((data.kind == ELiveValueType::string) &&
        !m_target.set_newline_escaping_suppressed(result, suppress_newlines)))
    {
        (void)fail(EDocumentMergeReason::construction_failed);
        return {};
    }
    return result;
}

CNodeKey CDocumentMerger::copy(const CDocumentRead& source, const SOccurrence value, const unsigned depth) noexcept
{
    auto create_value = [&](const SOccurrence item, const CStringView&, const bool) noexcept
    {
        SCopiedValue data;
        return document_translation::detail::read_copied_value(source, item, data) ?
            create(source, item, data, source.suppresses_newline_escaping(item)) : CNodeKey{};
    };
    const CNodeKey result = document_translation::detail::copy_subtree_impl<true>(
        m_target, source, value, source.name(value), depth, create_value, true);
    if (!result) (void)fail(EDocumentMergeReason::construction_failed);
    return result;
}

bool CDocumentMerger::objects(const CNodeKey target, const SOccurrence first, const SOccurrence second, const unsigned depth) noexcept
{
    const std::size_t begin = m_work.size();
    constexpr std::uint32_t small_count = 8u;
    const std::uint32_t count = m_second.child_count(second);
    const bool small = (count <= small_count) && (m_first.child_count(first) <= small_count);
    SChild local[small_count];
    TPodVector<SChild> storage;
    if (count > memory::t_max_elements<SChild>())
    {
        return fail(EDocumentMergeReason::storage_limit);
    }
    if (!small && !storage.resize(count))
    {
        return fail(EDocumentMergeReason::allocation_failed);
    }
    SChild* const children = small ? local : storage.data();
    std::uint32_t ordinal = 0u;
    for (SOccurrence child = m_second.first_child(second); child; child = m_second.next_sibling(child))
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
    for (SOccurrence child = m_first.first_child(first); child; child = m_first.next_sibling(child))
    {
        SChild* const other = find(m_second.find_property_name_id(m_first.name(child)));
        if (other) other->consumed = true;
        if (!schedule(child, (other ? other->value : SOccurrence{}), target, (depth + 1u)))
        {
            return false;
        }
    }

    //  The index is name-sorted; encounter order comes from the original source.
    for (SOccurrence child = m_second.first_child(second); child; child = m_second.next_sibling(child))
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

bool CDocumentMerger::schedule(const SOccurrence first, const SOccurrence second, const CNodeKey parent, const unsigned depth) noexcept
{
    if (m_work.size() == memory::t_max_elements<SWork>())
    {
        return fail(EDocumentMergeReason::storage_limit);
    }
    return m_work.push_back({ first, second, parent, depth }) || fail(EDocumentMergeReason::allocation_failed);
}

bool merge_document_section(const CDocumentRead& first, const CDocumentRead& second,
    const CStringView& section, const bool bulk_prefix, CLiveDocument& target,
    SDocumentMergeDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    if (!target.initialise())
    {
        diagnostic.reason = EDocumentMergeReason::construction_failed;
        return false;
    }
    const SOccurrence left = first.object_child(first.root(), section);
    const SOccurrence right = second.object_child(second.root(), section);
    CDocumentMerger merger{ first, second, target, diagnostic, bulk_prefix };
    if (!merger.prepare(left, right, section))
    {
        return false;
    }
    const CNodeKey result = (left || right) ? merger.merge(left, right, 0u) :
        CDocumentConstruction::create(target, ELiveValueType::object, 0u, {},
            CDocumentConstruction::import_name(target, section), true);
    if (!result || !CDocumentConstruction::append(target, target.root(), result))
    {
        if (diagnostic.reason == EDocumentMergeReason::none) diagnostic.reason = EDocumentMergeReason::construction_failed;
        return false;
    }
    return true;
}

}   // namespace schema::detail

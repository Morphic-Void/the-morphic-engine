
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    reference_construction.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Materialise logical references, retain effective text and patch final bake IDs.

#include "schema/blocks/reference_construction.hpp"
#include "schema/blocks/data_reference_context.hpp"
#include "schema/operations/data_operation_internal.hpp"
#include "data_model/string_value_remap.hpp"
#include "schema/values/value_codec.hpp"
#include "data_model/live_document.hpp"

#include <algorithm>
#include <cstring>

namespace schema::detail
{

bool CReferenceConstruction::add_collection(const CTypesIndex type, const CNodeKey entry,
    const std::uint32_t id, const std::uint32_t count) noexcept
{
    if (!id || (id > INT32_MAX) || !count || (count > memory::k_byte_size_ceiling))
    {
        return m_operation.fail(EDataOperationReason::invalid_reference);
    }
    return m_collections.push_back({ type, entry, id, count }) || m_operation.fail(EDataOperationReason::allocation_failed);
}

bool CReferenceConstruction::intern(const CStringView& text, std::uint32_t& id) noexcept
{
    if (!m_operation.bytes_work(text.length()))
    {
        return false;
    }
    id = 0u;
    if (!text.length()) return true;
    std::size_t begin{}, end = m_strings.size();
    while (begin < end)
    {
        if (!m_operation.work() || !m_operation.bytes_work(text.length()))
        {
            return false;
        }
        const std::size_t middle = begin + ((end - begin) >> 1u);
        const int order = m_document.string_value(m_strings[middle]).relationship(text);
        if (!order)
        {
            id = m_document.string_value_id(m_strings[middle]).query_value();
            return true;
        }
        if (order < 0) begin = middle + 1u;
        else end = middle;
    }
    if (!m_operation.nodes(1u)) return false;
    const CNodeKey string = m_document.create_string(text);
    if (!string)
    {
        return m_operation.fail(EDataOperationReason::allocation_failed);
    }
    id = m_document.string_value_id(string).query_value();
    if (id > INT32_MAX)
    {
        return m_operation.fail(EDataOperationReason::invalid_reference);
    }
    const std::size_t previous = m_by_id.size();
    if (id >= previous)
    {
        if (!m_operation.bytes_work((static_cast<std::uint64_t>(id) + 1u - previous) * sizeof(CNodeKey)) ||
            !m_by_id.resize(static_cast<std::size_t>(id) + 1u))
        {
            return m_operation.fail(EDataOperationReason::allocation_failed);
        }
        for (std::size_t index = previous; index < m_by_id.size(); ++index) m_by_id[index] = {};
    }
    m_by_id[id] = string;
    if (!m_operation.bytes_work((m_strings.size() - begin) * sizeof(CNodeKey)) || !m_strings.insert(begin, string))
    {
        return m_operation.fail(EDataOperationReason::allocation_failed);
    }
    return true;
}

bool CReferenceConstruction::lookup(const std::uint32_t id, CStringView& text) noexcept
{
    if (!id)
    {
        text = CStringView{ "" };
        return true;
    }
    if (!m_operation.work()) return false;
    if ((id < m_by_id.size()) && m_by_id[id])
    {
        text = m_document.string_value(m_by_id[id]);
        return true;
    }
    return m_operation.fail(EDataOperationReason::invalid_reference);
}

bool CReferenceConstruction::encode(void* const context, const SType& type, const SReferenceValue& value,
    std::uint8_t* const destination, EReason& reason) noexcept
{
    CReferenceConstruction& construction = *static_cast<CReferenceConstruction*>(context);
    std::uint32_t id = value.absent ? UINT32_MAX : 0u;
    if (type.category == ECategory::text)
    {
        if (!value.absent && !construction.intern(value.text, id))
        {
            reason = EReason::allocation_failed;
            return false;
        }
        write_scalar_bits(destination, 4u, id);
        return true;
    }
    std::uint32_t first{}, count{};
    if (value.targeted)
    {
        bool found{};
        for (std::size_t index = 0u; index < construction.m_collections.size(); ++index)
        {
            if (!construction.m_operation.work())
            {
                reason = EReason::storage_limit;
                return false;
            }
            const SCollection& collection = construction.m_collections[index];
            if ((collection.type != type.element_or_storage) ||
                !(construction.m_document.name(collection.entry) == value.collection))
            {
                continue;
            }
            if ((static_cast<std::uint64_t>(value.first) + value.count) > collection.count)
            {
                reason = EReason::invalid_range;
                return false;
            }
            found = true;
            if (value.count)
            {
                id = collection.id;
                first = value.first;
                count = value.count;
            }
            break;
        }
        if (!found)
        {
            reason = EReason::invalid_input;
            return false;
        }
    }
    write_scalar_bits(destination, 4u, id);
    write_scalar_bits((destination + 4u), 4u, first);
    write_scalar_bits((destination + 8u), 4u, count);
    return true;
}

bool CReferenceConstruction::finish() noexcept
{
    if (!m_operation.nodes(1u)) return false;
    if (m_document.object_child(m_document.root(), CStringView{ "retainedText" }))
    {
        return m_operation.fail(EDataOperationReason::invalid_input);
    }
    const CNodeKey retained = m_document.create_array(CStringView{ "retainedText" });
    if (!retained || !m_document.append_child(m_document.root(), retained).succeeded())
    {
        return m_operation.fail(EDataOperationReason::allocation_failed);
    }
    for (std::size_t index = 0u; index < m_strings.size(); ++index)
    {
        if (!m_operation.work()) return false;
        if (!m_document.append_child(retained, m_strings[index]).succeeded())
        {
            return m_operation.fail(EDataOperationReason::allocation_failed);
        }
    }
    return true;
}

struct STextRelocation
{
    std::uint8_t* bytes;
    const document_translation::CStringValueRemap& mapping;
    CDataOperation& operation;

    static bool slot(void* const context, const SType& type, const SDataReferenceSlot slot) noexcept
    {
        if (type.category != ECategory::text) return true;
        STextRelocation& relocation = *static_cast<STextRelocation*>(context);
        std::uint8_t* const bytes = relocation.bytes + slot.offset;
        const std::uint32_t old = static_cast<std::uint32_t>(read_scalar_bits(bytes, 4u));
        if (!old || (old == UINT32_MAX))
        {
            return true;
        }
        std::uint32_t target{};
        if (!relocation.mapping.translate_index(old, target) || !target || (target > INT32_MAX))
        {
            return relocation.operation.fail(EDataOperationReason::invalid_reference);
        }
        write_scalar_bits(bytes, 4u, target);
        return true;
    }
};

bool CReferenceConstruction::relocate_text(const CResolvedTypes& types, const CTypesIndex type,
    std::uint8_t* const bytes, const document_translation::CStringValueRemap& mapping) noexcept
{
    STextRelocation relocation{ bytes, mapping, m_operation };
    return visit_reference_slots(types, type, 0u, m_operation, &relocation, &STextRelocation::slot);
}

}   // namespace schema::detail

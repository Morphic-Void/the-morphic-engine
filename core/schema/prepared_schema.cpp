
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    prepared_schema.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Bounded schema conditioning and immutable indexed result construction.

#include "schema/prepared_schema.hpp"
#include "schema/value_work_budget.hpp"
#include "data_model/document_parser.hpp"
#include <algorithm>
#include <cstring>

namespace schema
{

bool CPreparedSchemaView::is_ready() const noexcept
{
    return m_view && (m_view->size == sizeof(SPreparedSchemaV1)) && (m_view->version == 1u) &&
        m_view->types && m_view->types->is_ready() &&
        (!m_view->instance_count || (m_view->instances && m_view->instance_order)) &&
        (!m_view->bulk_count || (m_view->bulk && m_view->bulk_order));
}

const SPreparedInstance* CPreparedSchemaView::instance_at(const std::uint32_t ordinal) const noexcept
{
    return (is_ready() && (ordinal < m_view->instance_count)) ? &m_view->instances[ordinal] : nullptr;
}

const SPreparedBulk* CPreparedSchemaView::bulk_at(const std::uint32_t ordinal) const noexcept
{
    return (is_ready() && (ordinal < m_view->bulk_count)) ? &m_view->bulk[ordinal] : nullptr;
}

template<typename T>
static SPreparedRange range(const T* const entries, const std::uint32_t* const order,
    const std::uint32_t count, const CTypesIndex type) noexcept
{
    std::uint32_t first = 0u, end = count;
    while (first < end)
    {
        const std::uint32_t middle = first + ((end - first) / 2u);
        if (entries[order[middle]].type.query_value() < type.query_value()) first = middle + 1u;
        else end = middle;
    }
    const std::uint32_t begin = first;
    end = count;
    while (first < end)
    {
        const std::uint32_t middle = first + ((end - first) / 2u);
        if (entries[order[middle]].type.query_value() <= type.query_value()) first = middle + 1u;
        else end = middle;
    }
    return { (order ? (order + begin) : nullptr), (first - begin) };
}

SPreparedRange CPreparedSchemaView::instances_of(const CTypesIndex type) const noexcept
{
    return is_ready() ? range(m_view->instances, m_view->instance_order, m_view->instance_count, type) : SPreparedRange{};
}

SPreparedRange CPreparedSchemaView::bulk_of(const CTypesIndex type) const noexcept
{
    return is_ready() ? range(m_view->bulk, m_view->bulk_order, m_view->bulk_count, type) : SPreparedRange{};
}

CPreparedSchema::CPreparedSchema() noexcept : m_allocator{ this, &allocate, &deallocate }, m_memory{ m_allocator } {}
CPreparedSchema::~CPreparedSchema() noexcept { clear(); }

void* MV_STD_ABI_CALL CPreparedSchema::allocate(void* const context, const std::size_t alignment,
    const std::size_t bytes) noexcept
{
    CPreparedSchema& owner = *static_cast<CPreparedSchema*>(context);
    const std::size_t align = std::max(alignment, alignof(SAllocation));
    const std::size_t prefix = (sizeof(SAllocation) + (align - 1u)) & ~(align - 1u);
    if (!owner.m_parent || (bytes > (SIZE_MAX - prefix)))
    {
        return nullptr;
    }
    const std::size_t total = owner.m_parent->condition_bytes(align, bytes + prefix);
    if (!total || (total > (owner.m_limit - owner.m_used)))
    {
        owner.m_limited = true;
        return nullptr;
    }
    void* const base = owner.m_parent->allocate(align, total);
    if (!base) return nullptr;
    std::uint8_t* const result = static_cast<std::uint8_t*>(base) + prefix;
    const SAllocation header{ base, total, align };
    std::memcpy((result - sizeof(header)), &header, sizeof(header));
    owner.m_used += total;
    return result;
}

bool MV_STD_ABI_CALL CPreparedSchema::deallocate(void* const context, const std::size_t alignment,
    void* const pointer) noexcept
{
    (void)alignment;
    CPreparedSchema& owner = *static_cast<CPreparedSchema*>(context);
    SAllocation header;
    std::memcpy(&header, (static_cast<std::uint8_t*>(pointer) - sizeof(header)), sizeof(header));
    owner.m_parent->deallocate(header.alignment, header.bytes, header.base);
    owner.m_used -= header.bytes;
    return true;
}

void CPreparedSchema::clear() noexcept
{
    m_view = {};
    m_instance_views.deallocate();
    m_bulk_views.deallocate();
    m_instance_order.deallocate();
    m_bulk_order.deallocate();
    m_instances.clear();
    m_bulk.clear();
    (void)m_types.clear();
}

bool CPreparedSchema::prepare(const CByteConstView source, const CDocumentParseOptions policy,
    const SPreparationLimits limits, SPreparationDiagnostic& diagnostic) noexcept
{
    diagnostic = {};
    const SPreparationLimits maximum;
    if (m_view.types || m_used || !source.data() || !source.size() || !limits.source_bytes ||
        (limits.source_bytes > maximum.source_bytes) || !limits.storage_bytes ||
        (limits.storage_bytes > maximum.storage_bytes) || !limits.payload_bytes ||
        (limits.payload_bytes > maximum.payload_bytes) || !limits.nodes || (limits.nodes > maximum.nodes) ||
        (policy.allowed_features & document_finding_bit(EDocumentFinding::name_collision_extension)))
    {
        return false;
    }
    if (source.size() > limits.source_bytes)
    {
        diagnostic.status = EPreparationStatus::limit_exceeded;
        return false;
    }
    m_parent = memory::get_ambient_memory_context();
    if (!m_parent) return false;
    m_instance_views.unsafe_replace_memory_context_without_accounting(m_instance_views.memory_attribution().source, &m_memory);
    m_bulk_views.unsafe_replace_memory_context_without_accounting(m_bulk_views.memory_attribution().source, &m_memory);
    m_instance_order.unsafe_replace_memory_context_without_accounting(m_instance_order.memory_attribution().source, &m_memory);
    m_bulk_order.unsafe_replace_memory_context_without_accounting(m_bulk_order.memory_attribution().source, &m_memory);
    m_limit = limits.storage_bytes;
    m_limited = false;
    memory::CMemoryContext* const prior = memory::set_thread_memory_context(&m_memory);
    CValueWorkBudget work_budget{ 4u * 1024u * 1024u };
    const bool success = construct(source, policy, limits, diagnostic, work_budget);
    if (!success)
    {
        clear();
        if (m_limited) diagnostic.status = EPreparationStatus::limit_exceeded;
    }
    else m_view.storage_bytes = m_used;
    (void)memory::set_thread_memory_context(prior);
    return success;
}

bool CPreparedSchema::construct(const CByteConstView source, const CDocumentParseOptions policy,
    const SPreparationLimits limits, SPreparationDiagnostic& diagnostic, CValueWorkBudget& work_budget) noexcept
{
    CLiveDocument document;
    const CDocumentReport parsed = document_parser::parse(source, document, policy);
    if (!parsed.accepted())
    {
        diagnostic.status = EPreparationStatus::invalid_source;
        diagnostic.stage = static_cast<std::uint32_t>(parsed.failure.stage);
        diagnostic.reason = static_cast<std::uint32_t>(parsed.failure.reason);
        if (parsed.failure.location.available)
        {
            diagnostic.line = static_cast<std::uint32_t>(parsed.failure.location.line_1_based);
            diagnostic.column = static_cast<std::uint32_t>(parsed.failure.location.code_point_column_1_based);
        }
        return false;
    }
    const CSchemaDocumentQuery query{ document };
    CSchemaHandle next = query.root();
    std::uint32_t nodes = 0u;
    while (next)
    {
        if (++nodes > limits.nodes)
        {
            diagnostic.status = EPreparationStatus::limit_exceeded;
            return false;
        }
        if (const CSchemaHandle child = query.first_child(next)) next = child;
        else
        {
            while (next && !query.next_sibling(next)) next = query.parent(next);
            if (next) next = query.next_sibling(next);
        }
    }
    CSchema schema;
    SSchemaDiagnostic error;
    diagnostic.status = EPreparationStatus::invalid_schema;
    if (!schema.try_adopt(std::move(document), error) ||
        !schema.materialise(m_types, m_instances, m_bulk, error, work_budget, limits.payload_bytes))
    {
        diagnostic.stage = static_cast<std::uint32_t>(error.stage);
        diagnostic.reason = static_cast<std::uint32_t>(error.reason);
        if (error.reason == ESchemaReason::allocation_failed) diagnostic.status = EPreparationStatus::allocation_failed;
        if (error.reason == ESchemaReason::storage_limit) diagnostic.status = EPreparationStatus::limit_exceeded;
        return false;
    }
    if (!index())
    {
        diagnostic.status = EPreparationStatus::allocation_failed;
        return false;
    }
    m_view.types = m_types.resolved();
    m_view.instances = m_instance_views.data();
    m_view.bulk = m_bulk_views.data();
    m_view.instance_order = m_instance_order.data();
    m_view.bulk_order = m_bulk_order.data();
    m_view.instance_count = static_cast<std::uint32_t>(m_instance_views.size());
    m_view.bulk_count = static_cast<std::uint32_t>(m_bulk_views.size());
    m_view.payload_bytes = m_instances.payload_view().size() + m_bulk.payload_view().size();
    m_view.storage_bytes = m_used;
    diagnostic = { EPreparationStatus::success };
    return true;
}

bool CPreparedSchema::index() noexcept
{
    //  The materialised role records are already in parent-before-child order.
    for (std::uint32_t index = 0u; index < m_instances.m_records.size(); ++index)
    {
        const CLiveInstances::SRecord& record = m_instances.m_records[index];
        const CStringView name = m_instances.m_document.name(record.entry);
        const std::uint8_t* const bytes = record.extent ? (m_instances.m_payload.data() + record.offset) : nullptr;
        if (!m_instance_views.push_back({ name, record.type, record.parent, { bytes, record.extent } }) ||
            !m_instance_order.push_back(index))
        {
            return false;
        }
    }
    for (std::uint32_t index = 0u; index < m_bulk.m_records.size(); ++index)
    {
        const CLiveBulkData::SRecord& record = m_bulk.m_records[index];
        SBulkDataEntryView entry;
        if (!m_bulk.observe(record, entry) ||
            !m_bulk_views.push_back({ m_bulk.m_document.name(record.entry), record.type, entry.count, entry.stride,
                { entry.bytes, static_cast<std::size_t>(entry.byte_count) } }) || !m_bulk_order.push_back(index))
        {
            return false;
        }
    }
    if (m_instance_order.size() > 1u)
    {
        std::sort(m_instance_order.data(), (m_instance_order.data() + m_instance_order.size()),
            [this](const std::uint32_t a, const std::uint32_t b)
            { return m_instance_views[a].type.query_value() < m_instance_views[b].type.query_value(); });
    }
    if (m_bulk_order.size() > 1u)
    {
        std::sort(m_bulk_order.data(), (m_bulk_order.data() + m_bulk_order.size()),
            [this](const std::uint32_t a, const std::uint32_t b)
            { return m_bulk_views[a].type.query_value() < m_bulk_views[b].type.query_value(); });
    }
    return true;
}

}   // namespace schema

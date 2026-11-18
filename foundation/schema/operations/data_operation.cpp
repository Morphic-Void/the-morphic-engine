
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_operation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Operation-wide allocation routing, monotonic admissions and work accounting.

#include "schema/operations/data_operation_internal.hpp"
#include "schema/operations/operation_meter.hpp"
#include "data_model/live_document.hpp"
#include "data_model/baked_document_format.hpp"

#include <algorithm>
#include <cstring>

namespace schema::detail
{

static thread_local CDataOperation* s_operation{};

bool data_active() noexcept { return s_operation != nullptr; }
bool schema_processing() noexcept { return s_operation && s_operation->schema_processing(); }

//  Traverse before calling the generic document analyses: their value_count()
//  and maximum_depth() are complete crawls, not cached constant-time queries.
static bool inspect_live_document(const CLiveDocument& document, const bool baking) noexcept
{
    if (!s_operation) return true;
    if (!document.is_ready())
    {
        return s_operation->fail(EDataOperationReason::invalid_input);
    }
    TPodVector<std::uint8_t> names, strings;
    std::uint64_t output = sizeof(SBakedDocumentHeader) + (2u * sizeof(SBakedStringReference)) + 2u;
    const auto retain = [&](TPodVector<std::uint8_t>& seen, const std::uint32_t id, const CStringView& text) noexcept
    {
        if (!id) return true;
        const std::size_t old = seen.size();
        if (id >= old)
        {
            if (!seen.resize(static_cast<std::size_t>(id) + 1u))
            {
                return s_operation->fail(EDataOperationReason::allocation_failed);
            }
            std::memset((seen.data() + old), 0, (seen.size() - old));
        }
        if (!seen[id])
        {
            seen[id] = 1u;
            output += sizeof(SBakedStringReference) + text.length() + 1u;
        }
        return data_bytes(static_cast<std::uint64_t>(text.length()) << 3u);
    };
    CNodeKey node = document.root();
    std::uint32_t depth{};
    while (node)
    {
        if (!data_depth(depth) || !(baking ? data_work(8u) : data_nodes(1u)))
        {
            return false;
        }
        if (baking)
        {
            output += sizeof(SBakedValueRecord);
            if (!retain(names, document.name_id(node).query_value(), document.name(node)))
            {
                return false;
            }
            if ((document.value_type(node) == ELiveValueType::string) &&
                !retain(strings, document.string_value_id(node).query_value(), document.string_value(node)))
            {
                return false;
            }
            if (!data_output(output)) return false;
        }
        if (const CNodeKey child = document.first_child(node))
        {
            node = child;
            ++depth;
            continue;
        }
        while (!document.next_sibling(node))
        {
            node = document.parent(node);
            if (!node) break;
            --depth;
            if (!data_work()) return false;
        }
        if (node) node = document.next_sibling(node);
    }
    if (baking)
    {
        //  The generic baker also visits detached interned entries while building
        //  its maps. Precharge those passes without retaining their text.
        for (std::uint32_t rank = 1u; document.property_name_id_at_rank(rank); ++rank)
        {
            if (!data_work(8u)) return false;
        }
        for (std::uint32_t rank = 1u; document.string_value_id_at_rank(rank); ++rank)
        {
            if (!data_work(8u)) return false;
        }
        return data_bytes(output << 2u) && data_output(output);
    }
    return data_healthy();
}

bool admit_data_document(const CLiveDocument& document) noexcept { return inspect_live_document(document, false); }
bool prepare_data_document_bake(const CLiveDocument& document) noexcept { return inspect_live_document(document, true); }

bool data_work(const std::uint64_t credits) noexcept { return !s_operation || s_operation->work(credits); }
bool data_bytes(const std::uint64_t bytes) noexcept { return !s_operation || s_operation->bytes_work(bytes); }
bool data_nodes(const std::uint64_t nodes) noexcept { return !s_operation || s_operation->nodes(nodes); }
bool data_depth(const std::uint32_t depth) noexcept { return !s_operation || s_operation->depth(depth); }
bool data_output(const std::uint64_t bytes) noexcept { return !s_operation || s_operation->output(bytes); }
bool data_healthy() noexcept
{
    if (!s_operation) return true;
    SDataOperationDiagnostic error;
    s_operation->diagnose(error);
    return error.reason == EDataOperationReason::none;
}

CDataOperationScope::CDataOperationScope(CDataOperation& operation) noexcept :
    m_prior(memory::set_thread_memory_context(&operation.memory_context())), m_prior_operation(s_operation)
{
    s_operation = &operation;
}

CDataOperationScope::CDataOperationScope(memory::CMemoryContext* const context) noexcept :
    m_prior(memory::set_thread_memory_context(context)), m_prior_operation(s_operation) {}

CDataOperationScope::~CDataOperationScope() noexcept
{
    s_operation = m_prior_operation;
    (void)memory::set_thread_memory_context(m_prior);
}

CDataOperation::CDataOperation(const SDataOperationLimits limits, const bool schema_processing) noexcept :
    m_schema_processing(schema_processing), m_limits(limits), m_work(limits.work_credits), m_parent(memory::get_ambient_memory_context()),
    m_allocator(this, &allocate, &deallocate), m_memory(m_allocator), m_sources(m_memory) {}

bool CDataOperation::valid() const noexcept
{
    return m_parent && (m_limits.inspected_source_bytes <= memory::k_byte_size_ceiling) &&
        (m_limits.owned_bytes <= memory::k_byte_size_ceiling) &&
        (m_limits.result_bytes <= memory::k_byte_size_ceiling) &&
        (m_limits.document_nodes <= memory::k_byte_size_ceiling) &&
        (m_limits.records <= memory::k_byte_size_ceiling) &&
        (m_limits.work_credits != UINT64_MAX) && (m_limits.depth <= document_limits::k_max_recursion_depth);
}

bool CDataOperation::limited(const EDataOperationLimit kind, const std::uint64_t requested,
    const std::uint64_t allowed) noexcept
{
    if (m_error.reason == EDataOperationReason::limit_exceeded) return false;
    m_error.reason = EDataOperationReason::limit_exceeded;
    m_error.limit = kind;
    m_error.requested = requested;
    m_error.allowed = allowed;
    return false;
}

bool CDataOperation::charge(std::uint64_t& used, const std::uint64_t amount,
    const std::uint64_t maximum, const EDataOperationLimit kind) noexcept
{
    if (amount > (maximum - used))
    {
        return limited(kind, ((amount > (UINT64_MAX - used)) ? UINT64_MAX : (used + amount)), maximum);
    }
    used += amount;
    return true;
}

bool CDataOperation::work(const std::uint64_t credits) noexcept
{
    const std::uint64_t used = m_limits.work_credits - m_work.remaining();
    return m_work.try_consume(credits) || limited(EDataOperationLimit::work_credits,
        ((credits > (UINT64_MAX - used)) ? UINT64_MAX : (used + credits)), m_limits.work_credits);
}

bool CDataOperation::bytes_work(const std::uint64_t bytes) noexcept
{
    return work((bytes >> 6u) + ((bytes & 63u) ? 1u : 0u));
}

bool CDataOperation::nodes(const std::uint64_t count) noexcept
{
    return charge(m_nodes, count, m_limits.document_nodes, EDataOperationLimit::document_nodes) && work(count);
}

bool CDataOperation::records(const std::uint64_t count) noexcept
{
    return (count <= m_limits.records) || limited(EDataOperationLimit::records, count, m_limits.records);
}

bool CDataOperation::depth(const std::uint32_t value) noexcept
{
    return (value < m_limits.depth) || limited(EDataOperationLimit::depth,
        (static_cast<std::uint64_t>(value) + 1u), m_limits.depth);
}

bool CDataOperation::source(const void* const identity, const std::uint64_t bytes) noexcept
{
    for (std::size_t index = 0u; index < m_sources.size(); ++index)
    {
        if (!work()) return false;
        if (identity == m_sources[index]) return true;
    }
    if (!charge(m_source_bytes, bytes, m_limits.inspected_source_bytes, EDataOperationLimit::inspected_source_bytes))
    {
        return false;
    }
    return m_sources.push_back(identity) || fail(EDataOperationReason::allocation_failed);
}

bool CDataOperation::output(const std::uint64_t bytes) noexcept
{
    return (bytes <= m_limits.result_bytes) || limited(EDataOperationLimit::result_bytes, bytes, m_limits.result_bytes);
}

bool CDataOperation::reserve_external(const std::uint64_t bytes) noexcept
{
    if (!charge(m_owned, bytes, m_limits.owned_bytes, EDataOperationLimit::owned_bytes))
    {
        return false;
    }
    m_peak = std::max(m_peak, m_owned);
    return true;
}

void CDataOperation::release_external(const std::uint64_t bytes) noexcept
{
    m_owned -= bytes;
}

bool CDataOperation::fail(const EDataOperationReason reason) noexcept
{
    if ((m_error.reason != EDataOperationReason::limit_exceeded) &&
        (m_error.reason != EDataOperationReason::allocation_failed))
    {
        m_error.reason = reason;
    }
    return false;
}

void CDataOperation::diagnose(SDataOperationDiagnostic& diagnostic) const noexcept
{
    diagnostic = m_error;
    if (m_work.rejected() && (diagnostic.reason != EDataOperationReason::limit_exceeded))
    {
        diagnostic.reason = EDataOperationReason::limit_exceeded;
        diagnostic.limit = EDataOperationLimit::work_credits;
        diagnostic.allowed = m_limits.work_credits;
        diagnostic.requested = m_limits.work_credits + 1u;
    }
}

void CDataOperation::location(const CStringView& entry, const std::uint32_t record) noexcept
{
    const std::size_t count = std::min(entry.length(), (sizeof(m_error.entry) - 1u));
    if (count) std::memcpy(m_error.entry, entry.string(), count);
    m_error.entry[count] = '\0';
    m_error.record = record;
    m_error.path[0] = '\0';
}

std::size_t CDataOperation::push_path(const CStringView& component, const std::uint32_t index) noexcept
{
    const std::size_t prior = std::strlen(m_error.path);
    const std::size_t remaining = sizeof(m_error.path) - prior;
    if (index != UINT32_MAX)
    {
        char digits[10];
        std::size_t count{};
        std::uint32_t value = index;
        do
        {
            digits[count++] = static_cast<char>('0' + (value % 10u));
            value /= 10u;
        } while (value);
        std::size_t cursor = prior;
        if (cursor < (sizeof(m_error.path) - 1u)) m_error.path[cursor++] = '[';
        while (count && (cursor < (sizeof(m_error.path) - 1u))) m_error.path[cursor++] = digits[--count];
        if (cursor < (sizeof(m_error.path) - 1u)) m_error.path[cursor++] = ']';
        m_error.path[cursor] = '\0';
    }
    else if (remaining > 1u)
    {
        m_error.path[prior] = '.';
        const std::size_t length = std::min(component.length(), (remaining - 2u));
        if (length) std::memcpy((m_error.path + prior + 1u), component.string(), length);
        m_error.path[prior + length + 1u] = '\0';
    }
    return prior;
}

void* MV_STD_ABI_CALL CDataOperation::allocate(void* const context, const std::size_t alignment,
    const std::size_t bytes) noexcept
{
    CDataOperation& operation = *static_cast<CDataOperation*>(context);
    const std::size_t align = std::max(alignment, alignof(SAllocation));
    const std::size_t prefix = (sizeof(SAllocation) + (align - 1u)) & ~(align - 1u);
    if (!operation.m_parent || (bytes > (SIZE_MAX - prefix)))
    {
        (void)operation.fail(EDataOperationReason::allocation_failed);
        return nullptr;
    }
    const std::size_t total = operation.m_parent->condition_bytes(align, (bytes + prefix));
    if (!total || !operation.reserve_external(total))
    {
        return nullptr;
    }
    void* const base = operation.m_parent->allocate(align, total);
    if (!base)
    {
        operation.release_external(total);
        (void)operation.fail(EDataOperationReason::allocation_failed);
        return nullptr;
    }
    std::uint8_t* const result = static_cast<std::uint8_t*>(base) + prefix;
    const SAllocation header{ base, total, align };
    std::memcpy((result - sizeof(header)), &header, sizeof(header));
    return result;
}

bool MV_STD_ABI_CALL CDataOperation::deallocate(void* const context, const std::size_t alignment,
    void* const pointer) noexcept
{
    (void)alignment;
    CDataOperation& operation = *static_cast<CDataOperation*>(context);
    SAllocation header;
    std::memcpy(&header, (static_cast<std::uint8_t*>(pointer) - sizeof(header)), sizeof(header));
    operation.m_parent->deallocate(header.alignment, header.bytes, header.base);
    operation.release_external(header.bytes);
    return true;
}

}   // namespace schema::detail


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    data_operation_internal.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    17 Nov 26
//
//  Private operation allocation routing, accounting and synchronous scopes.

#pragma once

#include "schema/data_operation.hpp"
#include "schema/value_work_budget.hpp"
#include "containers/TPodVector.hpp"
#include "memory/memory_context.hpp"

#include <cstddef>

namespace schema::detail
{

//  Lives for the complete operation/session. All private owners are constructed
//  under its allocation scope and destroyed before it. Publication allocates in
//  the parent context with an explicit peak-storage reservation.
class CDataOperation
{
public:
    explicit CDataOperation(const SDataOperationLimits limits, const bool schema_processing = false) noexcept;
    [[nodiscard]] bool schema_processing() const noexcept { return m_schema_processing; }
    CDataOperation(const CDataOperation&) = delete;
    CDataOperation& operator=(const CDataOperation&) = delete;

    [[nodiscard]] bool valid() const noexcept;
    [[nodiscard]] bool work(const std::uint64_t credits = 1u) noexcept;
    [[nodiscard]] bool bytes_work(const std::uint64_t bytes) noexcept;
    [[nodiscard]] bool nodes(const std::uint64_t count) noexcept;
    [[nodiscard]] bool records(const std::uint64_t count) noexcept;
    [[nodiscard]] bool depth(const std::uint32_t value) noexcept;
    [[nodiscard]] bool source(const void* const identity, const std::uint64_t bytes) noexcept;
    [[nodiscard]] bool output(const std::uint64_t bytes) noexcept;
    [[nodiscard]] bool reserve_external(const std::uint64_t bytes) noexcept;
    void release_external(const std::uint64_t bytes) noexcept;
    [[nodiscard]] bool fail(const EDataOperationReason reason) noexcept;
    void diagnose(SDataOperationDiagnostic& diagnostic) const noexcept;
    void command() noexcept { m_error = {}; }
    void stage(const EDataOperationStage stage) noexcept { m_error.stage = stage; }
    void location(const CStringView& entry, const std::uint32_t record = 0u) noexcept;
    void source_ordinal(const std::uint32_t source) noexcept { m_error.source = source; }
    void block_diagnostic(const SDataBlockDiagnostic& block) noexcept
    {
        m_error.block = block;
        m_error.block.location = {};
    }
    [[nodiscard]] std::size_t push_path(const CStringView& component, const std::uint32_t index = UINT32_MAX) noexcept;
    void restore_path(const std::size_t length) noexcept { m_error.path[length] = '\0'; }
    [[nodiscard]] memory::CMemoryContext& memory_context() noexcept { return m_memory; }
    [[nodiscard]] memory::CMemoryContext* parent_context() const noexcept { return m_parent; }
    [[nodiscard]] CValueWorkBudget& value_work() noexcept { return m_work; }
    [[nodiscard]] const SDataOperationLimits& limits() const noexcept { return m_limits; }
    [[nodiscard]] std::uint64_t peak_storage() const noexcept { return m_peak; }

private:
    struct SAllocation { void* base; std::size_t bytes, alignment; };
    [[nodiscard]] bool charge(std::uint64_t& used, const std::uint64_t amount,
        const std::uint64_t maximum, const EDataOperationLimit kind) noexcept;
    [[nodiscard]] bool limited(const EDataOperationLimit kind, const std::uint64_t requested,
        const std::uint64_t allowed) noexcept;
    static void* MV_STD_ABI_CALL allocate(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept;
    static bool MV_STD_ABI_CALL deallocate(void* const context, const std::size_t alignment, void* const pointer) noexcept;

    const bool m_schema_processing;
    const SDataOperationLimits m_limits;
    CValueWorkBudget m_work;
    memory::CMemoryContext* const m_parent;
    memory::CMemoryAllocator m_allocator;
    memory::CMemoryContext m_memory;
    std::uint64_t m_owned{}, m_peak{}, m_nodes{}, m_source_bytes{};
    //  Construct against m_memory directly; reverse member destruction releases
    //  source storage while its allocator, context and counters are still alive.
    //  These allocations also count against the owned-storage cap.
    TPodVector<const void*> m_sources;
    SDataOperationDiagnostic m_error;
};

class CDataOperationScope
{
public:
    explicit CDataOperationScope(CDataOperation& operation) noexcept;
    explicit CDataOperationScope(memory::CMemoryContext* const context) noexcept;
    ~CDataOperationScope() noexcept;
    CDataOperationScope(const CDataOperationScope&) = delete;
    CDataOperationScope& operator=(const CDataOperationScope&) = delete;

private:
    memory::CMemoryContext* const m_prior;
    CDataOperation* const m_prior_operation;
};

}   // namespace schema::detail

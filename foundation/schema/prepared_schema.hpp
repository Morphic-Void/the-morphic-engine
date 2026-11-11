
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    prepared_schema.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Immutable prepared schema observations and bounded, worker-owned construction.

#pragma once

#include "schema/schema.hpp"
#include "schema/live_instances.hpp"
#include "schema/live_bulk_data.hpp"
#include "data_model/document_parser.hpp"

namespace schema
{

struct SPreparationLimits
{
    std::uint32_t source_bytes{ 1024u * 1024u };
    std::uint32_t storage_bytes{ 64u * 1024u * 1024u };
    std::uint32_t payload_bytes{ 16u * 1024u * 1024u };
    std::uint32_t nodes{ 262144u };
};

enum class EPreparationStatus : std::uint32_t
{
    success = 0u, invalid_request, invalid_source, invalid_schema, limit_exceeded, allocation_failed
};

//  Values survive failed construction; no diagnostic borrows a discarded tree.
struct SPreparationDiagnostic
{
    EPreparationStatus status{ EPreparationStatus::invalid_request };
    std::uint32_t stage{}, reason{}, line{}, column{};
};

struct SPreparedInstance
{
    CStringView name;
    CTypesIndex type;
    std::uint32_t parent{ UINT32_MAX };
    CByteConstView bytes;
};

struct SPreparedBulk
{
    CStringView name;
    CTypesIndex type;
    std::uint32_t count{};
    std::uint64_t stride{};
    CByteConstView bytes;
};

struct SPreparedRange
{
    const std::uint32_t* ordinals{ nullptr };
    std::uint32_t count{};
};

struct SPreparedSchemaV1
{
    std::uint32_t size{ sizeof(SPreparedSchemaV1) }, version{ 1u };
    const CResolvedTypes* types{ nullptr };
    const SPreparedInstance* instances{ nullptr };
    const SPreparedBulk* bulk{ nullptr };
    const std::uint32_t* instance_order{ nullptr };
    const std::uint32_t* bulk_order{ nullptr };
    std::uint32_t instance_count{}, bulk_count{};
    std::uint64_t payload_bytes{}, storage_bytes{};
};

//  Copying this view never extends its Host acquisition. Queries are allocation
//  free; ordinal access is constant time and type ranges use binary search.
class CPreparedSchemaView
{
public:
    explicit CPreparedSchemaView(const SPreparedSchemaV1* const descriptor = nullptr) noexcept : m_view{ descriptor } {}
    [[nodiscard]] bool is_ready() const noexcept;
    [[nodiscard]] const CResolvedTypes* types() const noexcept { return is_ready() ? m_view->types : nullptr; }
    [[nodiscard]] const SPreparedInstance* instance_at(const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] const SPreparedBulk* bulk_at(const std::uint32_t ordinal) const noexcept;
    [[nodiscard]] SPreparedRange instances_of(const CTypesIndex type) const noexcept;
    [[nodiscard]] SPreparedRange bulk_of(const CTypesIndex type) const noexcept;
    [[nodiscard]] std::uint32_t instance_count() const noexcept { return is_ready() ? m_view->instance_count : 0u; }
    [[nodiscard]] std::uint32_t bulk_count() const noexcept { return is_ready() ? m_view->bulk_count : 0u; }
private:
    const SPreparedSchemaV1* m_view;
};

//  Construct at a stable address. Its private allocator bounds all simultaneous
//  parser, resolver, materialisation and index storage, including scratch.
//  Preparation/clear run on the nominated conditioning worker, never a reader.
class CPreparedSchema
{
public:
    CPreparedSchema() noexcept;
    ~CPreparedSchema() noexcept;
    CPreparedSchema(const CPreparedSchema&) = delete;
    CPreparedSchema& operator=(const CPreparedSchema&) = delete;
    [[nodiscard]] bool prepare(const CByteConstView source, const CDocumentParseOptions policy,
        const SPreparationLimits limits, SPreparationDiagnostic& diagnostic) noexcept;
    void clear() noexcept;
    [[nodiscard]] const SPreparedSchemaV1* descriptor() const noexcept { return m_view.types ? &m_view : nullptr; }
private:
    struct SAllocation { void* base; std::size_t bytes, alignment; };
    static void* MV_STD_ABI_CALL allocate(void* const context, const std::size_t alignment, const std::size_t bytes) noexcept;
    static bool MV_STD_ABI_CALL deallocate(void* const context, const std::size_t alignment, void* const pointer) noexcept;
    [[nodiscard]] bool construct(const CByteConstView source, const CDocumentParseOptions policy,
        const SPreparationLimits limits, SPreparationDiagnostic& diagnostic, class CValueWorkBudget& work_budget) noexcept;
    [[nodiscard]] bool index() noexcept;

    memory::CMemoryContext* m_parent{ nullptr };
    std::size_t m_used{}, m_limit{};
    bool m_limited{ false };
    memory::CMemoryAllocator m_allocator;
    memory::CMemoryContext m_memory;
    CLiveTypes m_types;
    CLiveInstances m_instances;
    CLiveBulkData m_bulk;
    TPodVector<SPreparedInstance> m_instance_views;
    TPodVector<SPreparedBulk> m_bulk_views;
    TPodVector<std::uint32_t> m_instance_order, m_bulk_order;
    SPreparedSchemaV1 m_view;
};

}   // namespace schema

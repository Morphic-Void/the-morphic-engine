
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    schema_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    06 Nov 26
//
//  Nominated Host schema conditioning, with an acquisition-scoped result.

#pragma once

#include "framework/system/system_ids.hpp"
#include "framework/assets/asset_messages.hpp"
#include "foundation/schema/prepared_schema.hpp"

#include "framework/module/host_eligible_budget.hpp"
#include "foundation/schema/data_operation.hpp"

struct SchemaPrepareRequest
{
    CAssetId source;
    schema::SPreparationLimits limits;
    CDocumentParseOptions policy{ document_policy::k_utf8 | document_findings::k_morphic };
};

//  A successful result has already acquired every descriptor reference. End all
//  reads before AssetReleaseRequest; release also retires this temporary result.
struct SchemaPrepareResult
{
    CAssetAcquisitionId acquisition;
    const schema::SPreparedSchemaV1* descriptor{ nullptr };
    schema::SPreparationDiagnostic diagnostic;
    EAssetStatus status{ EAssetStatus::invalid_request };
};

MV_REGISTER_SYSTEM_TYPE(SchemaPrepareRequest, system_type_ids::schema_prepare_request);
MV_REGISTER_SYSTEM_TYPE(SchemaPrepareResult, system_type_ids::schema_prepare_result);

struct SSchemaRequestId { std::uint64_t value{}; };
enum class ESchemaInput : std::uint8_t { none, owned_document, retained_document };

struct SSchemaProcessInput
{
    ESchemaInput kind{};
    CAssetId asset;
    CLiveDocument document;
};

struct SSchemaProcessInputs
{
    SSchemaProcessInput slots[3];

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        memory::SMemoryAttribution total;
        for (const auto& input : slots)
        {
            total = memory::combine_memory_attribution(total, input.document.memory_attribution());
        }
        return total;
    }

    void unsafe_replace_memory_context_without_accounting(memory::CMemoryContext* const source,
        memory::CMemoryContext* const target) noexcept
    {
        for (auto& input : slots)
        {
            input.document.unsafe_replace_memory_context_without_accounting(source, target);
        }
    }
};

struct SchemaProcessRequest
{
    std::uint32_t contract_version{ 1u };
    SSchemaRequestId request_id;
    schema::ESchemaAssembly operation{};
    schema::ESchemaOutput output_kind{};
    std::uint32_t input_count{};
    SSchemaProcessInputs inputs;
    std::int32_t types_source{ -1 }, instances_source{ -1 }, data_source{ -1 };
    schema::SDataOperationLimits limits;
    modules::SEligibleBudgetAnchor deadline; // All zero when absent.
};

struct SchemaProcessAccepted { SSchemaRequestId request_id; std::uint32_t contract_version{ 1u }; };
struct SchemaProcessCancel { SSchemaRequestId request_id; std::uint32_t contract_version{ 1u }; };

enum class ESchemaProcessStatus : std::uint8_t
{
    success, invalid_request, invalid_source, invalid_schema, conflict, limit_exceeded,
    allocation_failed, busy, cancelled, expired, delivery_failed
};

enum class ESchemaProcessStage : std::uint8_t { admission, assembly, encoding, baking, publication };
enum class ESchemaLocation : std::uint8_t { none, line_column, record_member };

// At admission, reason identifies the failed envelope check. During conditioning
// it carries the numeric Foundation ESchemaReason.
enum class ESchemaAdmissionReason : std::uint16_t
{
    none, envelope, version, slot, request_id, assembly_plan, limits, deadline, input, retained_source,
    capacity, stopped, allocation, cancellation, elapsed, transport
};

struct SSchemaProcessFailure
{
    ESchemaProcessStage stage{};
    ESchemaLocation location{};
    std::uint16_t reason{};
    std::int8_t input{ -1 };
    std::uint8_t role{};
    schema::EDataOperationLimit limit{};
    std::uint8_t reserved{};
    std::uint64_t requested{}, allowed{};
    std::uint32_t first{}, second{};
};

struct SSchemaOutputV1
{
    std::uint32_t size{ sizeof(SSchemaOutputV1) };
    std::uint16_t version{ 1u };
    schema::ESchemaOutput kind{};
    std::uint8_t reserved{};
    CByteConstView bytes;
};

struct SSchemaProcessSuccess
{
    CAssetId asset;
    CAssetAcquisitionId acquisition;
    const SSchemaOutputV1* descriptor{};
};

struct SchemaProcessResult
{
    SSchemaRequestId request_id;
    std::uint16_t contract_version{ 1u };
    ESchemaProcessStatus status{ ESchemaProcessStatus::invalid_request };
    schema::ESchemaOutput output_kind{};
    std::uint32_t reserved{};
    union
    {
        SSchemaProcessFailure failure{};
        SSchemaProcessSuccess success;
    };
    constexpr SchemaProcessResult() noexcept : failure{} {}
    constexpr SchemaProcessResult(const SSchemaRequestId id, const schema::ESchemaOutput kind,
        const SSchemaProcessSuccess granted) noexcept : request_id(id), status(ESchemaProcessStatus::success),
        output_kind(kind), success(granted) {}
};

static_assert(std::is_trivially_copyable_v<SchemaProcessAccepted> && std::is_trivially_copyable_v<SchemaProcessCancel>);
static_assert(sizeof(SSchemaRequestId) == 8u);
static_assert(sizeof(SchemaProcessAccepted) <= 16u && sizeof(SchemaProcessCancel) <= 16u);
static_assert(sizeof(SSchemaProcessFailure) == 32u);
static_assert(offsetof(SSchemaProcessFailure, requested) == 8u);
static_assert(offsetof(SSchemaProcessFailure, first) == 24u);
static_assert(offsetof(SchemaProcessResult, failure) == 16u);
static_assert(sizeof(SchemaProcessResult) == 48u);
static_assert(std::is_trivially_copyable_v<SchemaProcessResult>);
static_assert(std::is_standard_layout_v<SSchemaOutputV1>);
static_assert(offsetof(SSchemaOutputV1, bytes) == 8u);
static_assert(sizeof(SSchemaOutputV1) == (8u + sizeof(CByteConstView)));

MV_REGISTER_SYSTEM_TYPE(SchemaProcessRequest, system_type_ids::schema_process_request);
MV_REGISTER_SYSTEM_TYPE(SchemaProcessAccepted, system_type_ids::schema_process_accepted);
MV_REGISTER_SYSTEM_TYPE(SchemaProcessCancel, system_type_ids::schema_process_cancel);
MV_REGISTER_SYSTEM_TYPE(SchemaProcessResult, system_type_ids::schema_process_result);
MV_REGISTER_ERASED_OWNER_PAYLOAD(SchemaProcessRequest);

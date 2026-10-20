
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_payloads.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Host worker exchanges and retained storage; SYSTEM identities until localisation.

#pragma once

#ifndef HOST_PAYLOADS_HPP_INCLUDED
#define HOST_PAYLOADS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "assets/asset_messages.hpp"
#include "filesystem/filesystem_image.hpp"
#include "system/service_messages.hpp"
#include "system/erased_owner_registration.hpp"
#include "system/system_type_registration.hpp"

//==============================================================================
//  File I/O: borrowed Host inputs and worker completions
//  FileLoadResult identifies an owning reply whose payload is LoadedFile.
//==============================================================================

struct FileLoadRequest { const char* file; std::size_t alignment{ 16u }; };
struct FileSaveRequest { const char* file; CByteConstView view; };

struct FileLoadResult {};
struct FileSaveResult { bool success; };
struct LoadedFile { CByteBuffer buffer; };

//==============================================================================
//  Retained document storage
//==============================================================================

struct BakedDocumentAsset { CBakedDocumentBlock block; };
struct LiveDocumentAsset { CLiveDocument document; };

//  The worker borrows a private Host capture; the completion owns full storage.
struct BundleBuildWork { const filesystem_image::CBakeInput* input{ nullptr }; };
struct BundleBuildResult
{
    CBakedDocumentBlock block;
    bundle::SBuildResult result{ bundle::EBuildStatus::allocation_failed };
    bundle::CBundleView view; //  Successful builder validation, backed by block.
};

//  Host-owned, address-stable work storage. Only the conditioning worker may
//  access it between dispatch and completion; no live-image nodes are borrowed.
struct BundlePreparation
{
    CByteBuffer loaded;
    CBakedDocumentBlock block;
    bundle::CBundleView view;
    filesystem_image::CBundleDirectory directory;
};
struct BundlePrepareWork { BundlePreparation* preparation{ nullptr }; };
struct BundlePrepareResult { EAssetStatus status{ EAssetStatus::conditioning_failed }; };

//  Only the Host creates these carriers, with a backing lease on their record.
struct BorrowedBundleAsset { AssetResult view; };

//==============================================================================
//  Document conditioning: borrowed Host inputs and owning worker output
//==============================================================================

enum class EDocumentSource : std::uint8_t { text = 0, baked, live };

union DocumentConditionSource
{
    CByteConstView text;
    CBakedDocument baked;
    const CLiveDocument* live;

    DocumentConditionSource() noexcept : text{} {}
    explicit DocumentConditionSource(const CByteConstView& value) noexcept : text{ value } {}
    explicit DocumentConditionSource(const CBakedDocument& value) noexcept : baked{ value } {}
    explicit DocumentConditionSource(const CLiveDocument* const value) noexcept : live{ value } {}
};

//  The Host owns every object referenced here until the worker completes.
struct DocumentConditionRequest
{
    DocumentConditionSource source;
    const CDocumentWriteOptions* options{ nullptr };
    CDocumentParseOptions policy{};
    EDocumentSource kind{ EDocumentSource::text };
    bool write_json{ false };

    void set_text_source(const CByteConstView& value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::text;
    }

    void set_baked_source(const CBakedDocument& value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::baked;
    }

    void set_live_source(const CLiveDocument* const value) noexcept
    {
        source = DocumentConditionSource{ value };
        kind = EDocumentSource::live;
    }
};

struct DocumentConditionStorage
{
    CBakedDocumentBlock baked;
    CByteBuffer text;

    [[nodiscard]] memory::SMemoryAttribution memory_attribution() const noexcept
    {
        return memory::combine_memory_attribution(baked.memory_attribution(), text.memory_attribution());
    }

    void unsafe_replace_memory_context_without_accounting(
        memory::CMemoryContext* const expected_source, memory::CMemoryContext* const target) noexcept
    {
        baked.unsafe_replace_memory_context_without_accounting(expected_source, target);
        text.unsafe_replace_memory_context_without_accounting(expected_source, target);
    }
};

struct DocumentConditionResult
{
    DocumentConditionStorage storage;
    EAssetStatus status{ EAssetStatus::conditioning_failed };
    EDocumentPolicyStatus policy{ EDocumentPolicyStatus::unexamined };
    std::uint32_t findings{ 0u };
};

//==============================================================================
//  Module worker exchange
//  Only the Host and its worker need the complete job definition.
//==============================================================================

namespace host { struct SModuleWork; }

//  The Host keeps the record and inputs stable until completion. Only the worker
//  mutates the job while it is borrowed; no client supplies this pointer.
struct ModuleWorkRequest { host::SModuleWork* work{ nullptr }; };
struct ModuleWorkResult { EModuleStatus status{ EModuleStatus::invalid_request }; };

struct FilesystemScanRequest { const filesystem_image::SRootScan* root{ nullptr }; };
struct FilesystemScanResult
{
    CLiveDocument document;
    filesystem_image::EScanStatus status{ filesystem_image::EScanStatus::allocation_failed };
};

//  Type bindings precede owning-payload eligibility.
MV_REGISTER_SYSTEM_TYPE(FileLoadRequest, system_type_ids::file_load_request);
MV_REGISTER_SYSTEM_TYPE(FileSaveRequest, system_type_ids::file_save_request);
MV_REGISTER_SYSTEM_TYPE(FileLoadResult, system_type_ids::file_load_result);
MV_REGISTER_SYSTEM_TYPE(FileSaveResult, system_type_ids::file_save_result);
MV_REGISTER_SYSTEM_TYPE(LoadedFile, system_type_ids::loaded_file);
MV_REGISTER_SYSTEM_TYPE(BakedDocumentAsset, system_type_ids::baked_document_asset);
MV_REGISTER_SYSTEM_TYPE(LiveDocumentAsset, system_type_ids::live_document_asset);
MV_REGISTER_SYSTEM_TYPE(BorrowedBundleAsset, system_type_ids::borrowed_bundle_asset);
MV_REGISTER_SYSTEM_TYPE(BundleBuildWork, system_type_ids::bundle_build_work);
MV_REGISTER_SYSTEM_TYPE(BundleBuildResult, system_type_ids::bundle_build_result);
MV_REGISTER_SYSTEM_TYPE(BundlePrepareWork, system_type_ids::bundle_prepare_work);
MV_REGISTER_SYSTEM_TYPE(BundlePrepareResult, system_type_ids::bundle_prepare_result);
MV_REGISTER_SYSTEM_TYPE(DocumentConditionRequest, system_type_ids::document_condition_request);
MV_REGISTER_SYSTEM_TYPE(DocumentConditionResult, system_type_ids::document_condition_result);
MV_REGISTER_SYSTEM_TYPE(ModuleWorkRequest, system_type_ids::module_work_request);
MV_REGISTER_SYSTEM_TYPE(ModuleWorkResult, system_type_ids::module_work_result);
MV_REGISTER_SYSTEM_TYPE(FilesystemScanRequest, system_type_ids::filesystem_scan_request);
MV_REGISTER_SYSTEM_TYPE(FilesystemScanResult, system_type_ids::filesystem_scan_result);

#define MV_ERASED_OWNER_PAYLOAD(type) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "host/runtime/host_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE
#undef MV_ERASED_OWNER_PAYLOAD

#endif  //  HOST_PAYLOADS_HPP_INCLUDED

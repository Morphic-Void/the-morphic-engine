
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_payloads.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    20 Oct 26
//
//  Host-private worker exchanges and retained storage; bindings belong to the component.

#pragma once

#ifndef HOST_PAYLOADS_HPP_INCLUDED
#define HOST_PAYLOADS_HPP_INCLUDED

#include <cstddef>
#include <cstdint>

#include "assets/asset_messages.hpp"
#include "filesystem/filesystem_image.hpp"
#include "system/service_messages.hpp"

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

#endif  //  HOST_PAYLOADS_HPP_INCLUDED

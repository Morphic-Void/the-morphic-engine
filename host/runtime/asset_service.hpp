
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    asset_service.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    19 Sep 26
//
//  Host orchestration of retained assets and temporary load/save operations.

#pragma once

#ifndef HOST_ASSET_SERVICE_HPP_INCLUDED
#define HOST_ASSET_SERVICE_HPP_INCLUDED

#include "assets/asset_repository.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

class CAssetService
{
public:
    void set_filesystem(filesystem_image::CImage& image) noexcept { m_filesystem = &image; }
    [[nodiscard]] bool initialise() noexcept;
    void deallocate() noexcept;

    //  Requires stopped workers: no outstanding borrower may still read inputs.
    void fail_pending() noexcept;
    [[nodiscard]] bool is_idle() const noexcept { return m_operations.is_empty(); }
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

    //  Requires idle operations and quiescent clients; the DLL must remain loaded.
    void dispose_dependencies(const mount_point_ids::id_type mount) noexcept;
    void complete_disposals() noexcept;

    void request(
        threading::CErasedOwnerMsg& message, threading::CThreadPackage& client,
        threading::CThreadPackage& file_io, threading::CThreadPackage& conditioning) noexcept;
    void request_disposal(
        const AssetDisposeRequest& request, const std::int32_t slot,
        threading::CThreadPackage& client) noexcept;
    void complete(threading::CErasedOwnerMsg& message) noexcept;
    void complete(const threading::CErasedPodMsg& message) noexcept;

private:
    enum class EPhase : std::uint8_t
    {
        loading = 0,
        decoding,
        conditioning,
        encoding,
        saving,
        disposing
    };

    struct SOperation
    {
        threading::CThreadPackage* client{ nullptr };
        threading::CThreadPackage* file_io{ nullptr };
        threading::CThreadPackage* conditioning{ nullptr };
        std::int32_t client_slot{ -1 };
        EPhase phase{ EPhase::loading };
        EAssetFileFormat load_format{ EAssetFileFormat::raw };
        EAssetRetention retention{ EAssetRetention::source };
        bool save_requested{ false };
        bool load_requested{ false };

        const char* file{ nullptr };
        const char* logical_file{ nullptr }; //  Backed by request_owner.
        CSimpleString physical_file; //  Stable backing borrowed by the I/O worker.
        std::uint64_t admission_serial{ 0u };
        AssetSaveSettings save_settings; //  Borrowed writer options remain unchanged until completion.

        CErasedOwner request_owner; //  Keeps the borrowed filename alive.
        CErasedOwner candidate_owner; //  Temporary asset; empty after repository retention.
        CErasedOwner conditioning_input_owner; //  File bytes borrowed by decoding/parsing.
        CErasedOwner worker_result_owner; //  Worker output, also backs a pending file save.

        AssetResult working_views; //  May refer to temporary storage; never publish directly.
        CAssetId retained_asset{}; //  Only this identity permits client views.
    };

    //  True means the type matched; working_views.status reports acceptance or rejection.
    template<typename TRequest, typename TAsset>
    [[nodiscard]] bool handle_transfer_if_type_matches(SOperation& operation) noexcept;
    [[nodiscard]] bool retain_candidate(SOperation& operation) noexcept;
    void begin_save_or_bake(const std::int32_t slot) noexcept;
    void begin_file_save(const std::int32_t slot, const CByteConstView& bytes) noexcept;
    void finish_operation(const std::int32_t slot, const EAssetStatus status) noexcept;
    [[nodiscard]] bool disposal_pending(const CAssetId asset) const noexcept;
    [[nodiscard]] bool asset_in_use(const CAssetId asset) const noexcept;

    template<typename TResult>
    void reply(threading::CThreadPackage& client, const std::int32_t slot, const TResult& result) noexcept;

    template<typename TOwner>
    static void describe_views(TOwner& source, AssetResult& result) noexcept;

    void complete_file_load(const std::int32_t slot) noexcept;
    void complete_image_decode(const std::int32_t slot) noexcept;
    void complete_image_encode(const std::int32_t slot) noexcept;
    void complete_document_conditioning(const std::int32_t slot) noexcept;

    CAssetRepository m_assets;
    filesystem_image::CImage* m_filesystem{ nullptr };

    //  Address-stable objects: workers borrow save_settings until completion.
    TUnorderedCollection<SOperation> m_operations;
    bool m_failed{ false };
};

}   //  namespace host

#endif  //  HOST_ASSET_SERVICE_HPP_INCLUDED


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

#include <local_type_ids.hpp>

#include "assets/asset_repository.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "assets/asset_messages.hpp"
#include "runtime/host_payloads.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

class CAssetService
{
public:
    //  Host-thread operation protection, never a client reference. The service
    //  must outlive the guard; workers borrow its immutable bytes, not the guard.
    class CBundleOperation
    {
    public:
        [[nodiscard]] CByteConstView bytes() const noexcept
        {
            return (m_backing.state() != nullptr) ? m_bytes : CByteConstView{};
        }
        void reset() noexcept { m_bytes = {}; m_backing.reset(); }

    private:
        CAssetBackingLease m_backing;
        CByteConstView m_bytes;
        friend class CAssetService;
    };

    //  Internal publication/admission boundary used by Host worker routing.
    //  Publication consumes a reserved binding; failure cancels that reservation
    //  and preserves the caller's block. Success moves it without copying bytes.
    //  The input has completed conditioning; publication never revalidates bytes.
    //  The requester owns management of the original asset and receives final
    //  disposal notification. Other clients may retain views and sub-files.
    [[nodiscard]] AssetResult publish_bundle(BundlePreparation& prepared, const std::uint64_t binding, threading::CThreadPackage& owner) noexcept;
    [[nodiscard]] AssetResult acquire_bundle(const CAssetId source) noexcept;

    //  Consumes a trusted live-image resolution, checking binding identity and
    //  buffer bounds before protecting its bytes. Does not traverse the directory.
    [[nodiscard]] EAssetStatus begin_bundle_file(const filesystem_image::SResolvedSource& source,
        const std::size_t alignment, CBundleOperation& operation) noexcept;
    [[nodiscard]] AssetResult retain_bundle_file(const CBundleOperation& operation,
        const EAssetFileFormat format, const mount_point_ids::id_type dependency = {}) noexcept;

    //  Retire management ownership and final notifications before a package is
    //  destroyed or reused. Its accepted operations must have completed first.
    void detach_client(threading::CThreadPackage& client) noexcept;

    void set_filesystem(filesystem_image::CImage& image) noexcept { m_filesystem = &image; }
    [[nodiscard]] bool initialise() noexcept;

    //  A null conditioning worker routes conditioning to file_io.
    [[nodiscard]] bool configure_workers(threading::CThreadPackage& file_io, threading::CThreadPackage* const conditioning) noexcept;
    void set_rendering(threading::CThreadPackage* const rendering) noexcept { m_rendering = rendering; }
    void deallocate() noexcept;

    //  Requires stopped workers: no outstanding borrower may still read inputs.
    void fail_pending() noexcept;
    [[nodiscard]] bool is_idle() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

    //  Requires idle operations and quiescent clients; the DLL must remain loaded.
    void dispose_dependencies(const mount_point_ids::id_type mount) noexcept;
    void complete_disposals() noexcept;

    void request(threading::CErasedOwnerMsg& message, threading::CThreadPackage& client) noexcept;
    void request_disposal(const AssetDisposeRequest& request, const std::int32_t slot, threading::CThreadPackage& client) noexcept;
    void request_reference(const BundleReferenceRequest& request, const std::int32_t slot, threading::CThreadPackage& client) noexcept;
    void complete(threading::CErasedOwnerMsg& message, threading::CThreadPackage& worker) noexcept;
    void complete(const threading::CErasedPodMsg& message, threading::CThreadPackage& worker) noexcept;

private:
    enum class EPhase : std::uint8_t
    {
        loading = 0,
        decoding,
        conditioning,
        encoding,
        texture_encoding,
        texture_decoding,
        shader_compiling,
        saving,
        disposing,
        bundle_loading,
        bundle_building,
        bundle_preparing
    };

    struct SOperation
    {
        CAssetBackingLease backing;             //  Outlives all borrowed descriptors below.
        CBundleOperation bundle_input;
        filesystem_image::CBakeInput bake_input;
        BundlePreparation bundle_preparation;
        std::uint64_t reserved_binding{ 0u };    //  Roll back on any unpublished completion.
        std::uint64_t source_binding{ 0u };      //  Admission identity for late cache updates.
        threading::CThreadPackage* client{ nullptr };
        std::int32_t client_slot{ -1 };
        EPhase phase{ EPhase::loading };
        EAssetFileFormat load_format{ EAssetFileFormat::raw };
        EAssetRetention retention{ EAssetRetention::source };
        bool save_requested{ false };
        bool load_requested{ false };

        const char* file{ nullptr };
        const char* logical_file{ nullptr };    //  Backed by request_owner; null for native loads.
        CSimpleString physical_file;            //  Stable backing borrowed by the I/O worker.
        std::uint64_t admission_serial{ 0u };
        AssetSaveSettings save_settings;        //  Borrowed writer options remain unchanged until completion.

        CErasedOwner request_owner;             //  Keeps the borrowed filename alive.
        CErasedOwner candidate_owner;           //  Temporary asset; empty after repository retention.
        CErasedOwner conditioning_input_owner;  //  File bytes borrowed by decoding/parsing.
        CErasedOwner worker_result_owner;       //  Worker output, also backs a pending file save.

        AssetResult working_views;  //  May refer to temporary storage; never publish directly.
        CAssetId retained_asset{};  //  Only this identity permits client views.
        CAssetId texture_source{};  //  Borrowed input blocks disposal until rendering completes.
        CByteConstView texture_source_view; //  Stable POD work descriptor target.
        bundle::CBundleView shader_sources; //  Protected by backing through the group response.
    };

    //  True means the type matched; working_views.status reports acceptance or rejection.
    template<typename TRequest, typename TAsset>
    [[nodiscard]] bool handle_transfer_if_type_matches(SOperation& operation) noexcept;
    [[nodiscard]] bool retain_candidate(SOperation& operation) noexcept;
    void begin_save_or_bake(const std::int32_t slot) noexcept;
    void begin_file_save(const std::int32_t slot, const CByteConstView& bytes) noexcept;
    [[nodiscard]] bool post_conditioning(const threading::CErasedPodMsg& message) noexcept;
    void finish_operation(const std::int32_t slot, const EAssetStatus status) noexcept;
    [[nodiscard]] bool disposal_pending(const CAssetId asset) const noexcept;
    [[nodiscard]] bool asset_in_use(const CAssetId asset) const noexcept;

    template<typename TResult>
    void reply(threading::CThreadPackage& client, const std::int32_t slot, const TResult& result) noexcept;

    template<typename TOwner>
    static void describe_views(TOwner& source, AssetResult& result) noexcept;

    template<typename TMessage>
    [[nodiscard]] bool accept_worker_result(SOperation& operation, threading::CErasedOwnerMsg& message,
        threading::CThreadPackage& worker, threading::CThreadPackage* const expected_worker) noexcept;

    void complete_file_load(const std::int32_t slot) noexcept;
    void begin_conditioning_load(const std::int32_t slot, const CByteConstView bytes) noexcept;
    void complete_bundle(const std::int32_t slot) noexcept;
    [[nodiscard]] EAssetStatus reserve_bundle(const CStringView root, std::uint64_t& binding) noexcept;
    void complete_image_decode(const std::int32_t slot) noexcept;
    void complete_image_encode(const std::int32_t slot) noexcept;
    void complete_document_conditioning(const std::int32_t slot) noexcept;
    template<typename TResult>
    void complete_texture_result(const std::int32_t slot) noexcept;

    struct SBundleControl
    {
        SAssetBackingState references;
        CBakedDocumentBlock block;
        bundle::CBundleView view;
        CAssetId original;
        std::uint64_t binding{ 0u };
        threading::CThreadPackage* owner{ nullptr }; //  Instance identity; retired by detach_client.
    };

    [[nodiscard]] SBundleControl* find_bundle(const SAssetBackingState* const state) noexcept;
    [[nodiscard]] AssetResult retain_bundle_view(SBundleControl& bundle, const EAssetBackingRole role,
        const AssetResult& view, const mount_point_ids::id_type dependency = {}) noexcept;
    [[nodiscard]] bool protect_asset(const CAssetRecord& record, CAssetBackingLease& lease, const bool deriving) noexcept;
    void close_bundle(SBundleControl& bundle) noexcept;
    void finalise_bundles() noexcept;

    //  Stable control addresses outlive repository records and accepted work.
    TUnorderedCollection<SBundleControl> m_bundles;
    CAssetRepository m_assets;
    filesystem_image::CImage* m_filesystem{ nullptr };
    threading::CThreadPackage* m_file_io{ nullptr };
    threading::CThreadPackage* m_conditioning{ nullptr };
    threading::CThreadPackage* m_rendering{ nullptr };

    //  Address-stable objects: workers borrow save_settings until completion.
    TUnorderedCollection<SOperation> m_operations;
    bool m_failed{ false };
};

}   //  namespace host

#endif  //  HOST_ASSET_SERVICE_HPP_INCLUDED

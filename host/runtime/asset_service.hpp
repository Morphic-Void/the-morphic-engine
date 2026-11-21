
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

#include "host/assets/asset_repository.hpp"
#include "containers/TUnorderedCollection.hpp"
#include "framework/assets/asset_messages.hpp"
#include "runtime/host_payloads.hpp"
#include "runtime/client_record.hpp"
#include "framework/platform/report_validation.hpp"

namespace host
{

class CPumpedClient;

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
    [[nodiscard]] AssetResult publish_bundle(BundlePreparation& prepared, const std::uint64_t binding, SClientRecord& owner) noexcept;
    [[nodiscard]] AssetResult acquire_bundle(const CAssetId source) noexcept;

    //  Consumes a trusted live-image resolution, checking binding identity and
    //  buffer bounds before protecting its bytes. Does not traverse the directory.
    [[nodiscard]] EAssetStatus begin_bundle_file(const filesystem_image::SResolvedSource& source,
        const std::size_t alignment, CBundleOperation& operation) noexcept;
    [[nodiscard]] AssetResult retain_bundle_file(const CBundleOperation& operation,
        const EAssetFileFormat format, const mount_point_ids::id_type dependency = {}) noexcept;

    //  Retire management ownership and final notifications before a package is
    //  destroyed or reused. Its accepted operations must have completed first.
    void detach_client(SClientRecord& client) noexcept;

    //  Only after terminal state, queue drain and batch quiescence. Idempotent;
    //  retires abandoned borrows before a package address can be reused.
    void release_client_acquisitions(SClientRecord& client) noexcept;

    void set_filesystem(filesystem_image::CImage& image) noexcept { m_filesystem = &image; }
    [[nodiscard]] bool initialise() noexcept;

    //  A null conditioning worker routes conditioning to file_io.
    [[nodiscard]] bool configure_workers(threading::CThreadPackage& file_io, threading::CThreadPackage* const conditioning) noexcept;
    void deallocate() noexcept;

    //  Requires stopped workers: no outstanding borrower may still read inputs.
    void fail_pending() noexcept;
    [[nodiscard]] bool is_idle() const noexcept;

    //  Accepted worker operations have drained. Client acquisitions and pending
    //  disposal may remain; clients must be allowed to exit and release them.
    [[nodiscard]] bool work_idle() const noexcept;
    [[nodiscard]] bool client_idle(const SClientRecord& client) const noexcept;
    [[nodiscard]] bool dependencies_idle(const mount_point_ids::id_type mount) const noexcept;
    [[nodiscard]] bool failed() const noexcept { return m_failed; }

    //  Requires idle operations and quiescent clients; the DLL must remain loaded.
    void dispose_dependencies(const mount_point_ids::id_type mount) noexcept;
    void complete_disposals() noexcept;

    void install_report_provider(SClientRecord& client) noexcept;
    void close_report_provider(SClientRecord& client) noexcept;
    void progress_reports(const bool admission_closed) noexcept;
    void request_report(SClientRecord& client, const std::int32_t slot, const bool update, const bool closed) noexcept;
    void report_candidate(SClientRecord& client, threading::CErasedOwnerMsg& message) noexcept;
    void report_failed(SClientRecord& client, const platform::reports::PlatformReportCaptureFailed& outcome) noexcept;

    void request(threading::CErasedOwnerMsg& message, SClientRecord& client) noexcept;
    void request_disposal(const AssetDisposeRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    void request_reference(const BundleReferenceRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    void request_acquire(const AssetAcquireRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    void request_release(const AssetReleaseRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    void request_schema(const SchemaPrepareRequest& request, const std::int32_t slot, SClientRecord& client) noexcept;
    // Admission consumes two reserved client credits. A separate bounded slot
    // retires a transferred rejection before its terminal response.
    // Host-local posting reserves these credits internally and leaves the owner
    // with its caller if ingress or reply capacity is exhausted.
    [[nodiscard]] bool schema_ingress_available() const noexcept { return !m_processes[4].client; }
    [[nodiscard]] bool post_internal_process(threading::CErasedOwnerMsg& message, SClientRecord& client) noexcept;
    void request_process(threading::CErasedOwnerMsg& message, SClientRecord& client,
        const bool closed, const bool workers_unavailable) noexcept;
    void cancel_process(const SchemaProcessCancel& cancel, SClientRecord& client) noexcept;
    void close_schema_outputs() noexcept;
    void complete(threading::CErasedOwnerMsg& message, threading::CThreadPackage& worker) noexcept;
    void complete(const threading::CErasedPodMsg& message, threading::CThreadPackage& worker) noexcept;

private:
    enum class EProcessPhase { accepting, dispatch, executing, publishing, retiring };
    struct SProcess
    {
        SchemaProcessConditioning work;
        CAssetBackingLease pins[3];
        SClientRecord* client{};
        modules::SEligibleBudgetAnchor deadline;
        modules::CHostEligibleBudget budget{};
        SSchemaRequestId id;
        std::int32_t slot{ -1 }, acquisition{ -1 }, backing{ -1 };
        SchemaProcessResult result;
        bool accepted{};
        EProcessPhase phase{ EProcessPhase::accepting };
        ESchemaProcessStatus outcome{ ESchemaProcessStatus::success };
    };
    struct SOutputBacking
    {
        SAssetBackingState references;
        CBakedDocumentBlock block;
        SSchemaOutputV1 descriptor;
        CAssetId asset;
        SClientRecord* ambiguous_client{};
        bool private_output{ true }, reclaiming{};
    };
    SProcess m_processes[5];
    std::atomic<bool> m_process_stop[5]{};
    TUnorderedCollection<SOutputBacking> m_schema_outputs;
    void progress_processes() noexcept;
    void progress_schema_outputs() noexcept;
    void fail_processes() noexcept;
    void complete_process(const SchemaProcessDone& done, const std::int32_t slot,
        threading::CThreadPackage& worker) noexcept;
    void publish_process(SProcess& process) noexcept;
    void validate_process(SProcess& process, const SchemaProcessRequest& request) noexcept;
    [[nodiscard]] bool observe_process(SProcess& process) noexcept;

    enum class ESchemaPhase { preparing, publishing, acquired, retiring, reclaiming };
    struct SSchemaOperation
    {
        CAssetBackingLease backing;
        CAssetId source;
        SchemaConditioning work;
        SClientRecord* client{ nullptr };
        std::int32_t client_slot{ -1 };
        CAssetAcquisitionId acquisition;
        ESchemaPhase phase{ ESchemaPhase::preparing };
    };
    void progress_schemas() noexcept;
    void complete_schema(const SchemaConditionResult& result, const std::int32_t slot,
        threading::CThreadPackage& worker) noexcept;
    [[nodiscard]] bool release_schema(const CAssetAcquisitionId acquisition, SClientRecord& client) noexcept;
    TUnorderedCollection<SSchemaOperation> m_schemas;

    enum class EPhase : std::uint8_t
    {
        loading = 0,
        decoding,
        conditioning,
        encoding,
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
        SClientRecord* client{ nullptr };
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
    void reply(SClientRecord& client, const std::int32_t slot, const TResult& result) noexcept;

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

    struct SBundleControl
    {
        SAssetBackingState references;
        CBakedDocumentBlock block;
        bundle::CBundleView view;
        CAssetId original;
        std::uint64_t binding{ 0u };
        SClientRecord* owner{ nullptr }; //  Instance identity; retired by detach_client.
        bool disposal_reply_reserved{ false };
    };

    struct SAcquisition
    {
        CAssetBackingLease backing;
        SClientRecord* client{ nullptr };
        CAssetId source{};
        CAssetAcquisitionId identity{};
        bool requires_quiescence{};
    };

    struct SReportBacking
    {
        SAssetBackingState references;
        schema::CBakedDataBlock block;
    };
    struct SReportDelivery
    {
        SClientRecord* client{};
        std::int32_t slot{ -1 }, acquisition_slot{ -1 };
        CAssetAcquisitionId token;
        platform::reports::PlatformReportResult result;
        bool ready{};
    };
    struct SReportCapture
    {
        platform::reports::SCaptureIdentity identity;
        modules::CHostEligibleBudget budget{};
        std::uint64_t copy_ns{}, adoption_ns{}, validation_ns{}, peak_bytes{};
        std::int32_t delivery{ -1 }, backing{ -1 };
        platform::reports::SFailure failure;
        CErasedOwner candidate;
        bool outcome{}, settled{}, cancel_sent{};
        platform::reports::PlatformReportCaptureReceipt receipt;
        bool receipt_settled{ false }, receipt_published{ false };
    };
    [[nodiscard]] platform::reports::PlatformReportResult report_state() const noexcept;
    [[nodiscard]] bool report_matches(const SClientRecord& client, const platform::reports::SCaptureIdentity identity) const noexcept;
    [[nodiscard]] platform::reports::SFailure start_report(const std::int32_t delivery) noexcept;
    [[nodiscard]] bool grant_report(SReportDelivery& delivery, SReportBacking& backing) noexcept;
    void settle_report() noexcept;
    void retire_capture() noexcept;
    void deliver_reports() noexcept;
    SClientRecord* m_report_provider{};
    std::uint64_t m_next_capture{ 1u };
    std::int32_t m_current_report{ -1 };
    bool m_initial_report{}, m_report_closing{};
    platform::reports::ELastUpdate m_report_last{};
    platform::reports::SFailure m_report_failure;
    SReportCapture m_capture;
    TUnorderedCollection<SReportDelivery> m_report_deliveries;
    TUnorderedCollection<SReportBacking> m_report_backings;

    [[nodiscard]] SBundleControl* find_bundle(const SAssetBackingState* const state) noexcept;
    [[nodiscard]] AssetResult retain_bundle_view(SBundleControl& bundle, const EAssetBackingRole role,
        const AssetResult& view, const mount_point_ids::id_type dependency = {}) noexcept;
    [[nodiscard]] bool protect_asset(const CAssetRecord& record, CAssetBackingLease& lease, const bool deriving) noexcept;
    void close_bundle(SBundleControl& bundle) noexcept;
    void finalise_bundles() noexcept;

    //  Stable control addresses outlive repository records and accepted work.
    TUnorderedCollection<SBundleControl> m_bundles;
    CAssetRepository m_assets;
    TUnorderedCollection<SAcquisition> m_acquisitions;
    std::uint64_t m_next_acquisition{ 1u };
    filesystem_image::CImage* m_filesystem{ nullptr };
    threading::CThreadPackage* m_file_io{ nullptr };
    threading::CThreadPackage* m_conditioning{ nullptr };

    //  Address-stable objects: workers borrow save_settings until completion.
    TUnorderedCollection<SOperation> m_operations;
    bool m_failed{ false };
    bool m_workers_stopped{ false };
};

}   //  namespace host

#endif  //  HOST_ASSET_SERVICE_HPP_INCLUDED

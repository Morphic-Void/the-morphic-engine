
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_worker_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Shared Host worker for file I/O, asset conditioning and module binding.

#include "threading/messages/message_helpers.hpp"

#include <local_type_ids.hpp>

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "runtime/host_worker_thread.hpp"
#include "runtime/module_service.hpp"

#include "debug/macros.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "image/codec/tga.hpp"
#include "platform/path/native_path.hpp"
#include "platform/filesystem/file.hpp"
#include "system/erased_owner.hpp"
#include "assets/asset_messages.hpp"
#include "system/service_messages.hpp"
#include "image/image_messages.hpp"
#include "runtime/host_payloads.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

//==============================================================================
//  Asset failure diagnostics
//==============================================================================

namespace asset_diagnostics
{

static const char* failure_reason(const bundle::EBuildStatus status) noexcept
{
    switch (status)
    {
        case bundle::EBuildStatus::success: return "success";
        case bundle::EBuildStatus::invalid_input: return "invalid input";
        case bundle::EBuildStatus::allocation_failed: return "allocation failed";
        case bundle::EBuildStatus::size_query_failed: return "file size query failed";
        case bundle::EBuildStatus::read_failed: return "file read failed";
        case bundle::EBuildStatus::no_content: return "no non-empty files";
        case bundle::EBuildStatus::too_large: return "bundle exceeds allocation limit";
        case bundle::EBuildStatus::bake_failed: return "directory bake failed";
        case bundle::EBuildStatus::validation_failed: return "validation failed";
        default: return "unknown build failure";
    }
}

static const char* failure_reason(const bundle::EValidationStatus status) noexcept
{
    switch (status)
    {
        case bundle::EValidationStatus::success: return "success";
        case bundle::EValidationStatus::invalid_buffer: return "invalid buffer";
        case bundle::EValidationStatus::document_validation_failed: return "document validation failed";
        case bundle::EValidationStatus::invalid_schema: return "invalid schema";
        case bundle::EValidationStatus::unsupported_version: return "unsupported version";
        case bundle::EValidationStatus::invalid_name: return "invalid name";
        case bundle::EValidationStatus::invalid_extent: return "invalid file extent";
        case bundle::EValidationStatus::missing_terminator: return "missing zero terminator";
        case bundle::EValidationStatus::allocation_failed: return "allocation failed";
        default: return "unknown validation failure";
    }
}

static void report_bundle_failure(const std::int32_t slot, const bundle::SBuildResult& result,
    const filesystem_image::CBakeInput* const input) noexcept
{
    const char* const reason = failure_reason(result.status);
    if (result.status == bundle::EBuildStatus::validation_failed)
    {
        MV_LOG_RICH(error, "Bundle slot %d: build failed: %s (%s)", slot, reason, failure_reason(result.validation));
        return;
    }
    if ((input != nullptr) && result.entry.is_valid())
    {
        //  Borrow the captured path while the Host still owns the input. Failure
        //  reporting must not need another allocation, especially after OOM.
        const auto sources = input->sources();
        for (std::size_t index = 0u; index < sources.size(); ++index)
        {
            if ((sources[index].entry == result.entry) && (sources[index].physical_path != nullptr))
            {
                MV_LOG_RICH(error, "Bundle slot %d: build failed: %s; source \"%s\"", slot, reason, sources[index].physical_path);
                return;
            }
        }
        if (result.entry == input->content())
        {
            MV_LOG_RICH(error, "Bundle slot %d: build failed: %s; selected directory", slot, reason);
            return;
        }
        const CStringView name = input->inventory().name(result.entry);
        if (name.length() != 0u)
        {
            MV_LOG_RICH(error, "Bundle slot %d: build failed: %s; inventory entry \"%.*s\"",
                slot, reason, static_cast<int>(name.length()), name.cstring());
            return;
        }
    }
    MV_LOG_RICH(error, "Bundle slot %d: build failed: %s", slot, reason);
}

static const char* failure_stage(const EDocumentFailureStage stage) noexcept
{
    switch (stage)
    {
        case EDocumentFailureStage::linter: return "linter";
        case EDocumentFailureStage::structure: return "structure";
        case EDocumentFailureStage::parser: return "parser";
        default: return "processing";
    }
}

static const char* failure_reason(const EDocumentFailureReason reason) noexcept
{
    switch (reason)
    {
        case EDocumentFailureReason::utf8_decode: return "UTF-8 decode failed";
        case EDocumentFailureReason::undefined_cp1252_byte: return "undefined CP1252 byte";
        case EDocumentFailureReason::cp1252_decode: return "CP1252 decode failed";
        case EDocumentFailureReason::unexpected_character: return "unexpected character";
        case EDocumentFailureReason::unterminated_comment: return "unterminated comment";
        case EDocumentFailureReason::unterminated_string: return "unterminated string";
        case EDocumentFailureReason::invalid_escape: return "invalid escape";
        case EDocumentFailureReason::invalid_surrogate_pair: return "invalid surrogate pair";
        case EDocumentFailureReason::newline_in_name: return "newline in name";
        case EDocumentFailureReason::missing_name: return "missing name";
        case EDocumentFailureReason::missing_colon: return "missing colon";
        case EDocumentFailureReason::missing_value: return "missing value";
        case EDocumentFailureReason::missing_separator: return "missing separator";
        case EDocumentFailureReason::mismatched_delimiter: return "mismatched delimiter";
        case EDocumentFailureReason::unexpected_end: return "unexpected end";
        case EDocumentFailureReason::trailing_content: return "trailing content";
        case EDocumentFailureReason::numeric_out_of_range: return "numeric value out of range";
        case EDocumentFailureReason::construction_failed: return "construction failed";
        case EDocumentFailureReason::invalid_input_view: return "invalid input view";
        case EDocumentFailureReason::allocation_failed: return "allocation failed";
        case EDocumentFailureReason::input_limit: return "input limit exceeded";
        case EDocumentFailureReason::storage_limit: return "storage limit exceeded";
        case EDocumentFailureReason::internal_error: return "internal error";
        default: return "unspecified failure";
    }
}

static void report_parse_failure(const std::int32_t slot, const CDocumentReport& report) noexcept
{
    if (!report.processing_succeeded())
    {
        const CDocumentFailure& failure = report.failure;
        const char* const stage = failure_stage(failure.stage);
        const char* const reason = failure_reason(failure.reason);
        const bool show_start = (failure.stage != EDocumentFailureStage::linter) && failure.element_start.available &&
            (!failure.location.available ||
                (failure.element_start.line_1_based != failure.location.line_1_based) ||
                (failure.element_start.code_point_column_1_based != failure.location.code_point_column_1_based));
        if (failure.location.available && show_start)
        {
            MV_LOG_RICH(error, "Document slot %d: %s: %s at %zu:%zu; element starts %zu:%zu", slot, stage, reason,
                failure.location.line_1_based, failure.location.code_point_column_1_based,
                failure.element_start.line_1_based, failure.element_start.code_point_column_1_based);
        }
        else if (failure.location.available)
        {
            MV_LOG_RICH(error, "Document slot %d: %s: %s at %zu:%zu", slot, stage, reason,
                failure.location.line_1_based, failure.location.code_point_column_1_based);
        }
        else if (show_start)
        {
            MV_LOG_RICH(error, "Document slot %d: %s: %s; element starts %zu:%zu", slot, stage, reason,
                failure.element_start.line_1_based, failure.element_start.code_point_column_1_based);
        }
        else
        {
            MV_LOG_RICH(error, "Document slot %d: %s: %s", slot, stage, reason);
        }
    }
    else if (report.policy.status == EDocumentPolicyStatus::invalid_options)
    {
        MV_LOG(error, "Document slot {}: invalid policy bits 0x{}", slot, report.policy.unknown_policy_bits);
    }
    else if (report.policy.status == EDocumentPolicyStatus::rejected)
    {
        MV_LOG(error, "Document slot {}: policy rejected features 0x{}", slot, report.policy.disallowed_features);
    }
}

}   //  namespace asset_diagnostics

//==============================================================================
//  Shared worker operations
//==============================================================================

static EAssetStatus prepare_bundle(BundlePreparation& work, const std::int32_t slot) noexcept
{
    if (work.loaded.is_ready() && !work.block.adopt_buffer(std::move(work.loaded)))
    {
        MV_LOG(error, "Bundle slot {}: loaded document adoption failed", slot);
        return EAssetStatus::conditioning_failed;
    }
    const auto bytes = work.block.buffer_bytes();
    if (!work.view.is_ready())
    {
        const auto validation = work.view.reset(bytes.data(), bytes.size());
        if (!validation.succeeded())
        {
            MV_LOG_RICH(error, "Bundle slot %d: preparation failed: %s", slot, asset_diagnostics::failure_reason(validation.status));
            return (validation.status == bundle::EValidationStatus::allocation_failed) ?
                EAssetStatus::allocation_failed : EAssetStatus::conditioning_failed;
        }
    }
    else if ((work.view.bytes().data() != bytes.data()) || (work.view.bytes().size() != bytes.size()))
    {
        return EAssetStatus::conditioning_failed;
    }

    //  A builder result already has an immutable validated view. Loading obtains
    //  that view above; neither path repeats validation on the Host.
    if (!work.directory.prepare(work.view))
    {
        MV_LOG(error, "Bundle slot {}: directory preparation allocation failed", slot);
        return EAssetStatus::allocation_failed;
    }
    return EAssetStatus::success;
}

static EAssetStatus condition_document(const DocumentConditionRequest& request, const std::int32_t slot, DocumentConditionResult& result) noexcept
{
    if ((request.kind > EDocumentSource::live) || (request.write_json && (request.options == nullptr)))
    {
        MV_ASSERT_REPORT("Document slot {}: invalid conditioning request", slot);
        return EAssetStatus::conditioning_failed;
    }

    CLiveDocument parsed;
    const CLiveDocument* live = nullptr;
    CBakedDocument baked;
    switch (request.kind)
    {
        case EDocumentSource::text:
        {
            const CDocumentReport report = document_parser::parse(request.source.text, parsed, request.policy);
            result.findings = report.findings;
            result.policy = report.policy.status;
            if (!report.accepted())
            {
                asset_diagnostics::report_parse_failure(slot, report);
                return report.processing_succeeded() ? EAssetStatus::policy_rejected : EAssetStatus::conditioning_failed;
            }
            live = &parsed;
            break;
        }
        case EDocumentSource::live:
        {
            live = request.source.live;
            break;
        }
        case EDocumentSource::baked:
        {
            baked = request.source.baked;
            break;
        }
        default:
        {   //  Validation above limits the kind to the three handled sources; no current request reaches this.
            MV_ASSERT_REPORT("Document slot {}: invalid conditioning source", slot);
            return EAssetStatus::conditioning_failed;
        }
    }

    if (live != nullptr)
    {
        if (!document_translation::bake(*live, result.storage.baked))
        {
            MV_LOG(error, "Document slot {}: baking failed", slot);
            return EAssetStatus::conditioning_failed;
        }
        baked = result.storage.baked.document();
    }
    if (!baked.is_ready())
    {
        MV_LOG(error, "Document slot {}: source document is not ready", slot);
        return EAssetStatus::conditioning_failed;
    }
    if (request.write_json)
    {
        CDocumentWriteResult written = document_writer::write(baked, *request.options);
        if (!written.report.succeeded())
        {   //  Leave the baked snapshot intact for the Host's retention decision.
            MV_LOG(error, "Document slot {}: JSON writing failed (status {})", slot, static_cast<unsigned int>(written.report.status));
            return EAssetStatus::conditioning_failed;
        }

        //  File bytes exclude the writer's physical terminal zero.
        (void)written.output.set_size(written.report.logical_text_byte_size);
        result.storage.text = std::move(written.output);
    }
    return EAssetStatus::success;
}

static EModuleStatus perform_module_work(SModuleWork& work) noexcept
{
    const ModuleRequest& request = *work.request;
    work.function = nullptr;
    work.service_thread = {};
    if (work.previous != nullptr)
    {
        if (!work.previous->unbind())
        {
            return EModuleStatus::unload_failed;
        }
        MV_LOG_RICH(info, "Module unloaded on Host worker, mount %s",
            system_id_registry::lookup_mount_point_name(module_ids::ops::get_mount_point_id(request.module)));
    }
    if ((request.action == EModuleAction::unload) || work.cleanup_only)
    {
        return EModuleStatus::success;
    }
    if (work.physical_file == nullptr)
    {
        return EModuleStatus::binding_failed;
    }

    constexpr modules::SAdvertisedIdentity host_identity{
        module_ids::executable, { modules::k_binding_abi_major, 0u },
        modules::k_binding_abi_major, modules::k_binding_abi_major };
    const platform::path::NativePath path = platform::path::makeNativePath(work.physical_file);
    EModuleStatus status = EModuleStatus::binding_failed;
    if (path.is_ready() && work.next->bind(path, request.module, host_identity) &&
        (work.next->advertised_module_identity().version.major == modules::k_binding_abi_major))
    {
        status = EModuleStatus::installation_failed;
        if (work.next->install(*work.registry, request.module, work.memory_context, work.debug_service))
        {
            status = EModuleStatus::success;
            modules::SCoreFunctions unsupported;
            modules::FModuleFunction unknown = nullptr;
            if ((work.next->populate_core_functions(modules::k_binding_abi_major + 1u, unsupported) !=
                    modules::EBindingResult::unsupported_version) || !unsupported.is_empty() ||
                work.next->query_function(system_type_ids::undefined, unknown) || (unknown != nullptr))
            {
                status = EModuleStatus::binding_failed;
            }
            if ((request.required_function != system_type_ids::undefined) &&
                !work.next->query_function(request.required_function, work.function))
            {
                status = EModuleStatus::function_unavailable;
            }
            const modules::EBindingResult service = work.next->query_service_thread(work.service_thread);
            if (((service != modules::EBindingResult::success) &&
                    ((service != modules::EBindingResult::unsupported_function) || request.require_service_thread)) ||
                ((work.service_thread.entry != nullptr) && (request.module == module_ids::executive)))
            {
                status = EModuleStatus::function_unavailable;
            }
        }
    }
    if (status != EModuleStatus::success)
    {
        //  Failed installation must also release its native handle on this worker.
        //  Failure to unload keeps the record bound; the Host must not overwrite it.
        if (!work.next->unbind())
        {
            return EModuleStatus::unload_failed;
        }
        return status;
    }
    MV_LOG(info, "Module loaded and bound on Host worker");
    return EModuleStatus::success;
}

//==============================================================================
//  CHostWorkerThread
//  All Host background workers use this loop; the Host selects the destination.
//==============================================================================

class CHostWorkerThread
{
public:
    static std::uint32_t MV_STD_ABI_CALL entry_point(void* const user_data) noexcept;

private:
    CHostWorkerThread(const CHostWorkerThread&) noexcept = delete;
    CHostWorkerThread& operator=(const CHostWorkerThread&) noexcept = delete;
    CHostWorkerThread(CHostWorkerThread&&) noexcept = delete;
    CHostWorkerThread& operator=(CHostWorkerThread&&) noexcept = delete;

    explicit CHostWorkerThread(threading::CThreadResources& resources) noexcept : m_context{ resources } {}
    ~CHostWorkerThread() noexcept = default;

    std::uint32_t main() noexcept;
    void startup() noexcept;
    void operate() noexcept;
    void shutdown() noexcept;

    threading::CThreadContext m_context;
    bool m_failed{ false };
};

std::uint32_t MV_STD_ABI_CALL CHostWorkerThread::entry_point(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        MV_ASSERT_REPORT("CHostWorkerThread entry received a null user_data pointer");
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    CHostWorkerThread thread(resources);
    return thread.main();
}

std::uint32_t CHostWorkerThread::main() noexcept
{
    startup();
    operate();
    shutdown();
    return m_failed ? 1u : 0u;
}

void CHostWorkerThread::startup() noexcept
{
    m_context.startup();
    MV_INFO("Worker starting");
    m_context.mark_running();
}

void CHostWorkerThread::operate() noexcept
{
    std::uint32_t epoch = 0u;
    while (!m_context.exit_requested())
    {
        m_context.advance_heartbeat();
        threading::CErasedPodMsg inbound_msg;
        if (m_context.read(inbound_msg))
        {
            MV_TRACE("Worker message received");

            switch (inbound_msg.query_message_type_id().raw_value())
            {
                case k_type_id_v<BundleBuildWork>.raw_value():
                {
                    MV_DETAIL("Worker bundle build request");

                    BundleBuildWork request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto completion = threading::make_owner_message<BundleBuildResult>(inbound_msg.query_async_slot());
                    if (BundleBuildResult* const result = completion.owner().payload<BundleBuildResult>())
                    {
                        result->result.status = bundle::EBuildStatus::invalid_input;
                        if (request.input != nullptr)
                        {
                            result->result = bundle::build(request.input->inventory(), request.input->content(),
                                request.input->sources(), result->block, result->view);
                        }
                        if (!result->result.succeeded())
                        {
                            asset_diagnostics::report_bundle_failure(inbound_msg.query_async_slot(), result->result, request.input);
                        }
                    }
                    else
                    {
                        MV_LOG(error, "Bundle slot {}: result allocation failed", inbound_msg.query_async_slot());
                    }
                    if (!m_context.post(std::move(completion)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<BundlePrepareWork>.raw_value():
                {
                    MV_DETAIL("Worker bundle prepare request");

                    BundlePrepareWork request;
                    BundlePrepareResult result;
                    if (inbound_msg.copy_payload_to(request) && (request.preparation != nullptr))
                        result.status = prepare_bundle(*request.preparation, inbound_msg.query_async_slot());
                    const auto completion = threading::make_pod_message(inbound_msg.query_async_slot(), result);
                    if (!m_context.post(completion))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<FilesystemScanRequest>.raw_value():
                {
                    MV_DETAIL("Worker filesystem scan request");

                    FilesystemScanRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto completion = threading::make_owner_message<FilesystemScanResult>(inbound_msg.query_async_slot());
                    if (FilesystemScanResult* const result = completion.owner().payload<FilesystemScanResult>())
                    {
                        result->status = (request.root == nullptr) ?
                            filesystem_image::scan_manifest("development/root-manifest.json", result->document) :
                            filesystem_image::scan_root(*request.root, result->document);
                        if (result->status != filesystem_image::EScanStatus::success)
                        {
                            MV_LOG_RICH(error, "Filesystem scan failed: %s (status %u)",
                                (request.root == nullptr) ? "development/root-manifest.json" : request.root->logical_root.cstring(),
                                static_cast<unsigned int>(result->status));
                        }
                    }
                    if (!m_context.post(std::move(completion)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<ModuleWorkRequest>.raw_value():
                {
                    MV_DETAIL("Worker module lifecycle request");

                    ModuleWorkRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    ModuleWorkResult result;
                    if ((request.work != nullptr) && (request.work->request != nullptr))
                    {
                        result.status = perform_module_work(*request.work);
                    }

                    const auto completion = threading::make_pod_message(inbound_msg.query_async_slot(), result);
                    if (!m_context.post(completion))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<FileLoadRequest>.raw_value():
                {
                    MV_DETAIL("Worker file load request");

                    FileLoadRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto outbound_msg = threading::make_owner_message<FileLoadResult>(inbound_msg.query_async_slot());
                    if (ByteStorage* const result = outbound_msg.owner().payload<ByteStorage>())
                    {
                        result->buffer = platform::filesystem::loadFile(request.file, 0u, request.alignment);
                    }
                    const ByteStorage* const loaded = outbound_msg.owner().payload<ByteStorage>();
                    if ((loaded == nullptr) || !loaded->buffer.is_ready())
                    {
                        MV_LOG_RICH(error, "File load failed: %s", ((request.file != nullptr) ? request.file : "<null path>"));
                    }
                    if (!m_context.post(std::move(outbound_msg)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<FileSaveRequest>.raw_value():
                {
                    MV_DETAIL("Worker file save request");

                    FileSaveRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    FileSaveResult result;
                    result.success = platform::filesystem::saveFile(request.file, request.view);
                    if (!result.success)
                    {
                        MV_LOG_RICH(error, "File save failed: %s", ((request.file != nullptr) ? request.file : "<null path>"));
                    }
                    const auto outbound_msg = threading::make_pod_message(inbound_msg.query_async_slot(), result);
                    if (!m_context.post(outbound_msg))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<TgaEncodeRequest>.raw_value():
                {
                    MV_DETAIL("Worker TGA encode request");

                    TgaEncodeRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto outbound_msg = threading::make_owner_message<TgaEncodeResult>(inbound_msg.query_async_slot());
                    if (ByteStorage* const result = outbound_msg.owner().payload<ByteStorage>())
                    {
                        result->buffer = image::codec::tga::encode(request.view, request.options);
                    }
                    if (!m_context.post(std::move(outbound_msg)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<TgaDecodeRequest>.raw_value():
                {
                    MV_DETAIL("Worker TGA decode request");

                    TgaDecodeRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto outbound_msg = threading::make_owner_message<TgaDecodeResult>(inbound_msg.query_async_slot());
                    if (DecodedTga* const result = outbound_msg.owner().payload<DecodedTga>())
                    {
                        result->buffer = image::codec::tga::decode(request.view, result->desc, request.decode_top_down);
                        //  The codec defaults to bottom-up rows; the image view uses a
                        //  top-left logical origin and reverses addressing for that storage.
                        result->storage_bottom_up = !request.decode_top_down;
                    }
                    if (!m_context.post(std::move(outbound_msg)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                case k_type_id_v<DocumentConditionRequest>.raw_value():
                {
                    MV_DETAIL("Worker document conditioning request");

                    DocumentConditionRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    auto outbound_msg = threading::make_owner_message<DocumentConditionResult>(inbound_msg.query_async_slot());
                    if (DocumentConditionResult* const result = outbound_msg.owner().payload<DocumentConditionResult>())
                    {
                        result->status = condition_document(request, inbound_msg.query_async_slot(), *result);
                    }
                    else
                    {
                        MV_LOG(error, "Document slot {}: result allocation failed", inbound_msg.query_async_slot());
                    }
                    if (!m_context.post(std::move(outbound_msg)))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
                default:
                {
                    system_type_id unrecognised_id;
                    if (!inbound_msg.query_message_type_id().try_system_type_id(unrecognised_id))
                    {
                        MV_DETAIL("Worker unrecognised LOCAL message type");
                        break;
                    }
                    MV_DETAIL("Worker unrecognised message type {}", unrecognised_id);

                    UnrecognisedMsg unrecognised;
                    unrecognised.msg_id = unrecognised_id;
                    const auto outbound_msg = threading::make_pod_message(inbound_msg.query_async_slot(), unrecognised);
                    if (!m_context.post(outbound_msg))
                    {
                        MV_LOG(error, "Host worker: completion delivery failed at slot {}", inbound_msg.query_async_slot());
                        m_failed = true;
                        return;
                    }
                    break;
                }
            }
        }
        else
        {
            epoch = m_context.wait_for_new_epoch(epoch);
        }
    }
}

void CHostWorkerThread::shutdown() noexcept
{
    if (m_failed)
    {
        m_context.mark_failed(1u);
        return;
    }
    m_context.mark_exiting();
    m_context.mark_exited();
    MV_INFO("Worker exited");
}

platform::threading::FThreadEntry host_worker_thread_entry_point() noexcept
{
    return &CHostWorkerThread::entry_point;
}

}   //  namespace host


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    host_worker_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Shared Host worker for file I/O, asset conditioning and module binding.

#include <cstdint>      //  std::uint32_t
#include <utility>      //  std::move

#include "host/runtime/host_worker_thread.hpp"
#include "host/runtime/module_service.hpp"

#include "debug/macros.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "image/codec/tga.hpp"
#include "platform/path/native_path.hpp"
#include "platform/filesystem/file.hpp"
#include "system/erased_owner.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

namespace host
{

//==============================================================================
//  Document failure diagnostics
//==============================================================================

namespace asset_diagnostics
{

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
            MV_REPORT("Document slot %d: %s: %s at %zu:%zu; element starts %zu:%zu", slot, stage, reason,
                failure.location.line_1_based, failure.location.code_point_column_1_based,
                failure.element_start.line_1_based, failure.element_start.code_point_column_1_based);
        }
        else if (failure.location.available)
        {
            MV_REPORT("Document slot %d: %s: %s at %zu:%zu", slot, stage, reason,
                failure.location.line_1_based, failure.location.code_point_column_1_based);
        }
        else if (show_start)
        {
            MV_REPORT("Document slot %d: %s: %s; element starts %zu:%zu", slot, stage, reason,
                failure.element_start.line_1_based, failure.element_start.code_point_column_1_based);
        }
        else
        {
            MV_REPORT("Document slot %d: %s: %s", slot, stage, reason);
        }
    }
    else if (report.policy.status == EDocumentPolicyStatus::invalid_options)
    {
        MV_REPORT("Document slot %d: invalid policy bits 0x%08x", slot, report.policy.unknown_policy_bits);
    }
    else if (report.policy.status == EDocumentPolicyStatus::rejected)
    {
        MV_REPORT("Document slot %d: policy rejected features 0x%08x", slot, report.policy.disallowed_features);
    }
}

}   //  namespace asset_diagnostics

//==============================================================================
//  Shared worker operations
//==============================================================================

static EAssetStatus condition_document(const DocumentConditionRequest& request, const std::int32_t slot, DocumentConditionResult& result) noexcept
{
    if ((request.kind > EDocumentSource::live) || (request.write_json && (request.options == nullptr)))
    {
        MV_REPORT("Document slot %d: invalid conditioning request", slot);
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
            MV_REPORT("Document slot %d: invalid conditioning source", slot);
            return EAssetStatus::conditioning_failed;
        }
    }

    if (live != nullptr)
    {
        if (!document_translation::bake(*live, result.storage.baked))
        {
            MV_REPORT("Document slot %d: baking failed", slot);
            return EAssetStatus::conditioning_failed;
        }
        baked = result.storage.baked.document();
    }
    if (!baked.is_ready())
    {
        MV_REPORT("Document slot %d: source document is not ready", slot);
        return EAssetStatus::conditioning_failed;
    }
    if (request.write_json)
    {
        CDocumentWriteResult written = document_writer::write(baked, *request.options);
        if (!written.report.succeeded())
        {   //  Leave the baked snapshot intact for the Host's retention decision.
            MV_REPORT("Document slot %d: JSON writing failed (status %u)", slot, static_cast<unsigned int>(written.report.status));
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
    work.thread_function = nullptr;
    if (work.previous != nullptr)
    {
        if (!work.previous->unbind())
        {
            return EModuleStatus::unload_failed;
        }
        MV_REPORT("Module unloaded on Host worker, mount %s",
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
            if ((module_ids::ops::get_mount_point_id(request.module) == mount_point_ids::render) &&
                !work.next->query_function(system_type_ids::rendering_thread_function, work.thread_function))
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
    MV_REPORT("Module loaded and bound on Host worker");
    return EModuleStatus::success;
}

//==============================================================================
//  CHostWorkerThread
//  Both Host workers use this dispatch loop; the Host selects the destination.
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
        MV_ERROR("CHostWorkerThread entry received a null user_data pointer");
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
                case k_type_id_v<FilesystemScanRequest>.raw_value():
                {
                    MV_DETAIL("Worker filesystem scan request");

                    FilesystemScanRequest request;
                    (void)inbound_msg.copy_payload_to(request);
                    CErasedOwner content = CErasedOwner::create<FilesystemScanResult>();
                    if (FilesystemScanResult* const result = content.payload<FilesystemScanResult>())
                    {
                        result->status = (request.root == nullptr) ?
                            filesystem_image::scan_manifest("development/root-manifest.json", result->document) :
                            filesystem_image::scan_root(*request.root, result->document);
                        if (result->status != filesystem_image::EScanStatus::success)
                        {
                            MV_REPORT("Filesystem scan failed: %s (status %u)",
                                (request.root == nullptr) ? "development/root-manifest.json" : request.root->logical_root.cstring(),
                                static_cast<unsigned int>(result->status));
                        }
                    }
                    threading::CErasedOwnerMsg completion;
                    completion.set_message_type<FilesystemScanResult>();
                    completion.set_async_slot(inbound_msg.query_async_slot());
                    completion.set_owner(std::move(content));
                    if (!m_context.post(std::move(completion)))
                    {
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

                    threading::CErasedPodMsg completion;
                    completion.set_async_slot(inbound_msg.query_async_slot());
                    completion.assign_payload(result);
                    if (!m_context.post(completion))
                    {
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
                    CErasedOwner content = CErasedOwner::create<LoadedFile>();
                    if (LoadedFile* const result = content.payload<LoadedFile>())
                    {
                        result->buffer = platform::filesystem::loadFile(request.file, 0u, request.alignment);
                    }
                    const LoadedFile* const loaded = content.payload<LoadedFile>();
                    if ((loaded == nullptr) || !loaded->buffer.is_ready())
                    {
                        MV_REPORT("File load failed: %s", (request.file != nullptr) ? request.file : "<null path>");
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<FileLoadResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    if (!m_context.post(std::move(outbound_msg)))
                    {
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
                        MV_REPORT("File save failed: %s", (request.file != nullptr) ? request.file : "<null path>");
                    }
                    threading::CErasedPodMsg outbound_msg;
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.assign_payload(result);
                    if (!m_context.post(outbound_msg))
                    {
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
                    CErasedOwner content = CErasedOwner::create<EncodedTga>();
                    if (EncodedTga* const result = content.payload<EncodedTga>())
                    {
                        result->buffer = image::codec::tga::encode(request.view, request.options);
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<TgaEncodeResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    if (!m_context.post(std::move(outbound_msg)))
                    {
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
                    CErasedOwner content = CErasedOwner::create<DecodedTga>();
                    if (DecodedTga* const result = content.payload<DecodedTga>())
                    {
                        result->buffer = image::codec::tga::decode(request.view, result->desc, request.decode_top_down);
                        //  The codec defaults to bottom-up rows; the image view uses a
                        //  top-left logical origin and reverses addressing for that storage.
                        result->storage_bottom_up = !request.decode_top_down;
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<TgaDecodeResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    if (!m_context.post(std::move(outbound_msg)))
                    {
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
                    CErasedOwner content = CErasedOwner::create<DocumentConditionResult>();
                    if (DocumentConditionResult* const result = content.payload<DocumentConditionResult>())
                    {
                        result->status = condition_document(request, inbound_msg.query_async_slot(), *result);
                    }
                    else
                    {
                        MV_REPORT("Document slot %d: result allocation failed", inbound_msg.query_async_slot());
                    }
                    threading::CErasedOwnerMsg outbound_msg;
                    outbound_msg.set_message_type<DocumentConditionResult>();
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.set_owner(std::move(content));
                    if (!m_context.post(std::move(outbound_msg)))
                    {
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
                    threading::CErasedPodMsg outbound_msg;
                    outbound_msg.set_async_slot(inbound_msg.query_async_slot());
                    outbound_msg.assign_payload(unrecognised);
                    if (!m_context.post(outbound_msg))
                    {
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

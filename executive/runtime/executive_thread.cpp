
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Executive thread and asynchronous asset-service acceptance exercise.

#include "threading/messages/message_helpers.hpp"

#include <cstdint>      //  std::int32_t, std::uint32_t, std::uint64_t
#include <cstring>      //  std::strcmp
#include <utility>      //  std::move

#include "runtime/executive_thread.hpp"
#include "runtime/request_preparation.hpp"
#include "module/module_binding_context.hpp"

#include "debug/macros.hpp"
#include "image/codec/tga.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "platform/system/performance_counter.hpp"
#include "platform/module/binding.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/system_id_registry.hpp"
#include "assets/asset_messages.hpp"
#include "system/service_messages.hpp"
#include "threading/CThreadPackage.hpp"
#include "tests/support/host_test_paths.hpp"

namespace executive
{

namespace asset_acceptance
{

enum class EScenario : std::uint8_t
{
    retain_raw = 0,
    save_retained_raw,
    load_saved_raw,
    save_discarded_raw,
    load_discarded_raw_output,
    load_tga,
    save_retained_image,
    reload_tga,
    save_cached_image,
    load_cached_image,
    save_retained_greyscale,
    save_discarded_rgba,
    retain_live,
    save_live_binary,
    load_live_binary,
    save_live_json,
    load_live_json,
    mutate_live_and_save_binary,
    load_mutated_live_binary,
    save_live_json_retain_baked,
    save_retained_baked_binary,
    load_retained_baked_binary,
    save_discarded_baked_json,
    load_baked_json,
    retain_baked,
    retain_baked_from_live,
    retain_raw_on_save_failure,
    save_raw_after_failure,
    discard_raw_on_save_failure,
    retain_baked_on_save_failure,
    reject_invalid_asset,
    write_malformed_json,
    reject_malformed_json,
    reject_missing_file,
    write_policy_rejected_json,
    reject_disallowed_features,
    write_invalid_binary,
    reject_invalid_binary,
    retain_baked_on_json_failure,
    save_discarded_live_binary,
    save_discarded_live_json,
    reject_invalid_tga,
    write_invalid_encoding,
    reject_invalid_encoding,
    write_numeric_overflow,
    reject_numeric_overflow,
    reject_invalid_policy,
    write_unterminated_string,
    reject_unterminated_string,
    load_tga_bottom_up,

};

struct SScenario
{
    EScenario identity;
    const char* name;
    EAssetStatus status;
    EAssetKind retained_kind; //  none explicitly expects no retained asset or views.
    EDocumentPolicyStatus policy;
    std::uint32_t findings;
};

//  Order records fixture dependencies only. Every outcome is stated independently.
static constexpr SScenario scenarios[]{
    { EScenario::retain_raw,                   "retain_raw",                   EAssetStatus::success,             EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_raw,            "save_retained_raw",            EAssetStatus::success,             EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_saved_raw,               "load_saved_raw",               EAssetStatus::success,             EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_raw,           "save_discarded_raw",           EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_discarded_raw_output,    "load_discarded_raw_output",    EAssetStatus::success,             EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_tga,                     "load_tga",                     EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_image,          "save_retained_image",          EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reload_tga,                   "reload_tga",                   EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_cached_image,            "save_cached_image",            EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_cached_image,            "load_cached_image",            EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_greyscale,      "save_retained_greyscale",      EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_rgba,          "save_discarded_rgba",          EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_live,                  "retain_live",                  EAssetStatus::success,             EAssetKind::live,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_binary,             "save_live_binary",             EAssetStatus::success,             EAssetKind::live,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_live_binary,             "load_live_binary",             EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_json,               "save_live_json",               EAssetStatus::success,             EAssetKind::live,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_live_json,               "load_live_json",               EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::accepted,        0u },
    { EScenario::mutate_live_and_save_binary,  "mutate_live_and_save_binary",  EAssetStatus::success,             EAssetKind::live,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_mutated_live_binary,     "load_mutated_live_binary",     EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_json_retain_baked,  "save_live_json_retain_baked",  EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_baked_binary,   "save_retained_baked_binary",   EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_retained_baked_binary,   "load_retained_baked_binary",   EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_baked_json,    "save_discarded_baked_json",    EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_baked_json,              "load_baked_json",              EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::accepted,        0u },
    { EScenario::retain_baked,                 "retain_baked",                 EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_from_live,       "retain_baked_from_live",       EAssetStatus::success,             EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_raw_on_save_failure,   "retain_raw_on_save_failure",   EAssetStatus::write_failed,        EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_raw_after_failure,       "save_raw_after_failure",       EAssetStatus::success,             EAssetKind::raw,             EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::discard_raw_on_save_failure,  "discard_raw_on_save_failure",  EAssetStatus::write_failed,        EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_on_save_failure, "retain_baked_on_save_failure", EAssetStatus::write_failed,        EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_asset,         "reject_invalid_asset",         EAssetStatus::invalid_asset,       EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_malformed_json,         "write_malformed_json",         EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_malformed_json,        "reject_malformed_json",        EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      document_finding_bit(EDocumentFinding::unquoted_names) },
    { EScenario::reject_missing_file,          "reject_missing_file",          EAssetStatus::read_failed,         EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_policy_rejected_json,   "write_policy_rejected_json",   EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_disallowed_features,   "reject_disallowed_features",   EAssetStatus::policy_rejected,     EAssetKind::none,            EDocumentPolicyStatus::rejected,        document_finding_bit(EDocumentFinding::unquoted_names) },
    { EScenario::write_invalid_binary,         "write_invalid_binary",         EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_binary,        "reject_invalid_binary",        EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_on_json_failure, "retain_baked_on_json_failure", EAssetStatus::conditioning_failed, EAssetKind::baked,           EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_live_binary,   "save_discarded_live_binary",   EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_live_json,     "save_discarded_live_json",     EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_tga,           "reject_invalid_tga",           EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_invalid_encoding,       "write_invalid_encoding",       EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_encoding,      "reject_invalid_encoding",      EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      (EDocumentFinding::utf8_attempt_failed | EDocumentFinding::cp1252) },
    { EScenario::write_numeric_overflow,       "write_numeric_overflow",       EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_numeric_overflow,      "reject_numeric_overflow",      EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_policy,        "reject_invalid_policy",        EAssetStatus::policy_rejected,     EAssetKind::none,            EDocumentPolicyStatus::invalid_options, 0u },
    { EScenario::write_unterminated_string,    "write_unterminated_string",    EAssetStatus::success,             EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_unterminated_string,   "reject_unterminated_string",   EAssetStatus::conditioning_failed, EAssetKind::none,            EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_tga_bottom_up,           "load_tga_bottom_up",           EAssetStatus::success,             EAssetKind::image,           EDocumentPolicyStatus::unexamined,      0u },
};

static constexpr std::uint32_t scenario_count = sizeof(scenarios) / sizeof(scenarios[0]);
static constexpr std::uint32_t concurrent_save_count{ 32u };
static_assert((concurrent_save_count <= 100u), "Concurrent filenames reserve two decimal digits.");

static constexpr char fixture[] = R"({"null":null,"bool":true,"signed":-123,"unsigned":18446744073709551615,"float":1.25,"text":"line\ntext","object":{},"mixed":[null,true,3,"text"],"bools":[true,false],"ints":[-1,-2],"uints":[18446744073709551615,18446744073709551614],"floats":[1.25,2.5],"strings":["a","b"],"empty":[]})";
static constexpr char raw_file[] = "test-output:/asset-acceptance.raw";
static constexpr char binary_file[] = "test-output:/asset-acceptance.bin";
static constexpr char json_file[] = "test-output:/asset-acceptance.json";
static constexpr char image_file[] = "test-output:/asset-acceptance.tga";
static constexpr char failure_file[] = "test-output:/asset-acceptance-missing-directory/output.bin";
static constexpr char cached_image_file[] = "dev-workspace:/acceptance/cached-image.tga";

static bool prepare_output_load(threading::CErasedOwnerMsg& pending, const std::int32_t slot,
    const EAssetFileFormat format, const char* file, const CDocumentParseOptions policy = {}) noexcept
{
    static constexpr char prefix[] = "test-output:/";
    CSimpleString native;
    return (std::strncmp(file, prefix, sizeof(prefix) - 1u) == 0) &&
        host_test_paths::native_output(file + sizeof(prefix) - 1u, native) &&
        request_preparation::prepare_load_request(pending, slot, format, native.cstring(), policy);
}

static bool fixture_document(CLiveDocument& document) noexcept
{
    return document_parser::parse(CByteConstView{ reinterpret_cast<const std::uint8_t*>(fixture), (sizeof(fixture) - 1u) }, document).accepted() &&
        document.append_child(document.root(), document.create_empty(
            CStringView{ reinterpret_cast<const std::uint8_t*>("empty_value"), 11u })).succeeded();
}

static bool equal_bytes(const CByteConstView& left, const CByteConstView& right) noexcept
{
    return (left.size() == right.size()) && left.is_ready() && right.is_ready() &&
        (std::memcmp(left.data(), right.data(), left.size()) == 0);
}

static bool equal_documents(const CBakedDocument& left, const CBakedDocument& right) noexcept
{
    CDocumentWriteOptions options;
    options.mode = EDocumentWriteMode::strict_json;
    const CDocumentWriteResult a = document_writer::write(left, options);
    const CDocumentWriteResult b = document_writer::write(right, options);
    return a.report.succeeded() && b.report.succeeded() && equal_bytes(a.output.const_view(), b.output.const_view());
}


static AssetSaveSettings save_settings(const EAssetFileFormat format) noexcept
{
    AssetSaveSettings settings;
    settings.format = format;
    return settings;
}

static bool expect(const SScenario& scenario, const bool satisfied, const char* const property) noexcept
{
    if (!satisfied)
    {
        MV_LOG_RICH(error, "Executive: %s: failed expectation: %s", scenario.name, property);
    }
    return satisfied;
}

}   //  namespace asset_acceptance

class CExecutiveThread
{
public:
    static std::uint32_t MV_STD_ABI_CALL entry_point(void* const user_data) noexcept;

private:
    //  Failure codes start at one because zero is the successful thread exit code.
    enum class EFailure : std::uint32_t { registry = 1u, timing, submission, completion, timeout };

    enum class EPhase : std::uint8_t { rendering_acknowledgement = 0, rendering_completion, module_tests, sequential, concurrent, filesystem, bundle, awaiting_exit, complete };

    CExecutiveThread(const CExecutiveThread&) noexcept = delete;
    CExecutiveThread& operator=(const CExecutiveThread&) noexcept = delete;
    CExecutiveThread(CExecutiveThread&&) noexcept = delete;
    CExecutiveThread& operator=(CExecutiveThread&&) noexcept = delete;
    explicit CExecutiveThread(threading::CThreadResources& resources) noexcept : m_context{ resources } {}
    ~CExecutiveThread() noexcept = default;

    std::uint32_t main() noexcept;
    [[nodiscard]] bool startup() noexcept;
    [[nodiscard]] bool initialise() noexcept;
    [[nodiscard]] bool accept_rendering_notice(const threading::CErasedPodMsg& message) noexcept;
    void operate() noexcept;
    [[nodiscard]] bool prepare_scenario(threading::CErasedOwnerMsg& pending, const asset_acceptance::SScenario& scenario, const std::int32_t slot) noexcept;
    [[nodiscard]] bool check(const asset_acceptance::SScenario& scenario, const AssetResult& result) noexcept;
    [[nodiscard]] bool prepare_raw_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const char* const text, const EAssetRetention retention, const bool save, const char* const file) noexcept;
    [[nodiscard]] bool prepare_image_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const bool greyscale, const bool storage_bottom_up, const EAssetRetention retention, const char* const file) noexcept;
    [[nodiscard]] bool prepare_live_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const EAssetRetention retention, const bool save, const char* const file, const AssetSaveSettings& settings) noexcept;
    [[nodiscard]] bool prepare_baked_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const EAssetRetention retention, const bool save, const char* const file, const AssetSaveSettings& settings) noexcept;
    [[nodiscard]] bool prepare_expected_document() noexcept;
    [[nodiscard]] bool submit_concurrent_saves() noexcept;
    [[nodiscard]] bool complete_concurrent_save(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool start_filesystem_exercise() noexcept;
    [[nodiscard]] bool complete_filesystem_exercise(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool start_bundle_exercise() noexcept;
    [[nodiscard]] bool complete_bundle_exercise(const threading::CErasedPodMsg& message) noexcept;
    [[nodiscard]] bool request_system_shutdown() noexcept;
    void shutdown() noexcept;
    void fail(const EFailure failure, const char* const reason) noexcept;

    threading::CThreadContext m_context;
    platform::system::CPerfCounter m_perf_counter;
    std::uint32_t m_scenario_index{ 0u };
    std::int32_t m_pending_slot{ 0 };
    std::int32_t m_concurrent_first_slot{ 0 };
    EPhase m_phase{ EPhase::rendering_acknowledgement };
    AssetResult m_raw;
    AssetResult m_image;
    image::CImageView m_image_view;
    AssetResult m_live;
    AssetResult m_baked;
    CBakedDocumentBlock m_expected;
    bool m_save_completed[asset_acceptance::concurrent_save_count]{};
    std::uint32_t m_completed_save_count{ 0u };
    bool m_filesystem_completed[6]{};
    std::uint32_t m_filesystem_count{ 0u };
    std::int32_t m_filesystem_step{ 0 };
    std::int32_t m_refresh_step{ 0 };
    AssetResult m_bundle;
    CByteBuffer m_bundle_snapshot;
    std::int32_t m_bundle_step{ 0 };
    std::uint32_t m_failure_code{ 0u };
};

std::uint32_t MV_STD_ABI_CALL CExecutiveThread::entry_point(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        MV_ASSERT_REPORT("CExecutiveThread entry received a null user_data pointer");
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    if (!modules::is_thread_context_ready(user_data))
    {
        resources.control_state.mark_failed(~0u);
        MV_ASSERT_REPORT("CExecutiveThread entry detected incomplete module context installation");
        return ~0u;
    }

    CExecutiveThread thread(resources);
    return thread.main();
}

std::uint32_t CExecutiveThread::main() noexcept
{
    if (startup())
    {
        operate();
    }
    shutdown();
    return m_failure_code;
}

bool CExecutiveThread::startup() noexcept
{
    m_context.startup();
    MV_INFO("Executive: Starting");

    if (!initialise())
    {
        return false;
    }
    m_context.mark_running();
    MV_INFO("Executive: Running");
    return true;
}

bool CExecutiveThread::initialise() noexcept
{
    const char* const registry_name = system_id_registry::lookup_type_name(system_type_ids::asset_load_request);
    if ((registry_name == nullptr) || (std::strcmp(registry_name, "asset_load_request") != 0))
    {
        fail(EFailure::registry, "system registry lookup");
        return false;
    }

    MV_LOG_RICH(info, "Executive system registry authority: %s", registry_name);

    if (!m_perf_counter.update() ||
        !m_context.perf_count_conversion().is_valid())
    {
        fail(EFailure::timing, "performance counter initialisation");
        return false;
    }

    //  Renderer selection belongs to this Executive; selector Executives may
    //  omit it entirely. Loading an occupied mount is resolved by its reply.
    threading::CErasedOwnerMsg pending;
    CSimpleString rendering_file;
    if (!platform::module::make_module_filename("package:/bin/MorphicRendering", rendering_file) ||
        !request_preparation::prepare_module_request(pending, m_pending_slot,
        module_ids::render_vulkan, EModuleAction::load, rendering_file.cstring()))
    {
        fail(EFailure::submission, "rendering load preparation");
        return false;
    }
    if (!m_context.post(std::move(pending)))
    {
        fail(EFailure::submission, "rendering load submission");
        return false;
    }
    MV_LOG(info, "Executive: Rendering load requested");
    return true;
}

bool CExecutiveThread::accept_rendering_notice(const threading::CErasedPodMsg& message) noexcept
{
    MV_DETAIL("Executive rendering module notification");

    ModuleResult result;
    if (!message.copy_payload_to(result) || (message.query_async_slot() != m_pending_slot) ||
        (result.module != module_ids::render_vulkan))
    {
        return false;
    }

    if (m_phase == EPhase::rendering_acknowledgement)
    {
        if ((result.notice != EModuleNotice::acknowledged) || (result.status != EModuleStatus::success))
        {
            return false;
        }
        m_phase = EPhase::rendering_completion;
        MV_LOG(info, "Executive: Rendering load acknowledged");
    }
    else
    {
        //  An Executive replacement may inherit an already running renderer.
        //  Accept only the chosen implementation, with a live service thread.
        if ((result.notice != EModuleNotice::completed) || !result.available ||
            ((result.status != EModuleStatus::success) && (result.status != EModuleStatus::already_loaded)))
        {
            return false;
        }
        m_phase = EPhase::module_tests;
        ++m_pending_slot;
        MV_LOG(info, "Executive: Rendering ready ({})",
            ((result.status == EModuleStatus::already_loaded) ?
                debug_system::CInlineText16{ "already loaded" } : debug_system::CInlineText16{ "loaded" }));
    }
    (void)m_perf_counter.update();
    return true;
}

bool CExecutiveThread::prepare_raw_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const char* const text,
    const EAssetRetention retention, const bool save, const char* const file) noexcept
{
    if (!request_preparation::can_prepare_request(pending, slot)) return false;
    CByteBuffer bytes;
    const std::size_t size = std::strlen(text);
    if (!bytes.resize(size)) return false;
    std::memcpy(bytes.data(), text, size);
    return request_preparation::prepare_asset_transfer(pending, slot, bytes, retention, save, file,
        asset_acceptance::save_settings(EAssetFileFormat::raw));
}

bool CExecutiveThread::prepare_image_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const bool greyscale, const bool storage_bottom_up,
    const EAssetRetention retention, const char* const file) noexcept
{
    if (!request_preparation::can_prepare_request(pending, slot)) return false;
    CByteRectBuffer pixels;
    if (!pixels.allocate((greyscale ? 7u : 28u), 5u, 16u)) return false;
    const auto description = greyscale ? image::codec::tga::decoded_image_desc::Gray : image::codec::tga::decoded_image_desc::RGBA;
    const image::CImageView view{ pixels.view(), description, storage_bottom_up };
    view.fill(0x7f345678u);
    view.plot(2, 3, 0xffaabbccu);
    auto settings = asset_acceptance::save_settings(EAssetFileFormat::tga);
    settings.image = view.encode_options();
    if (!request_preparation::prepare_asset_transfer(pending, slot, pixels, retention, true, file, settings)) return false;
    auto* const request = pending.owner().payload<ImageAssetTransfer>();
    request->description = description;
    request->storage_bottom_up = storage_bottom_up;
    return true;
}

bool CExecutiveThread::prepare_live_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const EAssetRetention retention, const bool save,
    const char* const file, const AssetSaveSettings& settings) noexcept
{
    if (!request_preparation::can_prepare_request(pending, slot)) return false;
    CLiveDocument document;
    return asset_acceptance::fixture_document(document) &&
        request_preparation::prepare_asset_transfer(pending, slot, document, retention, save, file, settings);
}

bool CExecutiveThread::prepare_baked_fixture(threading::CErasedOwnerMsg& pending, const std::int32_t slot, const EAssetRetention retention, const bool save,
    const char* const file, const AssetSaveSettings& settings) noexcept
{
    if (!request_preparation::can_prepare_request(pending, slot)) return false;
    CLiveDocument document;
    CBakedDocumentBlock block;
    return asset_acceptance::fixture_document(document) && document_translation::bake(document, block) &&
        request_preparation::prepare_asset_transfer(pending, slot, block, retention, save, file, settings);
}

bool CExecutiveThread::prepare_expected_document() noexcept
{
    CLiveDocument document;
    return asset_acceptance::fixture_document(document) && document_translation::bake(document, m_expected);
}

bool CExecutiveThread::prepare_scenario(threading::CErasedOwnerMsg& pending, const asset_acceptance::SScenario& scenario, const std::int32_t slot) noexcept
{
    if (!request_preparation::can_prepare_request(pending, slot)) return false;
    using namespace asset_acceptance;
    switch (scenario.identity)
    {
        case EScenario::retain_raw:
        {
            return prepare_raw_fixture(pending, slot, fixture, EAssetRetention::source, false, nullptr);
        }
        case EScenario::save_retained_raw:
        case EScenario::save_raw_after_failure:
        {
            return request_preparation::prepare_save_request(pending, slot, m_raw.asset, raw_file, save_settings(EAssetFileFormat::raw));
        }
        case EScenario::load_saved_raw:
        case EScenario::load_discarded_raw_output:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::raw, raw_file);
        }
        case EScenario::save_discarded_raw:
        case EScenario::write_invalid_binary:
        {
            return prepare_raw_fixture(pending, slot, fixture, EAssetRetention::discard, true, raw_file);
        }
        case EScenario::load_tga:
        {
            return request_preparation::prepare_load_request(pending, slot, EAssetFileFormat::tga, "dev-source:/test_input.tga");
        }
        case EScenario::load_tga_bottom_up:
        {
            return request_preparation::prepare_load_request(pending, slot, EAssetFileFormat::tga, "dev-source:/test_input.tga", {}, false);
        }
        case EScenario::save_retained_image:
        case EScenario::save_cached_image:
        {
            AssetSaveSettings settings = save_settings(EAssetFileFormat::tga);
            settings.image = m_image_view.encode_options();
            return request_preparation::prepare_save_request(pending, slot, m_image.asset,
                (scenario.identity == EScenario::save_cached_image) ? cached_image_file : image_file, settings);
        }
        case EScenario::reload_tga:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::tga, image_file);
        }
        case EScenario::load_cached_image:
        {
            return request_preparation::prepare_load_request(pending, slot, EAssetFileFormat::tga, cached_image_file);
        }
        case EScenario::save_retained_greyscale:
        {
            return prepare_image_fixture(pending, slot, true, true, EAssetRetention::source, image_file);
        }
        case EScenario::save_discarded_rgba:
        {
            return prepare_image_fixture(pending, slot, false, false, EAssetRetention::discard, image_file);
        }
        case EScenario::retain_live:
        {
            return prepare_expected_document() &&
                prepare_live_fixture(pending, slot, EAssetRetention::source, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_live_binary:
        {
            return request_preparation::prepare_save_request(pending, slot, m_live.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::load_live_binary:
        case EScenario::load_mutated_live_binary:
        case EScenario::load_retained_baked_binary:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::baked, binary_file);
        }
        case EScenario::save_live_json:
        {
            return request_preparation::prepare_save_request(pending, slot, m_live.asset, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::load_live_json:
        case EScenario::load_baked_json:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::json, json_file);
        }
        case EScenario::mutate_live_and_save_binary:
        {   //  The preceding read has completed; the next save must bake this mutation.
            CLiveDocument* const live = m_live.live_document();
            if ((live == nullptr) || !live->append_child(live->root(), live->create_boolean(false,
                CStringView{ reinterpret_cast<const std::uint8_t*>("added"), 5u })).succeeded() ||
                !document_translation::bake((*live), m_expected))
            {
                return false;
            }
            return request_preparation::prepare_save_request(pending, slot, m_live.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_live_json_retain_baked:
        {
            return prepare_expected_document() &&
                prepare_live_fixture(pending, slot, EAssetRetention::baked, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::save_retained_baked_binary:
        {
            return request_preparation::prepare_save_request(pending, slot, m_baked.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_discarded_baked_json:
        {
            return prepare_baked_fixture(pending, slot, EAssetRetention::discard, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::retain_baked:
        {
            return prepare_baked_fixture(pending, slot, EAssetRetention::source, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::retain_baked_from_live:
        {
            return prepare_live_fixture(pending, slot, EAssetRetention::baked, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::retain_raw_on_save_failure:
        {
            return prepare_raw_fixture(pending, slot, fixture, EAssetRetention::source, true, failure_file);
        }
        case EScenario::discard_raw_on_save_failure:
        {
            return prepare_raw_fixture(pending, slot, fixture, EAssetRetention::discard, true, failure_file);
        }
        case EScenario::retain_baked_on_save_failure:
        {
            return prepare_live_fixture(pending, slot, EAssetRetention::baked, true, failure_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::reject_invalid_asset:
        {
            return request_preparation::prepare_save_request(pending, slot, CAssetId{}, raw_file, save_settings(EAssetFileFormat::raw));
        }
        case EScenario::write_malformed_json:
        {
            return prepare_raw_fixture(pending, slot, "{broken", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_malformed_json:
        case EScenario::reject_invalid_encoding:
        case EScenario::reject_numeric_overflow:
        case EScenario::reject_unterminated_string:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::json, raw_file);
        }
        case EScenario::reject_missing_file: return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::raw, failure_file);
        case EScenario::write_policy_rejected_json:
        {
            return prepare_raw_fixture(pending, slot, "{unquoted:1}", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_disallowed_features:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::json, raw_file, CDocumentParseOptions{ document_policy::k_ascii });
        }
        case EScenario::reject_invalid_binary:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::baked, raw_file);
        }
        case EScenario::retain_baked_on_json_failure:
        {
            AssetSaveSettings settings = save_settings(EAssetFileFormat::json);
            settings.document.mode = static_cast<EDocumentWriteMode>(255u);
            return prepare_live_fixture(pending, slot, EAssetRetention::baked, true, json_file, settings);
        }
        case EScenario::save_discarded_live_binary:
        {
            return prepare_live_fixture(pending, slot, EAssetRetention::discard, true, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_discarded_live_json:
        {
            return prepare_live_fixture(pending, slot, EAssetRetention::discard, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::reject_invalid_tga:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::tga, raw_file);
        }
        case EScenario::write_invalid_encoding:
        {
            return prepare_raw_fixture(pending, slot, "\x81", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::write_numeric_overflow:
        {
            return prepare_raw_fixture(pending, slot, "18446744073709551616", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_invalid_policy:
        {
            return asset_acceptance::prepare_output_load(pending, slot, EAssetFileFormat::json, json_file, CDocumentParseOptions{ 0x80000000u });
        }
        case EScenario::write_unterminated_string:
        {
            return prepare_raw_fixture(pending, slot, "\"unterminated", EAssetRetention::discard, true, raw_file);
        }
        default:
        {   //  All current scenario identities are handled above; reject any future unhandled value.
            return false;
        }
    }
}

bool CExecutiveThread::check(const asset_acceptance::SScenario& scenario, const AssetResult& result) noexcept
{
    using namespace asset_acceptance;
    if ((result.status != scenario.status) || (result.kind != scenario.retained_kind) ||
        (result.document_policy != scenario.policy) || (result.document_findings != scenario.findings))
    {
        MV_LOG_RICH(error, "Executive: %s: status %u/%u, kind %u/%u, policy %u/%u, findings 0x%08x/0x%08x (actual/expected)",
            scenario.name, static_cast<unsigned int>(result.status), static_cast<unsigned int>(scenario.status),
            static_cast<unsigned int>(result.kind), static_cast<unsigned int>(scenario.retained_kind),
            static_cast<unsigned int>(result.document_policy), static_cast<unsigned int>(scenario.policy),
            result.document_findings, scenario.findings);
        return false;
    }
    if (!expect(scenario, (static_cast<bool>(result.asset) == (scenario.retained_kind != EAssetKind::none)), "retained asset identity"))
    {
        return false;
    }
    const image::CImageView image = result.image_view();
    const CBakedDocument document = result.document_view();
    switch (scenario.identity)
    {
        case EScenario::retain_raw:
        case EScenario::retain_raw_on_save_failure:
        {
            m_raw = result;
            break;
        }
        case EScenario::load_saved_raw:
        case EScenario::load_discarded_raw_output:
        {
            return expect(scenario, equal_bytes(result.byte_view(), CByteConstView{
                reinterpret_cast<const std::uint8_t*>(fixture), (sizeof(fixture) - 1u) }), "raw byte equality") &&
                expect(scenario, (result.byte_view().align() >= 32u), "raw alignment");
        }
        case EScenario::load_tga:
        {
            m_image = result;
            m_image_view = image;
            return expect(scenario, (image.is_ready() && !image.is_read_only() && !image.vertical_flip()), "writable top-down image");
        }
        case EScenario::reload_tga:
        case EScenario::load_cached_image:
        case EScenario::load_tga_bottom_up:
        {
            if (!expect(scenario, ((image.width() == m_image_view.width()) &&
                (image.height() == m_image_view.height())), "image dimensions") ||
                !expect(scenario, (image.vertical_flip() == m_image_view.vertical_flip()), "image row addressing") ||
                !expect(scenario, (scenario.identity == EScenario::reload_tga) ?
                    (result.asset != m_image.asset) : (result.asset == m_image.asset),
                    "native readback has fresh identity; indexed reads retain cached identity"))
            {
                return false;
            }
            for (std::int32_t y = 0; y < image.height(); ++y)
            {
                for (std::int32_t x = 0; x < image.width(); ++x)
                {
                    if (!expect(scenario, (image.texel(x, y) == m_image_view.texel(x, y)), "image texel equality"))
                    {
                        return false;
                    }
                }
            }
            break;
        }
        case EScenario::save_retained_greyscale:
        {
            return expect(scenario, (image.is_greyscale() && image.vertical_flip() && (image.texel(2, 3) == 0xccu)), "bottom-up greyscale texel");
        }
        case EScenario::retain_live:
        {
            m_live = result;
            return expect(scenario, ((result.live_document() != nullptr) && result.live_document()->is_ready()), "live document readiness");
        }
        case EScenario::load_live_binary:
        case EScenario::load_mutated_live_binary:
        case EScenario::load_retained_baked_binary:
        {
            return expect(scenario, document.check_integrity(), "baked integrity") &&
                expect(scenario, equal_bytes(result.byte_view(), m_expected.bytes()), "binary equality");
        }
        case EScenario::load_live_json:
        case EScenario::load_baked_json:
        {
            return expect(scenario, document.check_integrity(), "baked integrity") &&
                expect(scenario, equal_documents(document, m_expected.document()), "normalised JSON equality");
        }
        case EScenario::save_live_json_retain_baked:
        case EScenario::retain_baked:
        case EScenario::retain_baked_from_live:
        case EScenario::retain_baked_on_save_failure:
        case EScenario::retain_baked_on_json_failure:
        {
            m_baked = result;
            return expect(scenario, document.check_integrity(), "retained baked integrity") &&
                expect(scenario, equal_bytes(result.byte_view(), m_expected.bytes()), "retained baked bytes");
        }
        default:
        {
            break;
        }
    }
    return true;
}

bool CExecutiveThread::submit_concurrent_saves() noexcept
{
    using namespace asset_acceptance;
    m_phase = EPhase::concurrent;
    m_concurrent_first_slot = m_pending_slot + 1;

    //  The source stays immutable until every independently correlated save completes.
    static constexpr char path_prefix[] = "test-output:/asset-concurrent-";
    for (std::uint32_t index = 0u; index < concurrent_save_count; ++index)
    {
        char path[] = "test-output:/asset-concurrent-00.json";
        constexpr std::uint32_t digit_offset = sizeof(path_prefix) - 1u;
        path[digit_offset] = static_cast<char>('0' + (index / 10u));
        path[digit_offset + 1u] = static_cast<char>('0' + (index % 10u));
        const std::int32_t slot = m_concurrent_first_slot + static_cast<std::int32_t>(index);
        threading::CErasedOwnerMsg pending;
        if (!request_preparation::prepare_save_request(pending, slot, m_live.asset, path, save_settings(EAssetFileFormat::json)))
        {
            MV_LOG(error, "Executive: concurrent save {}: preparation failed", index);
            return false;
        }
        if (!m_context.post(std::move(pending)))
        {
            MV_LOG(error, "Executive: concurrent save {}: submission failed", index);
            return false;
        }
    }
    return true;
}

bool CExecutiveThread::complete_concurrent_save(const threading::CErasedPodMsg& message) noexcept
{
    using namespace asset_acceptance;
    const std::int32_t index = message.query_async_slot() - m_concurrent_first_slot;
    if ((index < 0) || (index >= static_cast<std::int32_t>(concurrent_save_count)))
    {
        MV_ASSERT_REPORT("Executive: concurrent reply has unexpected slot {}", message.query_async_slot());
        return false;
    }
    AssetResult result;
    if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
        (result.asset != m_live.asset) || m_save_completed[index])
    {
        MV_LOG(error, "Executive: concurrent save {}: expected a unique successful reply for the retained live asset", index);
        return false;
    }
    m_save_completed[index] = true;
    ++m_completed_save_count;
    if (m_completed_save_count == concurrent_save_count)
    {
        MV_LOG(info, "Asset acceptance: {} sequential and {} concurrent operations passed", scenario_count, concurrent_save_count);
        return start_filesystem_exercise();
    }
    return true;
}

bool CExecutiveThread::start_filesystem_exercise() noexcept
{
    threading::CErasedOwnerMsg pending;
    m_phase = EPhase::filesystem;
    const char* roots[]{ "dev-source:", "dev-workspace:", "unknown:" };
    for (std::int32_t index = 0; index < 3; ++index)
    {
        if (!request_preparation::prepare_filesystem_request(pending, (1000 + index), roots[index]) ||
            !m_context.post(std::move(pending)))
        {
            MV_LOG(error, "Executive: filesystem refresh preparation or submission failed at slot {}", (1000 + index));
            return false;
        }
    }
    const bool submitted =
        (request_preparation::prepare_load_request(pending, 1003, EAssetFileFormat::tga, "dev-source:/test_input.tga") &&
            m_context.post(std::move(pending))) &&
        (request_preparation::prepare_save_request(pending, 1004, m_baked.asset, "dev-workspace:/acceptance/filesystem-concurrent.bin", asset_acceptance::save_settings(EAssetFileFormat::baked)) &&
            m_context.post(std::move(pending))) &&
        (asset_acceptance::prepare_output_load(pending, 1005, EAssetFileFormat::json, asset_acceptance::json_file) &&
            m_context.post(std::move(pending)));
    if (!submitted && pending.has_message_type())
        MV_LOG(error, "Executive: filesystem concurrent submission failed at slot {}", pending.query_async_slot());
    return submitted;
}

bool CExecutiveThread::complete_filesystem_exercise(const threading::CErasedPodMsg& message) noexcept
{
    threading::CErasedOwnerMsg pending;
    const std::int32_t step = message.query_async_slot() - 1000;
    if (m_filesystem_count < 6u)
    {
        if ((step < 0) || (step >= 6) || m_filesystem_completed[step])
        {
            return false;
        }
        if (step < 3)
        {
            FilesystemRefreshResult result;
            if ((step != m_refresh_step) || !message.copy_payload_to(result) ||
                (result.status != ((step == 2) ? EFilesystemStatus::invalid_root : EFilesystemStatus::success)))
            {
                return false;
            }
            ++m_refresh_step;
        }
        else
        {
            AssetResult result;
            if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
                ((step == 3) && (result.asset != m_image.asset)) ||
                ((step == 4) && (result.asset != m_baked.asset)) ||
                ((step == 5) && !result.document_view().is_ready()))
            {
                return false;
            }
        }
        m_filesystem_completed[step] = true;
        if (++m_filesystem_count != 6u)
        {
            return true;
        }
        m_filesystem_step = 6;
        const auto disposal = threading::make_pod_message(1006, AssetDisposeRequest{ m_image.asset });
        return m_context.post(disposal);
    }
    if (step != m_filesystem_step++)
    {
        return false;
    }
    if (step == 6)
    {
        AssetDisposeResult result;
        return message.copy_payload_to(result) && (result.status == EAssetStatus::success) &&
            (result.asset == m_image.asset) &&
            request_preparation::prepare_load_request(pending, 1007, EAssetFileFormat::tga, "dev-source:/test_input.tga") &&
            m_context.post(std::move(pending));
    }
    AssetResult result;
    if (!message.copy_payload_to(result))
    {
        return false;
    }
    switch (step)
    {
        case 7:
        {
            if ((result.status != EAssetStatus::success) || (result.asset == m_image.asset) || !result.image_view().is_ready())
            {
                return false;
            }
            m_image = result;
            return request_preparation::prepare_load_request(pending, 1008, EAssetFileFormat::tga, "dev-source:/test_input.tga") &&
                m_context.post(std::move(pending));
        }
        case 8:
        {
            return (result.status == EAssetStatus::success) && (result.asset == m_image.asset) &&
                request_preparation::prepare_load_request(pending, 1009, EAssetFileFormat::raw, "dev-source:/filesystem-missing-file") &&
                m_context.post(std::move(pending));
        }
        case 9:
        {
            CSimpleString executive_file;
            return (result.status == EAssetStatus::read_failed) &&
                platform::module::make_module_filename("package:/bin/MorphicExecutive", executive_file) &&
                request_preparation::prepare_save_request(pending, 1010, m_raw.asset, executive_file.cstring(), asset_acceptance::save_settings(EAssetFileFormat::raw)) &&
                m_context.post(std::move(pending));
        }
        case 10:
        {
            return (result.status == EAssetStatus::write_failed) &&
                request_preparation::prepare_load_request(pending, 1011, EAssetFileFormat::baked, "dev-workspace:/acceptance/filesystem-concurrent.bin") &&
                m_context.post(std::move(pending));
        }
        case 11:
        {
            if ((result.status != EAssetStatus::success) || (result.asset != m_baked.asset))
            {
                return false;
            }
            MV_LOG(info, "Filesystem acceptance: 12 queued refresh, concurrent access and cache operations passed");
            return start_bundle_exercise();
        }
        default: return false;
    }
}

bool CExecutiveThread::start_bundle_exercise() noexcept
{
    threading::CErasedOwnerMsg pending;
    m_phase = EPhase::bundle;
    return request_preparation::prepare_bundle_bake_request(pending, 2000, "dev-source:", "bundle-acceptance:") &&
        m_context.post(std::move(pending));
}

bool CExecutiveThread::complete_bundle_exercise(const threading::CErasedPodMsg& message) noexcept
{
    threading::CErasedOwnerMsg pending;
    if ((m_bundle_step == 4) || (m_bundle_step == 6))
    {
        BundleDisposedNotification notification;
        if ((message.query_async_slot() != threading::k_uncorrelated_slot) || !message.copy_payload_to(notification) ||
            (notification.asset != m_bundle.asset)) return false;
        m_bundle = {};
        if (m_bundle_step == 6)
        {
            m_bundle_snapshot.deallocate();
            m_phase = EPhase::complete;
            MV_LOG(info, "Bundle acceptance: bake, reference, save, close and reload passed");
            return true;
        }
        ++m_bundle_step;
        return request_preparation::prepare_bundle_load_request(pending, 2005,
            "dev-workspace:/acceptance/bundle-acceptance.bdl", "bundle-acceptance:") &&
            m_context.post(std::move(pending));
    }
    if (message.query_async_slot() != 2000 + m_bundle_step) return false;
    if (m_bundle_step == 2)
    {
        AssetDisposeResult result;
        if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success)) return false;
        ++m_bundle_step;
        return request_preparation::prepare_save_request(pending, 2003, m_bundle.asset, "dev-workspace:/acceptance/bundle-acceptance.bdl", asset_acceptance::save_settings(EAssetFileFormat::raw)) &&
            m_context.post(std::move(pending));
    }
    AssetResult result;
    if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
        (result.kind != EAssetKind::bundle) || !result.bundle_view().is_ready()) return false;
    threading::CErasedPodMsg outbound;
    switch (m_bundle_step++)
    {
        case 0:
        {
            m_bundle = result;
            const auto bytes = result.byte_view();
            if (!m_bundle_snapshot.allocate(bytes.size()) || !m_bundle_snapshot.set_size(bytes.size()) ||
                (result.bundle_view().lookup(CStringView{ "test_input.tga" }).status != bundle::ELookupStatus::success)) return false;
            std::memcpy(m_bundle_snapshot.data(), bytes.data(), bytes.size());
            outbound = threading::make_pod_message(2001, BundleReferenceRequest{ m_bundle.asset });
            break;
        }
        case 1:
        {
            if ((result.asset == m_bundle.asset) || (result.byte_view().data() != m_bundle.byte_view().data())) return false;
            outbound = threading::make_pod_message(2002, AssetDisposeRequest{ result.asset });
            break;
        }
        case 3:
        {
            if (result.asset != m_bundle.asset) return false;
            outbound = threading::make_pod_message(2004, AssetDisposeRequest{ m_bundle.asset });
            break;
        }
        case 5:
        {
            if (!asset_acceptance::equal_bytes(result.byte_view(), m_bundle_snapshot.const_view())) return false;
            m_bundle = result;
            outbound = threading::make_pod_message(2006, AssetDisposeRequest{ m_bundle.asset });
            break;
        }
        default: return false;
    }
    return m_context.post(outbound);
}

bool CExecutiveThread::request_system_shutdown() noexcept
{
    threading::CErasedOwnerMsg pending;
    if (!request_preparation::prepare_module_request(pending, threading::k_uncorrelated_slot,
        module_ids::executive, EModuleAction::unload) || !m_context.post(std::move(pending)))
    {
        return false;
    }
    m_phase = EPhase::awaiting_exit;
    (void)m_perf_counter.update();
    return true;
}

void CExecutiveThread::operate() noexcept
{
    using namespace asset_acceptance;
    const std::uint64_t ticks_per_second = m_context.perf_count_conversion().query_ticks_per_second();
    while (!m_context.exit_requested() && (m_phase != EPhase::complete) && (m_failure_code == 0u))
    {
        threading::CErasedPodMsg message;
        if (m_context.read(message))
        {
            if (m_phase == EPhase::awaiting_exit)
            {
                continue;
            }
            if ((m_phase == EPhase::rendering_acknowledgement) || (m_phase == EPhase::rendering_completion))
            {
                if (!accept_rendering_notice(message))
                {
                    MV_LOG(error, "Executive: Rendering unavailable or invalid reply; requesting system shutdown");
                    if (!request_system_shutdown())
                    {
                        fail(EFailure::submission, "system shutdown request");
                    }
                }
                else if (m_phase == EPhase::module_tests)
                {
                    const auto request = threading::make_pod_message(m_pending_slot, RunTestsRequest{ module_ids::render_vulkan });
                    if (!m_context.post(request)) fail(EFailure::submission, "module test request submission");
                }
                continue;
            }
            if (m_phase == EPhase::module_tests)
            {
                RunTestsResult result;
                if ((message.query_async_slot() != m_pending_slot) || !message.copy_payload_to(result) ||
                    (result.module != module_ids::render_vulkan) || (result.status != ETestRunStatus::success) ||
                    (result.failed != 0u) || (result.passed == 0u))
                {
                    fail(EFailure::completion, "module tests");
                    continue;
                }
                MV_LOG(info, "Executive: Module tests completed: {} passed, {} failed, {} skipped",
                    result.passed, result.failed, result.skipped);
                m_phase = EPhase::sequential;
                ++m_pending_slot;
                (void)m_perf_counter.update();
                threading::CErasedOwnerMsg pending;
                if (!prepare_scenario(pending, scenarios[m_scenario_index], m_pending_slot))
                    fail(EFailure::submission, "initial asset request preparation");
                else if (!m_context.post(std::move(pending)))
                    fail(EFailure::submission, "initial asset request submission");
                continue;
            }
            ModuleResult module_result;
            if (message.copy_payload_to(module_result))
            {
                MV_DETAIL("Executive module notification");
                //  No retry policy yet: failure of another DLL's operation requests
                //  our own termination, which the Host interprets as system shutdown.
                if ((module_result.notice == EModuleNotice::completed) && (module_result.status != EModuleStatus::success) &&
                    !request_system_shutdown())
                {
                    fail(EFailure::submission, "system shutdown request");
                }
                continue;
            }
            if (m_phase == EPhase::bundle)
            {
                if (!complete_bundle_exercise(message)) fail(EFailure::completion, "bundle exercise");
                continue;
            }
            if (m_phase == EPhase::filesystem)
            {
                if (!complete_filesystem_exercise(message))
                {
                    fail(EFailure::completion, "filesystem image exercise");
                }
                continue;
            }
            if (m_phase == EPhase::concurrent)
            {
                if (!complete_concurrent_save(message))
                {
                    fail(EFailure::completion, "concurrent completion");
                }
                continue;
            }
            const SScenario& scenario = scenarios[m_scenario_index];
            AssetResult result;
            if (!expect(scenario, (message.query_async_slot() == m_pending_slot), "reply correlation") ||
                !expect(scenario, message.copy_payload_to(result), "reply payload type") || !check(scenario, result))
            {
                fail(EFailure::completion, "scenario completion");
                break;
            }
            MV_LOG_RICH(info, "Executive: %s passed", scenario.name);
            ++m_scenario_index;
            (void)m_perf_counter.update();
            if (m_scenario_index == scenario_count)
            {
                if (!submit_concurrent_saves())
                {
                    fail(EFailure::submission, "concurrent request submission");
                }
            }
            else
            {
                ++m_pending_slot;
                threading::CErasedOwnerMsg pending;
                if (!prepare_scenario(pending, scenarios[m_scenario_index], m_pending_slot))
                    fail(EFailure::submission, "request preparation");
                else if (!m_context.post(std::move(pending)))
                    fail(EFailure::submission, "request submission");
            }
        }
        else if (m_perf_counter.query_delta() > (ticks_per_second * 30u))
        {
            fail(EFailure::timeout, "operation deadline");
        }
        else
        {
            m_context.advance_heartbeat();
            platform::threading::processor_relax();
        }
    }
}

void CExecutiveThread::shutdown() noexcept
{
    if (m_failure_code == 0u)
    {
        m_context.mark_exiting();
        m_context.mark_exited();
        MV_INFO("Executive: Exited");
    }
}

void CExecutiveThread::fail(const EFailure failure, const char* const reason) noexcept
{
    if (m_failure_code == 0u)
    {
        m_failure_code = static_cast<std::uint32_t>(failure);
        const char* const scenario = ((m_phase == EPhase::rendering_acknowledgement) || (m_phase == EPhase::rendering_completion)) ?
            "rendering_startup" : (m_phase == EPhase::module_tests) ? "module_tests" : ((m_phase == EPhase::filesystem) ? "filesystem" :
                ((m_phase == EPhase::bundle) ? "bundle" : ((m_scenario_index < asset_acceptance::scenario_count) ?
                    asset_acceptance::scenarios[m_scenario_index].name : "concurrent_saves")));
        MV_LOG_RICH(error, "Executive: %s failed: %s (scenario slot %d, filesystem step %d, bundle step %d)",
            scenario, reason, m_pending_slot, m_filesystem_step, m_bundle_step);
        m_context.mark_failed(m_failure_code);
    }
}

FExecutiveThread executive_thread_entry_point() noexcept
{
    return &CExecutiveThread::entry_point;
}

}   //  namespace executive

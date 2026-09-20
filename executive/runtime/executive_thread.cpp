
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    executive_thread.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    7 Aug 26
//
//  Executive thread and asynchronous asset-service acceptance exercise.

#include <cstdint>      //  std::int32_t, std::uint32_t, std::uint64_t
#include <cstring>      //  std::strcmp
#include <utility>      //  std::move

#include "executive/runtime/executive_thread.hpp"
#include "module/module_binding_context.hpp"

#include "debug/macros.hpp"
#include "image/codec/tga.hpp"
#include "data_model/document_parser.hpp"
#include "data_model/document_translation.hpp"
#include "data_model/document_writer.hpp"
#include "platform/system/performance_counter.hpp"
#include "platform/threading/processor_relax.hpp"
#include "system/system_id_registry.hpp"
#include "system/transported_types.hpp"
#include "threading/CThreadPackage.hpp"

namespace executive
{

namespace asset_acceptance
{

enum class EScenario : std::uint8_t
{
    retain_raw,
    save_retained_raw,
    load_saved_raw,
    save_discarded_raw,
    load_discarded_raw_output,
    load_tga,
    save_retained_image,
    reload_tga,
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
    { EScenario::retain_raw,                   "retain_raw",                   EAssetStatus::success,             EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_raw,            "save_retained_raw",            EAssetStatus::success,             EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_saved_raw,               "load_saved_raw",               EAssetStatus::success,             EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_raw,           "save_discarded_raw",           EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_discarded_raw_output,    "load_discarded_raw_output",    EAssetStatus::success,             EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_tga,                     "load_tga",                     EAssetStatus::success,             EAssetKind::image, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_image,          "save_retained_image",          EAssetStatus::success,             EAssetKind::image, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reload_tga,                   "reload_tga",                   EAssetStatus::success,             EAssetKind::image, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_greyscale,      "save_retained_greyscale",      EAssetStatus::success,             EAssetKind::image, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_rgba,          "save_discarded_rgba",          EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_live,                  "retain_live",                  EAssetStatus::success,             EAssetKind::live,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_binary,             "save_live_binary",             EAssetStatus::success,             EAssetKind::live,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_live_binary,             "load_live_binary",             EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_json,               "save_live_json",               EAssetStatus::success,             EAssetKind::live,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_live_json,               "load_live_json",               EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::accepted,        0u },
    { EScenario::mutate_live_and_save_binary,  "mutate_live_and_save_binary",  EAssetStatus::success,             EAssetKind::live,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_mutated_live_binary,     "load_mutated_live_binary",     EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_live_json_retain_baked,  "save_live_json_retain_baked",  EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_retained_baked_binary,   "save_retained_baked_binary",   EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_retained_baked_binary,   "load_retained_baked_binary",   EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_baked_json,    "save_discarded_baked_json",    EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_baked_json,              "load_baked_json",              EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::accepted,        0u },
    { EScenario::retain_baked,                 "retain_baked",                 EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_from_live,       "retain_baked_from_live",       EAssetStatus::success,             EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_raw_on_save_failure,   "retain_raw_on_save_failure",   EAssetStatus::write_failed,        EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_raw_after_failure,       "save_raw_after_failure",       EAssetStatus::success,             EAssetKind::raw,   EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::discard_raw_on_save_failure,  "discard_raw_on_save_failure",  EAssetStatus::write_failed,        EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_on_save_failure, "retain_baked_on_save_failure", EAssetStatus::write_failed,        EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_asset,         "reject_invalid_asset",         EAssetStatus::invalid_asset,       EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_malformed_json,         "write_malformed_json",         EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_malformed_json,        "reject_malformed_json",        EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      document_finding_bit(EDocumentFinding::unquoted_names) },
    { EScenario::reject_missing_file,          "reject_missing_file",          EAssetStatus::read_failed,         EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_policy_rejected_json,   "write_policy_rejected_json",   EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_disallowed_features,   "reject_disallowed_features",   EAssetStatus::policy_rejected,     EAssetKind::none,  EDocumentPolicyStatus::rejected,        document_finding_bit(EDocumentFinding::unquoted_names) },
    { EScenario::write_invalid_binary,         "write_invalid_binary",         EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_binary,        "reject_invalid_binary",        EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::retain_baked_on_json_failure, "retain_baked_on_json_failure", EAssetStatus::conditioning_failed, EAssetKind::baked, EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_live_binary,   "save_discarded_live_binary",   EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::save_discarded_live_json,     "save_discarded_live_json",     EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_tga,           "reject_invalid_tga",           EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::write_invalid_encoding,       "write_invalid_encoding",       EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_encoding,      "reject_invalid_encoding",      EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      (EDocumentFinding::utf8_attempt_failed | EDocumentFinding::cp1252) },
    { EScenario::write_numeric_overflow,       "write_numeric_overflow",       EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_numeric_overflow,      "reject_numeric_overflow",      EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_invalid_policy,        "reject_invalid_policy",        EAssetStatus::policy_rejected,     EAssetKind::none,  EDocumentPolicyStatus::invalid_options, 0u },
    { EScenario::write_unterminated_string,    "write_unterminated_string",    EAssetStatus::success,             EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::reject_unterminated_string,   "reject_unterminated_string",   EAssetStatus::conditioning_failed, EAssetKind::none,  EDocumentPolicyStatus::unexamined,      0u },
    { EScenario::load_tga_bottom_up,           "load_tga_bottom_up",           EAssetStatus::success,             EAssetKind::image, EDocumentPolicyStatus::unexamined,      0u },
};
static constexpr std::uint32_t scenario_count = sizeof(scenarios) / sizeof(scenarios[0]);
static constexpr std::uint32_t concurrent_save_count{ 32u };
static_assert(concurrent_save_count <= 100u, "Concurrent filenames reserve two decimal digits.");

static constexpr char fixture[] = R"({"null":null,"bool":true,"signed":-123,"unsigned":18446744073709551615,"float":1.25,"text":"line\ntext","object":{},"mixed":[null,true,3,"text"],"bools":[true,false],"ints":[-1,-2],"uints":[18446744073709551615,18446744073709551614],"floats":[1.25,2.5],"strings":["a","b"],"empty":[]})";
static constexpr char raw_file[] = "build/asset-acceptance.raw";
static constexpr char binary_file[] = "build/asset-acceptance.bin";
static constexpr char json_file[] = "build/asset-acceptance.json";
static constexpr char image_file[] = "build/asset-acceptance.tga";
static constexpr char failure_file[] = "build/asset-acceptance-missing-directory/output.bin";

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
        MV_REPORT("Executive: %s: failed expectation: %s", scenario.name, property);
    }
    return satisfied;
}

}   //  namespace asset_acceptance

class CExecutiveThread
{
public:
    static std::uint32_t MV_STD_ABI_CALL entry_point(void* const user_data) noexcept;

private:
    enum class EPhase : std::uint8_t { sequential, concurrent, complete };
    enum class EFailure : std::uint32_t { registry = 1u, timing, submission, completion, timeout };
    CExecutiveThread(const CExecutiveThread&) noexcept = delete;
    CExecutiveThread& operator=(const CExecutiveThread&) noexcept = delete;
    CExecutiveThread(CExecutiveThread&&) noexcept = delete;
    CExecutiveThread& operator=(CExecutiveThread&&) noexcept = delete;
    explicit CExecutiveThread(threading::CThreadResources& resources) noexcept : m_context{ resources } {}
    ~CExecutiveThread() noexcept = default;

    std::uint32_t main() noexcept;
    [[nodiscard]] bool startup() noexcept;
    [[nodiscard]] bool initialise() noexcept;
    void operate() noexcept;
    [[nodiscard]] bool submit(const asset_acceptance::SScenario& scenario, const std::int32_t slot) noexcept;
    [[nodiscard]] bool check(const asset_acceptance::SScenario& scenario, const AssetResult& result) noexcept;
    template<typename T>
    [[nodiscard]] bool post(const std::int32_t slot, CErasedOwner&& owner, const T* const request) noexcept;
    [[nodiscard]] bool load(const std::int32_t slot, const EAssetFileFormat format, const char* const file, const CDocumentParseOptions policy = {}, const bool decode_top_down = true) noexcept;
    [[nodiscard]] bool save(const std::int32_t slot, const CAssetId asset, const char* const file, const AssetSaveSettings& settings) noexcept;
    [[nodiscard]] bool transfer_raw(const std::int32_t slot, const char* const text, const EAssetRetention retention, const bool save, const char* const file) noexcept;
    [[nodiscard]] bool transfer_image(const std::int32_t slot, const bool greyscale, const bool storage_bottom_up, const EAssetRetention retention, const char* const file) noexcept;
    [[nodiscard]] bool transfer_live(const std::int32_t slot, const EAssetRetention retention, const bool save, const char* const file, const AssetSaveSettings& settings) noexcept;
    [[nodiscard]] bool transfer_baked(const std::int32_t slot, const EAssetRetention retention, const bool save, const char* const file, const AssetSaveSettings& settings) noexcept;
    [[nodiscard]] bool prepare_expected_document() noexcept;
    [[nodiscard]] bool submit_concurrent_saves() noexcept;
    [[nodiscard]] bool complete_concurrent_save(const threading::CErasedPodMsg& message) noexcept;
    void shutdown() noexcept;
    void fail(const EFailure failure, const char* const reason) noexcept;

    threading::CThreadContext m_context;
    platform::system::CPerfCounter m_perf_counter;
    std::uint32_t m_scenario_index{ 0u };
    std::int32_t m_pending_slot{ 0 };
    std::int32_t m_concurrent_first_slot{ 0 };
    EPhase m_phase{ EPhase::sequential };
    AssetResult m_raw;
    AssetResult m_image;
    image::CImageView m_image_view;
    AssetResult m_live;
    AssetResult m_baked;
    CBakedDocumentBlock m_expected;
    bool m_save_completed[asset_acceptance::concurrent_save_count]{};
    std::uint32_t m_completed_save_count{ 0u };
    std::uint32_t m_failure_code{ 0u };
};

std::uint32_t MV_STD_ABI_CALL CExecutiveThread::entry_point(void* const user_data) noexcept
{
    if (user_data == nullptr)
    {
        MV_ERROR("CExecutiveThread entry received a null user_data pointer");
        return ~0u;
    }

    threading::CThreadResources& resources = *static_cast<threading::CThreadResources*>(user_data);
    if (!modules::is_thread_context_ready(user_data))
    {
        resources.control_state.mark_failed(~0u);
        MV_ERROR("CExecutiveThread entry detected incomplete module context installation");
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
    const char* const registry_name = system_id_registry::lookup_type_name(system_type_ids::file_load_request);
    if ((registry_name == nullptr) || (std::strcmp(registry_name, "file_load_request") != 0))
    {
        fail(EFailure::registry, "system registry lookup");
        return false;
    }

    MV_REPORT("Executive system registry authority: %s", registry_name);

    if (!m_perf_counter.update() ||
        !m_context.perf_count_conversion().is_valid())
    {
        fail(EFailure::timing, "performance counter initialisation");
        return false;
    }

    if (!submit(asset_acceptance::scenarios[m_scenario_index], m_pending_slot))
    {
        fail(EFailure::submission, "initial request submission");
        return false;
    }
    return true;
}

template<typename T>
bool CExecutiveThread::post(const std::int32_t slot, CErasedOwner&& owner, const T* const request) noexcept
{
    if (!owner || (request == nullptr))
    {
        return false;
    }
    threading::CErasedOwnerMsg message;
    message.set_message_type<T>();
    message.set_async_slot(slot);
    message.set_owner(std::move(owner));
    return m_context.post(std::move(message));
}

bool CExecutiveThread::load(const std::int32_t slot, const EAssetFileFormat format, const char* const file,
    const CDocumentParseOptions policy, const bool decode_top_down) noexcept
{
    CErasedOwner owner = CErasedOwner::create<AssetLoadRequest>();
    AssetLoadRequest* const request = owner.payload<AssetLoadRequest>();
    if ((request == nullptr) || !request->file.set(file))
    {
        return false;
    }
    request->format = format;
    request->alignment = 32u;
    request->policy = policy;
    request->decode_top_down = decode_top_down;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::save(const std::int32_t slot, const CAssetId asset, const char* const file, const AssetSaveSettings& settings) noexcept
{
    CErasedOwner owner = CErasedOwner::create<AssetSaveRequest>();
    AssetSaveRequest* const request = owner.payload<AssetSaveRequest>();
    if ((request == nullptr) || !request->file.set(file))
    {
        return false;
    }
    request->source = asset;
    request->settings = settings;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::transfer_raw(const std::int32_t slot, const char* const text,
    const EAssetRetention retention, const bool save, const char* const file) noexcept
{
    CErasedOwner owner = CErasedOwner::create<RawAssetTransfer>();
    RawAssetTransfer* const request = owner.payload<RawAssetTransfer>();
    const std::size_t size = std::strlen(text);
    if ((request == nullptr) || !request->storage.value.resize(size) || (save && !request->storage.file.set(file)))
    {
        return false;
    }
    std::memcpy(request->storage.value.data(), text, size);
    request->settings.format = EAssetFileFormat::raw;
    request->save = save;
    request->retention = retention;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::transfer_image(const std::int32_t slot, const bool greyscale, const bool storage_bottom_up,
    const EAssetRetention retention, const char* const file) noexcept
{
    CErasedOwner owner = CErasedOwner::create<ImageAssetTransfer>();
    ImageAssetTransfer* const request = owner.payload<ImageAssetTransfer>();
    if ((request == nullptr) || !request->storage.value.allocate(greyscale ? 7u : 28u, 5u, 16u) ||
        !request->storage.file.set(file))
    {
        return false;
    }
    request->description = greyscale ? image::codec::tga::decoded_image_desc::Gray : image::codec::tga::decoded_image_desc::RGBA;
    request->storage_bottom_up = storage_bottom_up;
    const image::CImageView view{ request->storage.value.view(), request->description, storage_bottom_up };
    view.fill(0x7f345678u);
    view.plot(2, 3, 0xffaabbccu);
    request->settings.format = EAssetFileFormat::tga;
    request->settings.image = view.encode_options();
    request->save = true;
    request->retention = retention;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::transfer_live(const std::int32_t slot, const EAssetRetention retention, const bool save,
    const char* const file, const AssetSaveSettings& settings) noexcept
{
    CErasedOwner owner = CErasedOwner::create<LiveAssetTransfer>();
    LiveAssetTransfer* const request = owner.payload<LiveAssetTransfer>();
    if ((request == nullptr) || !asset_acceptance::fixture_document(request->storage.value) ||
        (save && !request->storage.file.set(file)))
    {
        return false;
    }
    request->retention = retention;
    request->save = save;
    request->settings = settings;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::transfer_baked(const std::int32_t slot, const EAssetRetention retention, const bool save,
    const char* const file, const AssetSaveSettings& settings) noexcept
{
    CErasedOwner owner = CErasedOwner::create<BakedAssetTransfer>();
    BakedAssetTransfer* const request = owner.payload<BakedAssetTransfer>();
    CLiveDocument document;
    if ((request == nullptr) || !asset_acceptance::fixture_document(document) ||
        !document_translation::bake(document, request->storage.value) || (save && !request->storage.file.set(file)))
    {
        return false;
    }
    request->save = save;
    request->retention = retention;
    request->settings = settings;
    return post(slot, std::move(owner), request);
}

bool CExecutiveThread::prepare_expected_document() noexcept
{
    CLiveDocument document;
    return asset_acceptance::fixture_document(document) && document_translation::bake(document, m_expected);
}

bool CExecutiveThread::submit(const asset_acceptance::SScenario& scenario, const std::int32_t slot) noexcept
{
    using namespace asset_acceptance;
    switch (scenario.identity)
    {
        case EScenario::retain_raw:
        {
            return transfer_raw(slot, fixture, EAssetRetention::source, false, nullptr);
        }
        case EScenario::save_retained_raw:
        case EScenario::save_raw_after_failure:
        {
            return save(slot, m_raw.asset, raw_file, save_settings(EAssetFileFormat::raw));
        }
        case EScenario::load_saved_raw:
        case EScenario::load_discarded_raw_output:
        {
            return load(slot, EAssetFileFormat::raw, raw_file);
        }
        case EScenario::save_discarded_raw:
        case EScenario::write_invalid_binary:
        {
            return transfer_raw(slot, fixture, EAssetRetention::discard, true, raw_file);
        }
        case EScenario::load_tga:
        {
            return load(slot, EAssetFileFormat::tga, "test_data/input/files/test_input.tga");
        }
        case EScenario::load_tga_bottom_up:
        {
            return load(slot, EAssetFileFormat::tga, "test_data/input/files/test_input.tga", {}, false);
        }
        case EScenario::save_retained_image:
        {
            AssetSaveSettings settings = save_settings(EAssetFileFormat::tga);
            settings.image = m_image_view.encode_options();
            return save(slot, m_image.asset, image_file, settings);
        }
        case EScenario::reload_tga:
        {
            return load(slot, EAssetFileFormat::tga, image_file);
        }
        case EScenario::save_retained_greyscale:
        {
            return transfer_image(slot, true, true, EAssetRetention::source, image_file);
        }
        case EScenario::save_discarded_rgba:
        {
            return transfer_image(slot, false, false, EAssetRetention::discard, image_file);
        }
        case EScenario::retain_live:
        {
            return prepare_expected_document() &&
                transfer_live(slot, EAssetRetention::source, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_live_binary:
        {
            return save(slot, m_live.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::load_live_binary:
        case EScenario::load_mutated_live_binary:
        case EScenario::load_retained_baked_binary:
        {
            return load(slot, EAssetFileFormat::baked, binary_file);
        }
        case EScenario::save_live_json:
        {
            return save(slot, m_live.asset, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::load_live_json:
        case EScenario::load_baked_json:
        {
            return load(slot, EAssetFileFormat::json, json_file);
        }
        case EScenario::mutate_live_and_save_binary:
        {   //  The preceding read has completed; the next save must bake this mutation.
            CLiveDocument* const live = m_live.live_document();
            if ((live == nullptr) || !live->append_child(live->root(), live->create_boolean(false,
                CStringView{ reinterpret_cast<const std::uint8_t*>("added"), 5u })).succeeded() ||
                !document_translation::bake(*live, m_expected))
            {
                return false;
            }
            return save(slot, m_live.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_live_json_retain_baked:
        {
            return prepare_expected_document() &&
                transfer_live(slot, EAssetRetention::baked, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::save_retained_baked_binary:
        {
            return save(slot, m_baked.asset, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_discarded_baked_json:
        {
            return transfer_baked(slot, EAssetRetention::discard, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::retain_baked:
        {
            return transfer_baked(slot, EAssetRetention::source, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::retain_baked_from_live:
        {
            return transfer_live(slot, EAssetRetention::baked, false, nullptr, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::retain_raw_on_save_failure:
        {
            return transfer_raw(slot, fixture, EAssetRetention::source, true, failure_file);
        }
        case EScenario::discard_raw_on_save_failure:
        {
            return transfer_raw(slot, fixture, EAssetRetention::discard, true, failure_file);
        }
        case EScenario::retain_baked_on_save_failure:
        {
            return transfer_live(slot, EAssetRetention::baked, true, failure_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::reject_invalid_asset:
        {
            return save(slot, CAssetId{}, raw_file, save_settings(EAssetFileFormat::raw));
        }
        case EScenario::write_malformed_json:
        {
            return transfer_raw(slot, "{broken", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_malformed_json:
        case EScenario::reject_invalid_encoding:
        case EScenario::reject_numeric_overflow:
        case EScenario::reject_unterminated_string:
        {
            return load(slot, EAssetFileFormat::json, raw_file);
        }
        case EScenario::reject_missing_file: return load(slot, EAssetFileFormat::raw, failure_file);
        case EScenario::write_policy_rejected_json:
        {
            return transfer_raw(slot, "{unquoted:1}", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_disallowed_features:
        {
            return load(slot, EAssetFileFormat::json, raw_file, CDocumentParseOptions{ document_policy::k_ascii });
        }
        case EScenario::reject_invalid_binary:
        {
            return load(slot, EAssetFileFormat::baked, raw_file);
        }
        case EScenario::retain_baked_on_json_failure:
        {
            AssetSaveSettings settings = save_settings(EAssetFileFormat::json);
            settings.document.mode = static_cast<EDocumentWriteMode>(255u);
            return transfer_live(slot, EAssetRetention::baked, true, json_file, settings);
        }
        case EScenario::save_discarded_live_binary:
        {
            return transfer_live(slot, EAssetRetention::discard, true, binary_file, save_settings(EAssetFileFormat::baked));
        }
        case EScenario::save_discarded_live_json:
        {
            return transfer_live(slot, EAssetRetention::discard, true, json_file, save_settings(EAssetFileFormat::json));
        }
        case EScenario::reject_invalid_tga:
        {
            return load(slot, EAssetFileFormat::tga, raw_file);
        }
        case EScenario::write_invalid_encoding:
        {
            return transfer_raw(slot, "\x81", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::write_numeric_overflow:
        {
            return transfer_raw(slot, "18446744073709551616", EAssetRetention::discard, true, raw_file);
        }
        case EScenario::reject_invalid_policy:
        {
            return load(slot, EAssetFileFormat::json, json_file, CDocumentParseOptions{ 0x80000000u });
        }
        case EScenario::write_unterminated_string:
        {
            return transfer_raw(slot, "\"unterminated", EAssetRetention::discard, true, raw_file);
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
        MV_REPORT("Executive: %s: status %u/%u, kind %u/%u, policy %u/%u, findings 0x%08x/0x%08x (actual/expected)",
            scenario.name, static_cast<unsigned int>(result.status), static_cast<unsigned int>(scenario.status),
            static_cast<unsigned int>(result.kind), static_cast<unsigned int>(scenario.retained_kind),
            static_cast<unsigned int>(result.document_policy), static_cast<unsigned int>(scenario.policy),
            result.document_findings, scenario.findings);
        return false;
    }
    if (!expect(scenario, static_cast<bool>(result.asset) == (scenario.retained_kind != EAssetKind::none), "retained asset identity"))
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
                reinterpret_cast<const std::uint8_t*>(fixture), sizeof(fixture) - 1u }), "raw byte equality") &&
                expect(scenario, result.byte_view().align() >= 32u, "raw alignment");
        }
        case EScenario::load_tga:
        {
            m_image = result;
            m_image_view = image;
            return expect(scenario, image.is_ready() && !image.is_read_only() && !image.vertical_flip(), "writable top-down image");
        }
        case EScenario::reload_tga:
        case EScenario::load_tga_bottom_up:
        {
            if (!expect(scenario, (image.width() == m_image_view.width()) &&
                (image.height() == m_image_view.height()), "image dimensions") ||
                !expect(scenario, image.vertical_flip() == (scenario.identity == EScenario::load_tga_bottom_up), "image row addressing"))
            {
                return false;
            }
            for (std::int32_t y = 0; y < image.height(); ++y)
            {
                for (std::int32_t x = 0; x < image.width(); ++x)
                {
                    if (!expect(scenario, image.texel(x, y) == m_image_view.texel(x, y), "image texel equality"))
                    {
                        return false;
                    }
                }
            }
            break;
        }
        case EScenario::save_retained_greyscale:
        {
            return expect(scenario, image.is_greyscale() && image.vertical_flip() && (image.texel(2, 3) == 0xccu), "bottom-up greyscale texel");
        }
        case EScenario::retain_live:
        {
            m_live = result;
            return expect(scenario, (result.live_document() != nullptr) && result.live_document()->is_ready(), "live document readiness");
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
    static constexpr char path_prefix[] = "build/asset-concurrent-";
    for (std::uint32_t index = 0u; index < concurrent_save_count; ++index)
    {
        char path[] = "build/asset-concurrent-00.json";
        constexpr std::uint32_t digit_offset = sizeof(path_prefix) - 1u;
        path[digit_offset] = static_cast<char>('0' + (index / 10u));
        path[digit_offset + 1u] = static_cast<char>('0' + (index % 10u));
        const std::int32_t slot = m_concurrent_first_slot + static_cast<std::int32_t>(index);
        if (!save(slot, m_live.asset, path, save_settings(EAssetFileFormat::json)))
        {
            MV_REPORT("Executive: concurrent save %u: submission failed", index);
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
        MV_REPORT("Executive: concurrent reply has unexpected slot %d", message.query_async_slot());
        return false;
    }
    AssetResult result;
    if (!message.copy_payload_to(result) || (result.status != EAssetStatus::success) ||
        (result.asset != m_live.asset) || m_save_completed[index])
    {
        MV_REPORT("Executive: concurrent save %d: expected a unique successful reply for the retained live asset", index);
        return false;
    }
    m_save_completed[index] = true;
    ++m_completed_save_count;
    if (m_completed_save_count == concurrent_save_count)
    {
        m_phase = EPhase::complete;
        MV_REPORT("Asset acceptance: %u sequential and %u concurrent operations passed", scenario_count, concurrent_save_count);
    }
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
            if (!expect(scenario, message.query_async_slot() == m_pending_slot, "reply correlation") ||
                !expect(scenario, message.copy_payload_to(result), "reply payload type") || !check(scenario, result))
            {
                fail(EFailure::completion, "scenario completion");
                break;
            }
            MV_REPORT("Executive: %s passed", scenario.name);
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
                if (!submit(scenarios[m_scenario_index], m_pending_slot))
                {
                    fail(EFailure::submission, "request submission");
                }
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
        const char* const scenario = (m_scenario_index < asset_acceptance::scenario_count) ?
            asset_acceptance::scenarios[m_scenario_index].name : "concurrent_saves";
        MV_REPORT("Executive: %s failed: %s", scenario, reason);
        m_context.mark_failed(m_failure_code);
    }
}

FExecutiveThread executive_thread_entry_point() noexcept
{
    return &CExecutiveThread::entry_point;
}

}   //  namespace executive

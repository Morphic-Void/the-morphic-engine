
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PlatformReport_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Nov 26
//
//  Exercises capture deadlines, backpressure and malformed or incoherent report rejection.

#include "platform/runtime/report_capture.hpp"
#include "framework/platform/report_validation.hpp"
#include "framework/threading/messages/message_helpers.hpp"
#include "tests/support/test_context.hpp"
#include "tests/support/endpoint_fixture.hpp"
#include "tests/support/test_allocator.hpp"
#include "tests/support/test_scopes.hpp"
#include "host/runtime/host.hpp"

namespace
{

using namespace platform::reports;
using namespace platform_report_v1;
using platform::native::EReportStep;
using services::EServiceStatus;

static bool limitation(platform::runtime::SReportCandidate& candidate, const std::uint64_t end) noexcept
{
    PlatformReportHeader& header = candidate.header;
    header.capture_end_ns = end;
    header.discovery = PlatformDiscovery::unsupported;
    header.coverage = PlatformCoverage::limited;
    header.inventory_query_id = 1u;
    header.consistency_query_id = 2u;
    header.adapters = header.sources = header.displays = header.paths = header.mode_catalogues = { -1, 0, 0u };
    header.queries = { 1, 0, 2u };
    PlatformQueryResult q{};
    q.id = 1u;
    q.kind = PlatformQueryKind::inventory;
    q.api = PlatformQueryApi::windows_ccd;
    q.subject_kind = PlatformSubjectKind::report;
    q.status = PlatformQueryStatus::unsupported;
    q.reason = PlatformQueryReason::api_not_supported;
    q.error_domain = PlatformErrorDomain::win32;
    q.native_error = 50u;
    q.native_flags = 0x51u;
    q.attempts = q.call_count = 1u;
    q.diagnostic.string_value_id = -1;
    if (!candidate.queries.push_back(q)) return false;
    q.id = 2u;
    q.kind = PlatformQueryKind::consistency_check;
    q.status = PlatformQueryStatus::blocked;
    q.reason = PlatformQueryReason::dependency_missing;
    q.blocked_by_query_id = 1u;
    q.error_domain = PlatformErrorDomain::none;
    q.native_error = q.native_flags = q.attempts = q.call_count = 0u;
    return candidate.queries.push_back(q);
}

struct SProbe
{
    tests::TTestContext& tests;
    std::uint64_t now{ 100u };
    bool clock_failed{}, retry{}, busy{};
    unsigned calls{}, posts{};
    const void* first_offer{};
    SFailure failure;
    threading::CErasedOwnerMsg accepted;
    modules::CHostRestrictionPublication restrictions;
    modules::SHostExecutionContext execution;
    static const modules::SCommunicationV1 functions;
    const modules::CCommunication communication{ functions, this };
    modules::SExecutionContext context;
    const platform::threading::SAwakeClock clock{ this, &read };
    const platform::native::SReportAdapter adapter{ &step };
    TInstance<platform::runtime::CReportCapture> capture;

    explicit SProbe(tests::TTestContext& tests) noexcept : tests{ tests }
    {
        modules::SHostRestrictionSnapshot state;
        state.generation = 1u;
        TEST_EXPECT(tests, restrictions.publish(state));
        execution.restriction = restrictions.reader();
        context.instance = { 7u };
        context.execution = &execution;
        context.communication = &communication;
        TEST_EXPECT(tests, capture.emplace(context, clock, adapter));
    }
    static modules::ESubmission MV_STD_ABI_CALL post_owner(void* const cookie, const modules::SDestination&,
        threading::CErasedOwnerMsg&& message) noexcept
    {
        SProbe& probe = *static_cast<SProbe*>(cookie);
        if (probe.busy) return modules::ESubmission::busy;
        probe.accepted = std::move(message);
        ++probe.posts;
        return modules::ESubmission::accepted;
    }
    static modules::ESubmission MV_STD_ABI_CALL post_pod(void* const cookie, const modules::SDestination&,
        const threading::CErasedPodMsg& message) noexcept
    {
        SProbe& probe = *static_cast<SProbe*>(cookie);
        if (probe.busy) return modules::ESubmission::busy;
        PlatformReportCaptureFailed outcome;
        TEST_EXPECT(probe.tests, message.copy_payload_to(outcome));
        probe.failure = outcome.failure;
        ++probe.posts;
        return modules::ESubmission::accepted;
    }
    static platform::threading::WaitError read(void* const data, std::uint64_t& time) noexcept
    {
        SProbe& probe = *static_cast<SProbe*>(data);
        time = probe.now;
        return { probe.clock_failed ? platform::threading::EWaitError::invalid_argument : platform::threading::EWaitError::none, 0u };
    }
    static EReportStep step(platform::native::SReportSession& session) noexcept
    {
        SProbe& probe = *static_cast<SProbe*>(session.clock.context);
        ++probe.calls;
        ++session.native_calls;
        if (probe.retry)
        {
            return EReportStep::topology_changed;
        }
        if (!limitation(session.candidate, probe.now))
        {
            session.failure = { EStage::capture, EReason::allocation_failed };
            return EReportStep::failed;
        }
        return EReportStep::finished;
    }
    void begin(const std::uint64_t identity = 1u) noexcept
    {
        modules::CHostEligibleBudget budget{ clock };
        TEST_EXPECT(tests, (budget.start(restrictions.reader(), k_capture_limits.eligible_duration_ns) == modules::EBudgetStatus::running));
        capture->message(threading::make_pod_message(-1, PlatformReportCaptureRequest{ { context.instance, identity }, budget.anchor() }));
    }
    void tick() noexcept
    {
        capture->progress(false, 500000u);
    }
    void receipt(const std::uint64_t id) noexcept
    {
        capture->message(threading::make_pod_message(-1, PlatformReportCaptureReceipt{ { context.instance, id }, EDisposition::rejected, {} }));
    }
};
const modules::SCommunicationV1 SProbe::functions{ &SProbe::post_pod, &SProbe::post_owner };

static void transfer(tests::TTestContext& tests)
{
    SProbe p{ tests };
    p.begin();
    p.capture->progress(false, 0u);
    TEST_EXPECT(tests, (!p.calls && !p.posts));
    p.busy = true;
    for (unsigned n = 0u; n < 5u; ++n) p.tick();
    TEST_EXPECT(tests, ((p.calls == 1u) && (p.posts == 0u) && !p.capture->runnable()));
    p.busy = false;
    p.tick();
    TEST_EXPECT(tests, (p.accepted.has_owner() && p.capture->active() && !p.capture->runnable()));
    auto* const offered = p.accepted.owner().payload<PlatformReportCaptured>();
    schema::CBakedDataBlock block;
    schema::SDataBlockDiagnostic error;
    TEST_EXPECT(tests, (offered && block.adopt_local(std::move(offered->storage), error)));
    TEST_EXPECT(tests, (validate_report(block, { { 7u }, 1u }, {}).reason == EReason::none));
    TEST_EXPECT(tests, (validate_report(block, { { 8u }, 1u }, {}).reason == EReason::invalid_data));
    p.receipt(2u);
    TEST_EXPECT(tests, p.capture->active());
    p.receipt(1u);
    TEST_EXPECT(tests, !p.capture->active());
    p.begin();
    TEST_EXPECT(tests, !p.capture->active());
}

static void receipt_then_request(tests::TTestContext& tests)
{
    SProbe p{ tests };
    p.begin();
    for (unsigned turn = 0u; (turn < 8u) && !p.accepted.has_owner(); ++turn) p.tick();
    TEST_EXPECT(tests, (p.accepted.has_owner() && p.capture->active()));
    p.accepted = {};
    // One incoming FIFO can contain both before the next pump invocation.
    p.receipt(1u);
    p.begin(2u);
    for (unsigned turn = 0u; (turn < 8u) && !p.accepted.has_owner(); ++turn) p.tick();
    TEST_EXPECT(tests, (p.accepted.has_owner() && (p.calls == 2u)));
    const PlatformReportCaptured* const offered = p.accepted.owner().payload<PlatformReportCaptured>();
    TEST_EXPECT(tests, (offered && (offered->capture.capture_id == 2u)));
    p.accepted = {};
    p.receipt(2u);
    TEST_EXPECT(tests, !p.capture->active());
}

static void failures(tests::TTestContext& tests)
{
    for (unsigned scenario = 0u; scenario < 5u; ++scenario)
    {
        SProbe p{ tests };
        p.begin();
        p.tick();
        EReason expected = EReason::closing;
        switch (scenario)
        {
            case 0u:
            {
                p.retry = true;
                expected = EReason::retry_exhausted;
                break;
            }
            case 1u:
            {
                p.now += k_capture_limits.eligible_duration_ns;
                expected = EReason::deadline_exceeded;
                break;
            }
            case 2u:
            {
                --p.now;
                expected = EReason::clock_failed;
                break;
            }
            case 3u:
            {
                p.clock_failed = true;
                expected = EReason::clock_failed;
                break;
            }
            case 4u:
            {
                p.capture->message(threading::make_pod_message(-1, PlatformReportCaptureCancel{ { { 7u }, 1u }, EReason::closing }));
                break;
            }
        }
        for (unsigned i = 0u; i < 12u; ++i) p.tick();
        TEST_EXPECT(tests, ((p.failure.reason == expected) && (p.posts == 1u) && !p.capture->runnable()));
        TEST_EXPECT(tests, (p.calls == (scenario ? 0u : 3u)));
        p.receipt(1u);
        TEST_EXPECT(tests, !p.capture->active());
    }
}

static void restricted_deadline(tests::TTestContext& tests)
{
    SProbe p{ tests };
    p.begin();
    modules::SHostRestrictionSnapshot state;
    state.generation = 1u;
    state.depth = 1u;
    state.active_start_ns = p.now;
    TEST_EXPECT(tests, p.restrictions.publish(state));
    p.now += k_capture_limits.eligible_duration_ns + 1u;
    p.tick();
    TEST_EXPECT(tests, (!p.posts && !p.calls && p.capture->active()));
    state.depth = 0u;
    state.completed_ns = p.now - state.active_start_ns;
    state.active_start_ns = 0u;
    TEST_EXPECT(tests, p.restrictions.publish(state));
    p.tick();
    p.tick();
    p.tick();
    p.busy = true;
    p.tick();
    TEST_EXPECT(tests, ((p.calls == 1u) && (p.posts == 0u) && !p.accepted.has_owner()));
    p.now += k_capture_limits.eligible_duration_ns;
    p.busy = false;
    p.tick();
    TEST_EXPECT(tests, p.capture->runnable());
    p.tick();
    TEST_EXPECT(tests, ((p.failure.reason == EReason::deadline_exceeded) && !p.accepted.has_owner()));
    p.receipt(1u);
}

static void malformed(tests::TTestContext& tests)
{
    constexpr SCaptureIdentity identity{ { 7u }, 9u };
    for (unsigned which = 0u; which < 10u; ++which)
    {
        platform::runtime::SReportCandidate candidate;
        TEST_EXPECT(tests, limitation(candidate, 20u));
        PlatformReportHeader& h = candidate.header;
        h.kind = PlatformReportKind::platform_report;
        h.schema_version = k_report_version;
        h.platform = PlatformBackend::windows;
        h.query_profile = PlatformQueryProfile::windows_desktop_discovery_v1;
        h.clock = PlatformClock::system_awake_ns;
        h.visibility_scope = PlatformVisibilityScope::calling_session;
        h.platform_instance = identity.platform_instance.value;
        h.capture_id = identity.capture_id;
        h.capture_begin_ns = 10u;
        h.capture_attempts = h.native_calls = 1u;
        h.limits = k_capture_limits;
        h.schema_limits = schema_limits({});
        switch (which)
        {
            case 0u:
            {
                break;
            }
            case 1u:
            {
                --h.limits.modes_per_scope;
                break;
            }
            case 2u:
            {
                ++h.schema_limits.result_bytes;
                break;
            }
            case 3u:
            {
                h.capture_end_ns = 0u;
                break;
            }
            case 4u:
            {
                h.coverage = PlatformCoverage::complete;
                break;
            }
            case 5u:
            {
                candidate.queries[1].blocked_by_query_id = 2u;
                break;
            }
            case 6u:
            {
                candidate.queries[1].id = 1u;
                break;
            }
            case 7u:
            {
                candidate.queries[0].native_error = 87u;
                break;
            }
            case 8u:
            {
                ++h.native_calls;
                break;
            }
            case 9u:
            {
                h.adapters = {};
                break;
            }
        }
        CBakedDocumentBlock complete;
        schema::SSchemaDiagnostic bake;
        schema::CBakedDataBlock block;
        schema::SDataBlockDiagnostic storage;
        TEST_EXPECT(tests, (candidate.bake(complete, {}, bake) && block.adopt_local(std::move(complete), storage)));
        TEST_EXPECT(tests, (validate_report(block, identity, {}).reason == (which ? EReason::invalid_data : EReason::none)));
    }
}

static void fact_provenance(tests::TTestContext& tests)
{
    using Q = PlatformQueryKind;
    using A = PlatformQueryApi;
    using S = PlatformSubjectKind;
    using I = PlatformNativeIdentityKind;
    constexpr SCaptureIdentity capture{ { 7u }, 1u };
    platform::runtime::SReportCandidate candidate;
    PlatformReportHeader& h = candidate.header;
    h.kind = PlatformReportKind::platform_report;
    h.schema_version = k_report_version;
    h.platform = PlatformBackend::windows;
    h.query_profile = PlatformQueryProfile::windows_desktop_discovery_v1;
    h.clock = PlatformClock::system_awake_ns;
    h.visibility_scope = PlatformVisibilityScope::calling_session;
    h.capture_id = capture.capture_id;
    h.platform_instance = capture.platform_instance.value;
    h.discovery = PlatformDiscovery::captured;
    h.coverage = PlatformCoverage::limited;
    h.capture_attempts = 1u;
    h.limits = k_capture_limits;
    h.schema_limits = schema_limits({});
    const auto query = [&](const Q kind, const A api, const S subject, const std::uint32_t subject_id)
    {
        PlatformQueryResult q{};
        q.id = ++h.native_calls;
        q.kind = kind;
        q.api = api;
        q.subject_kind = subject;
        q.subject_id = subject_id;
        q.status = PlatformQueryStatus::success;
        q.attempts = q.call_count = q.returned_count = 1u;
        q.diagnostic = { -1 };
        if (api == A::windows_ccd) q.native_flags = 0x51u;
        TEST_EXPECT(tests, candidate.queries.push_back(q));
        return q.id;
    };
    h.inventory_query_id = query(Q::inventory, A::windows_ccd, S::report, 0u);
    h.consistency_query_id = query(Q::consistency_check, A::windows_ccd, S::report, 0u);
    const std::uint32_t annotation = query(Q::desktop_annotation, A::windows_gdi_devices, S::report, 0u);
    const std::uint32_t inventory = query(Q::adapter_output_inventory, A::windows_dxgi, S::report, 0u);
    const std::uint32_t settings = query(Q::current_desktop_settings, A::windows_gdi_settings, S::source, 1u);
    const std::uint32_t adapter_label = query(Q::adapter_description, A::windows_dxgi, S::adapter, 1u);
    const std::uint32_t display_label = query(Q::display_identity, A::windows_display_device_info, S::display, 1u);
    const std::uint32_t other_label = query(Q::adapter_description, A::windows_dxgi, S::adapter, 2u);
    if (candidate.queries.size() != h.native_calls) return;
    h.queries = { 1, 0, h.native_calls };
    h.native_identities = { 1, 0, 3u };
    h.adapters = { 1, 0, 2u };
    h.sources = h.displays = h.paths = { 1, 0, 1u };
    const schema::SSpanReference absent{ -1, 0, 0u };
    const std::int32_t label = static_cast<std::int32_t>(candidate.text.append("Observed label"));
    TEST_EXPECT(tests, (label > 0));
    TEST_EXPECT(tests, candidate.bytes.push_back_zeroed(32u));
    TEST_EXPECT(tests, candidate.identities.push_back({ 1u, I::windows_adapter_luid, h.inventory_query_id, { 1, 0, 8u }, { -1 } }));
    TEST_EXPECT(tests, candidate.identities.push_back({ 2u, I::windows_ccd_source, h.inventory_query_id, { 1, 8, 12u }, { -1 } }));
    TEST_EXPECT(tests, candidate.identities.push_back({ 3u, I::windows_ccd_target, h.inventory_query_id, { 1, 20, 12u }, { -1 } }));
    TEST_EXPECT(tests, candidate.adapters.push_back({ 1u, { label }, adapter_label, { 1, 0, 1u }, h.inventory_query_id }));
    TEST_EXPECT(tests, candidate.adapters.push_back({ 2u, { -1 }, other_label, absent, inventory }));
    PlatformDisplaySource source{};
    source.id = source.adapter_id = 1u;
    source.active = PlatformTruth::yes;
    source.primary = PlatformTruth::no;
    source.identities = { 1, 1, 1u };
    source.desktop.query_id = source.topology_query_id = h.inventory_query_id;
    source.annotation_query_id = annotation;
    source.gdi_current.query_id = settings;
    source.gdi_current.known_fields = PlatformDesktopFields::depth;
    source.gdi_current.bits_per_pixel = 32u;
    TEST_EXPECT(tests, candidate.sources.push_back(source));
    PlatformDisplay display{};
    display.id = display.adapter_id = 1u;
    display.label = { label };
    display.label_query_id = display_label;
    display.active = display.available = PlatformTruth::yes;
    display.identities = { 1, 2, 1u };
    display.state_query_id = display.connection_query_id = h.inventory_query_id;
    TEST_EXPECT(tests, candidate.displays.push_back(display));
    PlatformDisplayPath path{};
    path.id = path.source_id = path.display_id = 1u;
    path.query_id = path.signal.query_id = path.desktop_image.query_id = h.inventory_query_id;
    TEST_EXPECT(tests, candidate.paths.push_back(path));
    if ((candidate.adapters.size() != 2u) || !candidate.sources.size() || !candidate.displays.size() ||
        (candidate.identities.size() != 3u))
    {
        return;
    }
    const auto expect = [&](const EReason reason)
    {
        CBakedDocumentBlock complete;
        schema::SSchemaDiagnostic bake;
        schema::CBakedDataBlock block;
        schema::SDataBlockDiagnostic storage;
        TEST_EXPECT(tests, (candidate.bake(complete, {}, bake) && block.adopt_local(std::move(complete), storage)));
        const SFailure result = validate_report(block, capture, {});
        TEST_EXPECT(tests, (result.reason == reason));
        TEST_EXPECT(tests, (result.stage == ((reason == EReason::none) ? EStage::none : EStage::validation)));
    };
    // One known setting, one known-false annotation, and two observed labels.
    expect(EReason::none);
    PlatformQueryResult& current = candidate.queries[settings - 1u];
    current.status = PlatformQueryStatus::failed;
    current.reason = PlatformQueryReason::native_failure;
    current.error_domain = PlatformErrorDomain::win32;
    current.native_error = 31u;
    current.returned_count = 0u;
    expect(EReason::invalid_data);
    candidate.sources[0].gdi_current = {};
    candidate.sources[0].gdi_current.query_id = settings;
    expect(EReason::none);

    PlatformQueryResult& annotations = candidate.queries[annotation - 1u];
    annotations.status = PlatformQueryStatus::blocked;
    annotations.reason = PlatformQueryReason::dependency_missing;
    annotations.blocked_by_query_id = settings;
    annotations.attempts = annotations.call_count = annotations.returned_count = 0u;
    --h.native_calls;
    expect(EReason::invalid_data);
    candidate.sources[0].primary = PlatformTruth::unknown;
    expect(EReason::none);
    candidate.sources[0].desktop_attached = PlatformTruth::yes;
    expect(EReason::invalid_data);
    candidate.sources[0].desktop_attached = PlatformTruth::unknown;

    candidate.adapters[0].label_query_id = other_label;
    expect(EReason::invalid_data);
    candidate.adapters[0].label_query_id = adapter_label;
    PlatformQueryResult& description = candidate.queries[adapter_label - 1u];
    description.status = PlatformQueryStatus::failed;
    description.reason = PlatformQueryReason::native_failure;
    description.returned_count = 0u;
    expect(EReason::invalid_data);
    candidate.adapters[0].label = { -1 };
    expect(EReason::none);
    PlatformQueryResult& name = candidate.queries[display_label - 1u];
    name.status = PlatformQueryStatus::blocked;
    name.reason = PlatformQueryReason::dependency_missing;
    name.blocked_by_query_id = adapter_label;
    name.attempts = name.call_count = name.returned_count = 0u;
    --h.native_calls;
    expect(EReason::invalid_data);
    candidate.displays[0].label = { -1 };
    expect(EReason::none);
    candidate.identities[0].query_id = other_label;
    expect(EReason::invalid_data);
    candidate.identities[0].query_id = h.inventory_query_id;
    candidate.adapters[1].discovery_query_id = h.consistency_query_id;
    expect(EReason::invalid_data);
}

}   //  namespace

namespace host
{

struct SReportHostTestAccess
{
    static void progress(CAssetService& service, SClientRecord& provider, SClientRecord* const consumer = nullptr)
    {
        service.progress_reports(false);
        if (service.report_output_ready(provider) && provider.output_ready()) service.retry_report_output(provider);
        if (consumer && service.report_output_ready(*consumer) && consumer->output_ready())
        {
            service.retry_report_output(*consumer);
        }
    }
    static void capture_boundaries(tests::TTestContext& tests, const unsigned scenario)
    {
        using namespace platform::reports;
        memory::CMemoryAllocator allocator{ nullptr, &tests::allocate_scalar_test_memory, &tests::deallocate_scalar_test_memory, system_ids::host };
        CHost runtime{ allocator };
        runtime.m_phase = CHost::EPhase::complete;
        CAssetService& service = runtime.m_asset_service;
        TEST_EXPECT(tests, service.initialise());
        modules::CBoundModule binding;
        SClientRecord provider{ module_ids::platform, 800u, nullptr, { 1800u } };
        provider.owner_wake = runtime.m_owner_wait.endpoint();
        TEST_EXPECT(tests, provider.initialise_execution(binding, {}, nullptr, 32u));
        tests::CEndpointProbe probe{ provider };
        provider.phase = EClientPhase::ready;
        provider.host_execution.restriction = runtime.m_maintenance.restrictions().reader();
        service.install_report_provider(provider);
        progress(service, provider);
        if (scenario == 0u)
        {
            // Stop is recorded before Platform reads even the capture request.
            service.close_report_provider(provider);
            progress(service, provider);
            TEST_EXPECT(tests, (probe.pending() == 2u));
        }
        PlatformReportCaptureRequest request;
        TEST_EXPECT(tests, probe.received(0u).copy_payload_to(request));
        probe.discard(1u);
        if (scenario == 0u)
        {
            PlatformReportCaptureCancel cancel;
            TEST_EXPECT(tests, (probe.received(0u).copy_payload_to(cancel) &&
                (cancel.capture.capture_id == request.capture.capture_id)));
            probe.discard(1u);
        }
        if (scenario == 2u)
        {
            provider.phase = EClientPhase::stopped;
            progress(service, provider);
            TEST_EXPECT(tests, !service.client_idle(provider));
            provider.terminal_before_drain = provider.incoming_drained = true;
            progress(service, provider);
            TEST_EXPECT(tests, (service.client_idle(provider) && provider.replies_idle()));
        }
        else
        {
            service.report_failed(provider, { request.capture, { EStage::lifecycle, EReason::closing } });
            std::uint32_t fillers = 0u;
            if (scenario == 1u)
            {
                while (provider.post(threading::make_pod_message(-1, PlatformReportCaptureReceipt{}))) ++fillers;
                TEST_EXPECT(tests, (fillers != 0u));
            }
            progress(service, provider);
            progress(service, provider);
            if (scenario == 1u)
            {
                TEST_EXPECT(tests, (!service.client_idle(provider) && !provider.replies_idle()));
                probe.discard(fillers);
                progress(service, provider);
            }
            PlatformReportCaptureReceipt receipt;
            TEST_EXPECT(tests, (probe.received(0u).copy_payload_to(receipt) &&
                (receipt.capture.capture_id == request.capture.capture_id) &&
                (receipt.disposition == EDisposition::interrupted)));
            probe.discard(1u);
            // Publication settles the operation's output; the next Host pass
            // retires its capture record independently of recipient consumption.
            progress(service, provider);
            TEST_EXPECT(tests, (service.client_idle(provider) && provider.replies_idle()));
        }
        service.close_report_provider(provider);
        service.deallocate();
    }

    static void handoff(tests::TTestContext& tests, const unsigned phase)
    {
        using namespace platform::reports;
        tests::TAllocationFailureFixture allocations;
        memory::CMemoryAllocator allocator{ &allocations, &tests::allocate_with_failure, &tests::deallocate_test_memory, system_ids::host };
        memory::CMemoryContext memory{ allocator, system_ids::host };
        tests::TMemoryContextScope memory_scope{ &memory };
        modules::CBoundModule binding;
        CHost host{ allocator };
        host.m_phase = CHost::EPhase::complete;
        CAssetService& service = host.m_asset_service;
        TEST_EXPECT(tests, service.initialise());
        SClientRecord consumer{ module_ids::executive, 31u, nullptr, modules::SModuleInstance{ 131u } };
        consumer.owner_wake = host.m_owner_wait.endpoint();
        TEST_EXPECT(tests, consumer.initialise_execution(binding, {}, nullptr, 32u));
        consumer.phase = EClientPhase::ready;
        SClientRecord provider{ module_ids::platform, 32u, nullptr, modules::SModuleInstance{ 132u } };
        provider.owner_wake = host.m_owner_wait.endpoint();
        TEST_EXPECT(tests, provider.initialise_execution(binding, {}));
        TEST_EXPECT(tests, provider.install_host_execution(host.m_maintenance.restrictions().reader()));
        tests::CEndpointProbe provider_probe{ provider };
        provider.phase = EClientPhase::ready;
        service.install_report_provider(provider);
        progress(service, provider, &consumer);
        PlatformReportCaptureRequest request;
        TEST_EXPECT(tests, ((provider_probe.pending() == 1u) && provider_probe.received(0u).copy_payload_to(request)));
        auto message = threading::make_owner_message<PlatformReportCaptured>(-1);
        auto* const offered = message.owner().payload<PlatformReportCaptured>();
        if (!offered) return;
        offered->capture = request.capture;
        platform::runtime::SReportCandidate candidate;
        TEST_EXPECT(tests, limitation(candidate, 101u));
        PlatformReportHeader& header = candidate.header;
        header.kind = PlatformReportKind::platform_report;
        header.schema_version = k_report_version;
        header.platform = PlatformBackend::windows;
        header.query_profile = PlatformQueryProfile::windows_desktop_discovery_v1;
        header.clock = PlatformClock::system_awake_ns;
        header.visibility_scope = PlatformVisibilityScope::calling_session;
        header.platform_instance = request.capture.platform_instance.value;
        header.capture_id = request.capture.capture_id;
        header.capture_begin_ns = 100u;
        header.capture_attempts = header.native_calls = 1u;
        header.limits = k_capture_limits;
        header.schema_limits = schema_limits({});
        schema::SSchemaDiagnostic error;
        TEST_EXPECT(tests, candidate.bake(offered->storage, {}, error));
        provider_probe.discard(1u);
        {
            CRetainedReplies::CDispatch dispatch{ *provider.pending_replies, 0u };
            TEST_EXPECT(tests, static_cast<bool>(dispatch));
            host.receive_request(message, provider);
        }
        TEST_EXPECT(tests, !message.has_owner());
        if (phase < 2u) allocations.fail_on = allocations.attempts + phase;
        progress(service, provider, &consumer);
        progress(service, provider, &consumer);
        progress(service, provider, &consumer); // Import, settlement, then FIFO receipt publication.
        PlatformReportCaptureReceipt receipt;
        TEST_EXPECT(tests, ((provider_probe.pending() == 1u) && provider_probe.received(0u).copy_payload_to(receipt)));
        if (phase < 2u)
        {
            TEST_EXPECT(tests, allocations.failed);
            TEST_EXPECT(tests, (receipt.disposition == EDisposition::rejected));
            TEST_EXPECT(tests, (receipt.failure.reason == EReason::allocation_failed));
        }
        else
        {
            TEST_EXPECT(tests, (receipt.disposition == EDisposition::published));
        }
        provider_probe.discard(1u);
        progress(service, provider, &consumer);
        TEST_EXPECT(tests, (service.client_idle(provider) && provider.replies_idle()));
        if (phase == 2u)
        {
            CModulePodPipe& pipe = consumer.channel->to_module;
            for (unsigned i = 0u; i < 32u; ++i)
            {
                TEST_EXPECT(tests, consumer.post(threading::make_pod_message(-1, AssetReleaseResult{})));
            }
            TEST_EXPECT(tests, consumer.reserve_replies(1u));
            {
                CRetainedReplies::CDispatch dispatch{ *consumer.pending_replies, 1u };
                TEST_EXPECT(tests, static_cast<bool>(dispatch));
                host.receive_message(threading::make_pod_message(265, PlatformReportRequest{}), consumer);
            }
            const std::size_t allocation_attempts = allocations.attempts;
            allocations.fail_on = allocation_attempts;
            progress(service, provider, &consumer);
            TEST_EXPECT(tests, (!allocations.failed && !service.failed() && (consumer.reserved_replies == 1u) &&
                (allocations.attempts == allocation_attempts) && service.report_output_ready(consumer)));
            allocations.fail_on = std::numeric_limits<std::size_t>::max();
            // Fixed ordinary rings refuse full without allocating or exposing
            // an acquisition. The report operation retains its one owed result.
            threading::CErasedPodMsg reply;
            unsigned fillers = 0u;
            while (consumer.channel->read_preamble_pod(reply))
            {
                TEST_EXPECT(tests, reply.is_payload_a<AssetReleaseResult>());
                ++fillers;
            }
            TEST_EXPECT(tests, (fillers == pipe.capacity()));
            progress(service, provider, &consumer);
            PlatformReportResult exposed;
            unsigned count{};
            while (consumer.channel->read_preamble_pod(reply))
            {
                if (reply.copy_payload_to(exposed)) ++count;
            }
            TEST_EXPECT(tests, (!consumer.reserved_replies && !service.report_output_ready(consumer)));
            TEST_EXPECT(tests, ((count == 1u) && exposed.acquisition && exposed.bytes.size()));
            service.close_report_provider(provider);
            service.release_client_acquisitions(consumer);
            progress(service, provider, &consumer);
            TEST_EXPECT(tests, (!service.client_idle(consumer) && exposed.bytes.data() && (exposed.bytes.data()[0] == 'M')));
        }
        consumer.phase = EClientPhase::stopped;
        consumer.terminal_before_drain = consumer.incoming_drained = true;
        service.release_client_acquisitions(consumer);
        service.close_report_provider(provider);
        progress(service, provider, &consumer);
        TEST_EXPECT(tests, service.client_idle(consumer));
        provider.phase = EClientPhase::stopped;
        provider.terminal_before_drain = provider.incoming_drained = true;
        TEST_EXPECT(tests, provider.retire_host_execution(true));
        service.deallocate();
    }
};

}   //  namespace host

#if MV_PLATFORM_WINDOWS
void run_platform_report_windows_tests(tests::TTestContext& tests);
#endif

void run_platform_report_tests(tests::TTestContext& tests)
{
    for (unsigned phase = 0u; phase < 3u; ++phase) host::SReportHostTestAccess::handoff(tests, phase);
    for (unsigned scenario = 0u; scenario < 3u; ++scenario) host::SReportHostTestAccess::capture_boundaries(tests, scenario);
    transfer(tests);
    receipt_then_request(tests);
    failures(tests);
    restricted_deadline(tests);
    malformed(tests);
    fact_provenance(tests);
#if MV_PLATFORM_WINDOWS
    run_platform_report_windows_tests(tests);
#endif
}

int run_platform_report_focused()
{
    tests::TTestContext tests;
    run_platform_report_tests(tests);
    return tests.report("PlatformReport");
}


//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    PlatformReportWindows_test_cases.cpp
//  Primary implementation: OpenAI Codex
//  Reviewed and accepted by: Ritchie Brannan
//  Date:    18 Nov 26
//
//  Replays native CCD, GDI and DXGI return values through the production Windows discovery cursor.

#include "platform/platform_defines.hpp"
#if MV_PLATFORM_WINDOWS
#include "platform/native/report.hpp"
#include "platform/native/windows/report_api.hpp"
#include "framework/platform/report_validation.hpp"
#include "tests/support/test_context.hpp"

namespace
{

using namespace platform::native;
using namespace platform_report_v1;
using platform::reports::EReason;

enum class ECase
{
    ordinary, flag_fallback, buffer_retry, ccd_exhaustion, unavailable, unsupported, unexpected,
    bad_index, wrong_identity, topology_changed, ambiguous, zero_modes, more_data, more_exhausted,
    dxgi_denied, api_failure, mode_cap, aggregate_cap, adapter_cap, path_cap, memory_failure,
    native_call_cap, deadline, missing_names, desktop_image, unknown_codes, missing_fields,
    exact_limits, display_cap, source_cap, monitor_cap, output_cap, ccd_mode_cap, interface_failure
};

struct SReplay
{
    explicit SReplay(const ECase scenario) noexcept : scenario{ scenario } {}
    ECase scenario;
    std::uint64_t now{ 100u };
    unsigned size_calls{}, fetch_calls{}, mode_calls{}, gdi_indexed{}, references{};
    bool flags_valid{ true };
    static thread_local SReplay* active;

    static platform::threading::WaitError clock(void* const data, std::uint64_t& value) noexcept
    {
        value = static_cast<SReplay*>(data)->now;
        return {};
    }
    static LONG WINAPI sizes(const UINT32 flags, UINT32* const paths, UINT32* const modes) noexcept
    {
        SReplay& s = *active;
        ++s.size_calls;
        if ((s.scenario == ECase::flag_fallback) && (flags != QDC_ALL_PATHS))
        {
            return ERROR_INVALID_PARAMETER;
        }
        if (s.scenario == ECase::unavailable)
        {
            return ERROR_ACCESS_DENIED;
        }
        if (s.scenario == ECase::unsupported)
        {
            return ERROR_NOT_SUPPORTED;
        }
        if (s.scenario == ECase::unexpected)
        {
            return ERROR_BAD_CONFIGURATION;
        }
        if (s.scenario == ECase::memory_failure)
        {
            return ERROR_NOT_ENOUGH_MEMORY;
        }
        *paths = 4u;
        *modes = (s.scenario == ECase::desktop_image) ? 5u : 3u;
        if (s.scenario == ECase::deadline) s.now += platform::reports::k_capture_limits.eligible_duration_ns;
        return ERROR_SUCCESS;
    }
    static LONG WINAPI fetch(const UINT32 flags, UINT32* const paths, DISPLAYCONFIG_PATH_INFO* const p,
        UINT32* const modes, DISPLAYCONFIG_MODE_INFO* const m, DISPLAYCONFIG_TOPOLOGY_ID* const topology) noexcept
    {
        SReplay& s = *active;
        ++s.fetch_calls;
        if ((s.scenario == ECase::ccd_exhaustion) || ((s.scenario == ECase::buffer_retry) && (s.fetch_calls <= 2u)))
        {
            return ERROR_INSUFFICIENT_BUFFER;
        }
        if (topology || (*paths != 4u) || (*modes != ((s.scenario == ECase::desktop_image) ? 5u : 3u)))
        {
            return ERROR_INVALID_PARAMETER;
        }
        const LUID adapter{ 0x10203040u, -2 };
        const bool reversed = s.fetch_calls > 1u;
        for (UINT32 n = 0u; n < 3u; ++n)
        {
            m[n] = {};
            m[n].adapterId = adapter;
            m[n].infoType = n ? DISPLAYCONFIG_MODE_INFO_TYPE_TARGET : DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE;
            m[n].id = n ? (20u + n) : 9u;
            if (!n)
            {
                m[n].sourceMode = { 1600u, 900u, DISPLAYCONFIG_PIXELFORMAT_32BPP, { -1600, -90 } };
                if (reversed && (s.scenario == ECase::topology_changed)) ++m[n].sourceMode.position.x;
            }
            else
            {
                m[n].targetMode.targetVideoSignalInfo.activeSize = { 1920u, 1080u };
                m[n].targetMode.targetVideoSignalInfo.vSyncFreq = { 60000u, 1001u };
            }
        }
        if (s.scenario == ECase::desktop_image)
        {
            for (UINT32 n = 3u; n < 5u; ++n)
            {
                m[n] = {};
                m[n].infoType = DISPLAYCONFIG_MODE_INFO_TYPE_DESKTOP_IMAGE;
                m[n].id = 18u + n;
                m[n].adapterId = adapter;
                m[n].desktopImageInfo.PathSourceSize = { 800, 600 };
                m[n].desktopImageInfo.DesktopImageRegion = { -10, 2, 790, 602 };
                m[n].desktopImageInfo.DesktopImageClip = { 0, 0, 750, 590 };
            }
        }
        for (UINT32 n = 0u; n < 4u; ++n)
        {
            DISPLAYCONFIG_PATH_INFO& path = p[reversed ? (3u - n) : n];
            path = {};
            path.sourceInfo.adapterId = path.targetInfo.adapterId = adapter;
            path.sourceInfo.id = 9u;
            path.targetInfo.id = 21u + n;
            path.targetInfo.targetAvailable = (n == 0u) || (n == 2u);
            path.targetInfo.outputTechnology = DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI;
            path.sourceInfo.modeInfoIdx = path.targetInfo.modeInfoIdx = UINT32_MAX;
            if (n >= 2u) continue;
            path.flags = DISPLAYCONFIG_PATH_ACTIVE;
            path.sourceInfo.modeInfoIdx = 0u;
            path.targetInfo.modeInfoIdx = n + 1u;
            if (flags & QDC_VIRTUAL_MODE_AWARE)
            {
                path.flags |= DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE;
                path.sourceInfo.sourceModeInfoIdx = 0u;
                path.targetInfo.desktopModeInfoIdx = (s.scenario == ECase::desktop_image) ? static_cast<UINT16>(n + 3u) : UINT16_MAX;
                path.targetInfo.targetModeInfoIdx = static_cast<UINT16>(n + 1u);
            }
            path.targetInfo.rotation = (s.scenario == ECase::unknown_codes) ? static_cast<DISPLAYCONFIG_ROTATION>(93u) : DISPLAYCONFIG_ROTATION_ROTATE90;
            path.targetInfo.scaling = DISPLAYCONFIG_SCALING_CENTERED;
            path.targetInfo.scanLineOrdering = DISPLAYCONFIG_SCANLINE_ORDERING_PROGRESSIVE;
            path.targetInfo.refreshRate = { 60u, 1u };
            if (flags & QDC_VIRTUAL_REFRESH_RATE_AWARE) path.flags |= DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE;
            if (s.scenario == ECase::bad_index) path.sourceInfo.modeInfoIdx = 0x77777777u;
        }
        if (s.scenario == ECase::wrong_identity) ++m[1].id;
        return ERROR_SUCCESS;
    }

    static LONG WINAPI info(DISPLAYCONFIG_DEVICE_INFO_HEADER* const header) noexcept
    {
        if (active->scenario == ECase::missing_names)
        {
            return ERROR_GEN_FAILURE;
        }
        switch (header->type)
        {
            case DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME:
            {
                auto& packet = *reinterpret_cast<DISPLAYCONFIG_SOURCE_DEVICE_NAME*>(header);
                wcscpy_s(packet.viewGdiDeviceName, L"REPORT0");
                break;
            }
            case DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME:
            {
                auto& packet = *reinterpret_cast<DISPLAYCONFIG_TARGET_DEVICE_NAME*>(header);
                wcscpy_s(packet.monitorDevicePath, (header->id == 21u) ? L"monitor-a" : L"monitor-b");
                wcscpy_s(packet.monitorFriendlyDeviceName, L"Display");
                break;
            }
            case DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME:
            {
                auto& packet = *reinterpret_cast<DISPLAYCONFIG_ADAPTER_NAME*>(header);
                wcscpy_s(packet.adapterDevicePath, L"adapter");
                break;
            }
            default:
            {
                return ERROR_INVALID_PARAMETER;
            }
        }
        return ERROR_SUCCESS;
    }

    static BOOL WINAPI devices(const wchar_t* const name, const DWORD i, DISPLAY_DEVICEW* const d, const DWORD flags) noexcept
    {
        if (i) return FALSE;
        if (name)
        {
            active->flags_valid &= flags == EDD_GET_DEVICE_INTERFACE_NAME;
            wcscpy_s(d->DeviceID, L"monitor-a");
        }
        else
        {
            wcscpy_s(d->DeviceName, L"REPORT0");
            d->StateFlags = DISPLAY_DEVICE_PRIMARY_DEVICE | DISPLAY_DEVICE_ATTACHED_TO_DESKTOP;
        }
        return TRUE;
    }
    static BOOL WINAPI settings(const wchar_t* const, const DWORD i, DEVMODEW* const mode, const DWORD flags) noexcept
    {
        active->flags_valid &= flags == 0u;
        if (i != ENUM_CURRENT_SETTINGS)
        {
            ++active->gdi_indexed;
            if (i) return FALSE;
        }
        mode->dmFields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY | DM_BITSPERPEL;
        if (active->scenario == ECase::missing_fields) mode->dmFields = DM_BITSPERPEL;
        mode->dmPosition = { -1600, -90 };
        mode->dmPelsWidth = 1600u;
        mode->dmPelsHeight = 900u;
        mode->dmDisplayFrequency = 59u;
        mode->dmBitsPerPel = 32u;
        return TRUE;
    }
    template<typename T>
    static HRESULT object(T** const result) noexcept
    {
        //  The injected driver owns this opaque token; every interface operation is intercepted.
        *result = reinterpret_cast<T*>(active);
        ++active->references;
        return S_OK;
    }
    static HRESULT list(IDXGIOutput1*, const DXGI_FORMAT format, const UINT flags, UINT* const count, DXGI_MODE_DESC1* const buffer) noexcept
    {
        SReplay& s = *active;
        ++s.mode_calls;
        s.flags_valid &= (flags == (DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING)) &&
            ((format == DXGI_FORMAT_R8G8B8A8_UNORM) || (format == DXGI_FORMAT_B8G8R8A8_UNORM));
        if (s.scenario == ECase::api_failure) return E_FAIL;
        if (s.scenario == ECase::zero_modes)
        {
            *count = 0u;
            return S_OK;
        }
        *count = 2u;
        if (!buffer) return S_OK;
        if ((s.scenario == ECase::more_exhausted) || ((s.scenario == ECase::more_data) && (s.mode_calls <= 4u)))
        {
            return DXGI_ERROR_MORE_DATA;
        }
        for (UINT i = 0u; i < *count; ++i)
        {
            buffer[i] = {};
            buffer[i].Format = format;
            buffer[i].Width = 1600u;
            buffer[i].Height = 900u;
            buffer[i].RefreshRate = { i ? 60000u : 60u, i ? 1001u : 1u };
            buffer[i].Scaling = DXGI_MODE_SCALING_CENTERED;
            buffer[i].ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_PROGRESSIVE;
        }
        return S_OK;
    }

    static const SWindowsReportApi& api() noexcept
    {
        static const SWindowsReportApi value{
            &sizes, &fetch, &info, &devices, &settings,
            [](IDXGIFactory1** const f) noexcept { return (active->scenario == ECase::dxgi_denied) ? DXGI_ERROR_UNSUPPORTED : object(f); },
            [](IDXGIFactory1*, const UINT i, IDXGIAdapter1** const a) noexcept { return i ? DXGI_ERROR_NOT_FOUND : object(a); },
            [](IDXGIAdapter1*, DXGI_ADAPTER_DESC1* const d) noexcept
            {
                d->AdapterLuid = { 0x10203040u, -2 };
                wcscpy_s(d->Description, L"Adapter");
                return S_OK;
            },
            [](IDXGIAdapter1*, const UINT i, IDXGIOutput** const o) noexcept { return i ? DXGI_ERROR_NOT_FOUND : object(o); },
            [](IDXGIOutput*, DXGI_OUTPUT_DESC* const d) noexcept
            {
                wcscpy_s(d->DeviceName, (active->scenario == ECase::ambiguous) ? L"unmapped" : L"REPORT0");
                return S_OK;
            },
            [](IDXGIOutput*, IDXGIOutput1** const o) noexcept { return (active->scenario == ECase::interface_failure) ? E_NOINTERFACE : object(o); },
            &list, [](IUnknown*) noexcept { --active->references; }
        };
        return value;
    }
};
thread_local SReplay* SReplay::active{};

static void run(tests::TTestContext& tests, const ECase scenario)
{
    using namespace platform::reports;
    SReplay replay{ scenario };
    SReplay::active = &replay;
    modules::CHostRestrictionPublication restrictions;
    modules::SHostRestrictionSnapshot state;
    state.generation = 1u;
    TEST_EXPECT(tests, restrictions.publish(state));
    {
        SReportSession session;
        session.clock = { &replay, &SReplay::clock };
        modules::CHostEligibleBudget budget{ session.clock };
        TEST_EXPECT(tests, (budget.start(restrictions.reader(), k_capture_limits.eligible_duration_ns) == modules::EBudgetStatus::running));
        session.budget = &budget;
        session.slice_end_ns = UINT64_MAX;
        auto& limits = session.candidate.header.limits;
        limits = k_capture_limits;
        if (scenario == ECase::mode_cap) limits.modes_per_scope = 1u;
        if (scenario == ECase::aggregate_cap) limits.total_mode_rows = 3u;
        if (scenario == ECase::path_cap) limits.ccd_paths = 3u;
        if (scenario == ECase::adapter_cap) limits.adapters_per_backend = 0u;
        if (scenario == ECase::native_call_cap) limits.native_calls = 1u;
        if (scenario == ECase::display_cap) limits.displays = 2u;
        if (scenario == ECase::source_cap) limits.sources = 0u;
        if (scenario == ECase::monitor_cap) limits.monitor_records = 0u;
        if (scenario == ECase::output_cap) limits.outputs = 0u;
        if (scenario == ECase::ccd_mode_cap) limits.ccd_modes = 2u;
        if (scenario == ECase::exact_limits)
        {
            limits.adapters_per_backend = limits.sources = limits.outputs = limits.monitor_records = 1u;
            limits.displays = limits.ccd_modes = 3u;
            limits.ccd_paths = limits.total_mode_rows = 4u;
            limits.modes_per_scope = 2u;
        }
        EReportStep step = EReportStep::more;
        for (unsigned i = 0u; (step == EReportStep::more) && (i < 300u); ++i)
        {
            const auto before = session.native_calls;
            step = windows_report_step(session, SReplay::api());
            TEST_EXPECT(tests, ((session.native_calls - before) <= 1u));
        }
        const auto& rows = session.candidate;
        EReason failure = EReason::none;
        switch (scenario)
        {
            case ECase::ccd_exhaustion:
            {
                failure = EReason::retry_exhausted;
                break;
            }
            case ECase::bad_index:
            case ECase::wrong_identity:
            {
                failure = EReason::invalid_data;
                break;
            }
            case ECase::unexpected:
            {
                failure = EReason::native_failure;
                break;
            }
            case ECase::mode_cap:
            case ECase::aggregate_cap:
            case ECase::adapter_cap:
            case ECase::path_cap:
            case ECase::display_cap:
            case ECase::source_cap:
            case ECase::monitor_cap:
            case ECase::output_cap:
            case ECase::ccd_mode_cap:
            case ECase::native_call_cap:
            {
                failure = EReason::limit_exceeded;
                break;
            }
            case ECase::memory_failure:
            {
                failure = EReason::allocation_failed;
                break;
            }
            case ECase::deadline:
            {
                failure = EReason::deadline_exceeded;
                break;
            }
            default:
            {
                break;
            }
        }
        TEST_EXPECT(tests, (session.failure.reason == failure));
        if (failure != EReason::none) TEST_EXPECT(tests, (step == EReportStep::failed));
        else if (scenario == ECase::topology_changed) TEST_EXPECT(tests, (step == EReportStep::topology_changed));
        else if ((scenario == ECase::unsupported) || (scenario == ECase::unavailable))
        {
            TEST_EXPECT(tests, ((step == EReportStep::finished) && (rows.header.coverage == PlatformCoverage::limited) &&
                (rows.header.adapters.collection_id == -1) && !rows.displays.size()));
        }
        else
        {
            TEST_EXPECT(tests, ((step == EReportStep::finished) && (rows.sources.size() == 1u) &&
                (rows.displays.size() == 3u) && (rows.paths.size() == 2u)));
            if (rows.paths.size() == 2u)
            {
                TEST_EXPECT(tests, ((rows.paths[0].source_id == rows.paths[1].source_id) &&
                    (rows.paths[0].signal.rotation.value == ((scenario == ECase::unknown_codes) ? PlatformRotation::other : PlatformRotation::counterclockwise_270)) &&
                    (rows.sources[0].desktop.rectangle.x == -1600) && (rows.paths[0].signal.extent.width == 1920u) &&
                    (rows.paths[0].signal.physical_refresh.denominator == 1001u)));
            }
            if (scenario == ECase::desktop_image)
            {
                TEST_EXPECT(tests, (rows.paths.size() && (rows.paths[0].desktop_image.known_fields == 7u) &&
                    (rows.paths[0].desktop_image.image_region.x == -10) && (rows.paths[0].desktop_image.image_clip.width == 750u)));
            }
            if (scenario == ECase::missing_fields)
            {
                TEST_EXPECT(tests, (rows.sources.size() && (rows.sources[0].gdi_current.known_fields == PlatformDesktopFields::depth)));
            }
            if (scenario == ECase::unknown_codes)
            {
                TEST_EXPECT(tests, (rows.paths.size() && (rows.paths[0].signal.rotation.native_code == 93u)));
            }
            if (scenario == ECase::zero_modes)
            {
                TEST_EXPECT(tests, (!replay.gdi_indexed && (rows.catalogues.size() == 2u) && !rows.modes.size()));
            }
            if ((scenario == ECase::more_exhausted) || (scenario == ECase::dxgi_denied) ||
                (scenario == ECase::interface_failure) || (scenario == ECase::api_failure))
            {
                TEST_EXPECT(tests, ((replay.gdi_indexed == 2u) && (rows.modes.size() == 1u) &&
                    (rows.modes[0].refresh.precision == PlatformRefreshPrecision::integer_hz)));
            }
            if (scenario == ECase::buffer_retry) TEST_EXPECT(tests, (replay.fetch_calls == 4u));
            if (scenario == ECase::flag_fallback) TEST_EXPECT(tests, (rows.header.coverage == PlatformCoverage::limited));
            if (scenario == ECase::ambiguous) TEST_EXPECT(tests, (rows.catalogues.size() && !rows.catalogues[0].source_id));
            if ((scenario == ECase::missing_names) || (scenario == ECase::missing_fields) || (scenario == ECase::interface_failure))
            {
                PlatformReportHeader& header = session.candidate.header;
                constexpr SCaptureIdentity capture{ { 7u }, 1u };
                header.kind = PlatformReportKind::platform_report;
                header.schema_version = k_report_version;
                header.platform = PlatformBackend::windows;
                header.query_profile = PlatformQueryProfile::windows_desktop_discovery_v1;
                header.clock = PlatformClock::system_awake_ns;
                header.visibility_scope = PlatformVisibilityScope::calling_session;
                header.platform_instance = capture.platform_instance.value;
                header.capture_id = capture.capture_id;
                header.native_calls = session.native_calls;
                header.capture_attempts = 1u;
                header.schema_limits = schema_limits({});
                CBakedDocumentBlock complete;
                schema::SSchemaDiagnostic bake;
                schema::CBakedDataBlock block;
                schema::SDataBlockDiagnostic storage;
                TEST_EXPECT(tests, (rows.bake(complete, {}, bake) && block.adopt_local(std::move(complete), storage)));
                TEST_EXPECT(tests, (validate_report(block, capture, {}).reason == EReason::none));
            }
        }
        TEST_EXPECT(tests, replay.flags_valid);
    }
    TEST_EXPECT(tests, !replay.references);
    SReplay::active = nullptr;
}

}   //  namespace

void run_platform_report_windows_tests(tests::TTestContext& tests)
{
    for (unsigned n = 0u; n <= static_cast<unsigned>(ECase::interface_failure); ++n)
    {
        run(tests, static_cast<ECase>(n));
    }
}
#endif

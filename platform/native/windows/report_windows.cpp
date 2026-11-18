
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_windows.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Observes desktop topology, session annotations and scoped display modes without creating a window or device.

#include "platform/platform_defines.hpp"
#if MV_PLATFORM_WINDOWS
#include "native/report.hpp"
#include "native/windows/report_api.hpp"
#include "debug/macros.hpp"

#include <algorithm>
#include <cstring>

namespace platform::native
{

using namespace platform_report_v1;
using reports::EReason;

static constexpr UINT32 k_profiles[] = { QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE | QDC_VIRTUAL_REFRESH_RATE_AWARE,
    QDC_ALL_PATHS | QDC_VIRTUAL_MODE_AWARE, QDC_ALL_PATHS };
static constexpr DXGI_FORMAT k_formats[] = { DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_B8G8R8A8_UNORM };
static constexpr UINT k_mode_flags = DXGI_ENUM_MODES_INTERLACED | DXGI_ENUM_MODES_SCALING;
static constexpr schema::SSpanReference k_absent{ -1, 0, 0u };

template<typename T>
static void release(const SWindowsReportApi& api, T*& resource) noexcept
{
    if (resource) api.release(resource);
    resource = nullptr;
}

const SWindowsReportApi& windows_report_api() noexcept
{
    static constexpr SWindowsReportApi api{
        &GetDisplayConfigBufferSizes, &QueryDisplayConfig, &DisplayConfigGetDeviceInfo, &EnumDisplayDevicesW, &EnumDisplaySettingsExW,
        [](IDXGIFactory1** const f) noexcept { return CreateDXGIFactory1(IID_PPV_ARGS(f)); },
        [](IDXGIFactory1* const f, const UINT i, IDXGIAdapter1** const a) noexcept { return f->EnumAdapters1(i, a); },
        [](IDXGIAdapter1* const a, DXGI_ADAPTER_DESC1* const d) noexcept { return a->GetDesc1(d); },
        [](IDXGIAdapter1* const a, const UINT i, IDXGIOutput** const o) noexcept { return a->EnumOutputs(i, o); },
        [](IDXGIOutput* const o, DXGI_OUTPUT_DESC* const d) noexcept { return o->GetDesc(d); },
        [](IDXGIOutput* const o, IDXGIOutput1** const next) noexcept { return o->QueryInterface(IID_PPV_ARGS(next)); },
        [](IDXGIOutput1* const o, const DXGI_FORMAT f, const UINT flags, UINT* const n, DXGI_MODE_DESC1* const m) noexcept
        {
            return o->GetDisplayModeList1(f, flags, n, m);
        },
        [](IUnknown* const resource) noexcept { resource->Release(); }
    };
    return api;
}

SWindowsReport::~SWindowsReport() noexcept
{
    release(*api, output1);
    release(*api, output);
    release(*api, adapter);
    release(*api, factory);
}

static SCcdKey key(const LUID adapter, const UINT32 endpoint = 0u) noexcept
{
    return { adapter.LowPart, static_cast<std::uint32_t>(adapter.HighPart), endpoint };
}

static LUID luid(const SCcdKey value) noexcept
{
    return { value.low, static_cast<LONG>(value.high) };
}

static PlatformRefresh rational(const UINT32 numerator, const UINT32 denominator) noexcept
{
    return denominator ? PlatformRefresh{ PlatformRefreshPrecision::rational, numerator, denominator } :
        PlatformRefresh{ PlatformRefreshPrecision::unspecified, 0u, 0u };
}

static PlatformScanValue scan(const UINT32 value) noexcept
{
    static constexpr PlatformScan values[]{ PlatformScan::unspecified, PlatformScan::progressive,
        PlatformScan::interlaced_upper_first, PlatformScan::interlaced_lower_first };
    return { (value < 4u) ? values[value] : PlatformScan::other, value };
}

static PlatformConnectionValue connection(const DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY value) noexcept
{
    PlatformConnection kind = PlatformConnection::other;
    switch (value)
    {
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INTERNAL:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_LVDS:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EMBEDDED:
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_UDI_EMBEDDED:
        {
            kind = PlatformConnection::internal;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DISPLAYPORT_EXTERNAL:
        {
            kind = PlatformConnection::displayport;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HDMI:
        {
            kind = PlatformConnection::hdmi;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_DVI:
        {
            kind = PlatformConnection::dvi;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_HD15:
        {
            kind = PlatformConnection::vga;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_MIRACAST:
        {
            kind = PlatformConnection::wireless;
            break;
        }
        case DISPLAYCONFIG_OUTPUT_TECHNOLOGY_INDIRECT_VIRTUAL:
        {
            kind = PlatformConnection::virtual_output;
            break;
        }
        default:
        {
            break;
        }
    }
    return { kind, static_cast<std::uint32_t>(value) };
}

static bool rectangle(const RECTL value, PlatformRect& result) noexcept
{
    if ((value.right < value.left) || (value.bottom < value.top))
    {
        return false;
    }
    result = { value.left, value.top, static_cast<std::uint32_t>(static_cast<std::int64_t>(value.right) - value.left),
        static_cast<std::uint32_t>(static_cast<std::int64_t>(value.bottom) - value.top) };
    return true;
}

static EReportStep failure(SReportSession& s, const EReason reason) noexcept
{
    if (s.failure.reason == EReason::none) s.failure = { reports::EStage::capture, reason };
    return EReportStep::failed;
}

static debug_system::CInlineText16 phase_name(const EQueryPhase phase) noexcept
{
    using debug_system::CInlineText16;
    switch (phase)
    {
        case EQueryPhase::ccd_size: return CInlineText16{ "ccd_size" };
        case EQueryPhase::ccd_fetch: return CInlineText16{ "ccd_fetch" };
        case EQueryPhase::translate: return CInlineText16{ "translate" };
        case EQueryPhase::adapter_name: return CInlineText16{ "adapter_name" };
        case EQueryPhase::source_name: return CInlineText16{ "source_name" };
        case EQueryPhase::target_name: return CInlineText16{ "target_name" };
        case EQueryPhase::gdi_adapter: return CInlineText16{ "gdi_adapter" };
        case EQueryPhase::gdi_monitor: return CInlineText16{ "gdi_monitor" };
        case EQueryPhase::current_settings: return CInlineText16{ "gdi_current" };
        case EQueryPhase::factory: return CInlineText16{ "factory" };
        case EQueryPhase::dxgi_adapter: return CInlineText16{ "dxgi_adapter" };
        case EQueryPhase::adapter_desc: return CInlineText16{ "adapter_desc" };
        case EQueryPhase::output: return CInlineText16{ "output" };
        case EQueryPhase::output_desc: return CInlineText16{ "output_desc" };
        case EQueryPhase::output_interface: return CInlineText16{ "output_iface" };
        case EQueryPhase::mode_count: return CInlineText16{ "mode_count" };
        case EQueryPhase::mode_fetch: return CInlineText16{ "mode_fetch" };
        case EQueryPhase::mode_rows: return CInlineText16{ "mode_rows" };
        case EQueryPhase::gdi_mode: return CInlineText16{ "gdi_mode" };
        case EQueryPhase::finalise: return CInlineText16{ "finalise" };
        case EQueryPhase::compare: return CInlineText16{ "compare" };
        case EQueryPhase::labels: return CInlineText16{ "labels" };
    }
    return CInlineText16{ "unknown" };
}

//  One short-lived cursor dispatches a single query or translation step. All
//  state that survives the outer pump belongs to the exact capture session.
class CWindowsQueries
{
public:
    CWindowsQueries(SReportSession& session, SWindowsReport& state) noexcept :
        s{ session }, w{ state }, r{ session.candidate }, limits{ r.header.limits } {}
    EReportStep step() noexcept;

private:
    std::uint32_t query(const PlatformQueryKind kind, const PlatformQueryApi api,
        const PlatformSubjectKind subject = PlatformSubjectKind::report, const std::uint32_t id = 0u) noexcept;
    PlatformQueryResult& q(const std::uint32_t id) noexcept { return r.queries[id - 1u]; }
    void missing(const std::uint32_t id, const PlatformQueryStatus status, const PlatformQueryReason reason,
        const PlatformErrorDomain domain = PlatformErrorDomain::none, const std::uint32_t error = 0u) noexcept;
    template<typename F>
    LONG call(const std::uint32_t id, const F& f) noexcept;
    template<std::size_t N, std::size_t F>
    schema::STextReference text(const wchar_t (&value)[N], const char (&field)[F]) noexcept;
    template<std::size_t N>
    EReportStep reject(const char (&field)[N], const std::int64_t actual, const std::int64_t expected) noexcept
    {
        if (s.failure.reason == EReason::none)
        {
            static_assert(N <= sizeof(w.rejected_field));
            std::memcpy(w.rejected_field, field, N);
            w.rejected_actual = actual;
            w.rejected_expected = expected;
            MV_LOG(info, "Platform report native rejection: phase {} ({}), snapshot {}, row {}, field {}, actual {}, expected {}",
                phase_name(w.phase), static_cast<std::uint32_t>(w.phase), w.snapshot, w.index,
                debug_system::CInlineText16{ field }, actual, expected);
        }
        return failure(s, EReason::invalid_data);
    }
    bool evidence(const PlatformSubjectKind subject, const std::uint32_t id, const PlatformNativeIdentityKind kind,
        const std::uint32_t query_id, const SCcdKey native, const schema::STextReference label = { -1 }) noexcept;
    std::uint32_t adapter(const SCcdKey native, const std::uint32_t query_id) noexcept;
    std::uint32_t source_for_name(const wchar_t* const name, const std::uint32_t adapter_id = 0u) const noexcept;
    EReportStep ccd() noexcept;
    EReportStep translate() noexcept;
    bool observe(const DISPLAYCONFIG_PATH_INFO& path, SCoreObservation& observation) noexcept;
    bool record(const SCoreObservation& observation) noexcept;
    EReportStep device_info() noexcept;
    EReportStep gdi() noexcept;
    EReportStep dxgi() noexcept;
    EReportStep modes() noexcept;
    EReportStep finalise() noexcept;
    void begin_final() noexcept;
    std::uint32_t catalogue(const bool dxgi) noexcept;
    void next_format(const bool failed) noexcept;
    SReportSession& s;
    SWindowsReport& w;
    runtime::SReportCandidate& r;
    const PlatformCaptureLimits& limits;
};

std::uint32_t CWindowsQueries::query(const PlatformQueryKind kind, const PlatformQueryApi api,
    const PlatformSubjectKind subject, const std::uint32_t id) noexcept
{
    PlatformQueryResult row{};
    row.id = static_cast<std::uint32_t>(r.queries.size()) + 1u;
    row.kind = kind;
    row.api = api;
    row.subject_kind = subject;
    row.subject_id = id;
    row.status = PlatformQueryStatus::success;
    row.diagnostic.string_value_id = -1;
    if (!r.queries.push_back(row))
    {
        (void)failure(s, EReason::allocation_failed);
        return 0u;
    }
    return row.id;
}

void CWindowsQueries::missing(const std::uint32_t id, const PlatformQueryStatus status, const PlatformQueryReason reason,
    const PlatformErrorDomain domain, const std::uint32_t error) noexcept
{
    PlatformQueryResult& row = q(id);
    row.status = status;
    row.reason = reason;
    row.error_domain = domain;
    row.native_error = error;
    r.header.coverage = PlatformCoverage::limited;
}

template<typename F>
LONG CWindowsQueries::call(const std::uint32_t id, const F& f) noexcept
{
    if (!id || (s.native_calls >= limits.native_calls))
    {
        (void)failure(s, EReason::limit_exceeded);
        return ERROR_GEN_FAILURE;
    }
    std::uint64_t before{}, after{};
    if (s.clock.read(before).code != platform::threading::EWaitError::none)
    {
        (void)failure(s, EReason::clock_failed);
        return ERROR_GEN_FAILURE;
    }
    ++s.native_calls;
    PlatformQueryResult& row = q(id);
    ++row.call_count;
    if (!row.attempts) row.attempts = 1u;
    const LONG result = static_cast<LONG>(f());
    if ((s.clock.read(after).code != platform::threading::EWaitError::none) || (after < before))
    {
        (void)failure(s, EReason::clock_failed);
        return ERROR_GEN_FAILURE;
    }
    const modules::EBudgetStatus time = s.budget->observe().status;
    if ((time == modules::EBudgetStatus::failed) || (time == modules::EBudgetStatus::expired))
    {
        (void)failure(s, (time == modules::EBudgetStatus::failed) ? EReason::clock_failed : EReason::deadline_exceeded);
    }
    if ((result == E_OUTOFMEMORY) || (result == ERROR_NOT_ENOUGH_MEMORY) || (result == ERROR_OUTOFMEMORY))
    {
        s.failure = { reports::EStage::capture, EReason::allocation_failed,
            (result == E_OUTOFMEMORY) ? reports::EErrorDomain::hresult : reports::EErrorDomain::win32,
            static_cast<UINT32>(result) };
    }
    row.elapsed_ns += after - before;
    s.maximum_call_ns = std::max(s.maximum_call_ns, (after - before));
    r.header.capture_end_ns = after;
    return result;
}

template<std::size_t N, std::size_t F>
schema::STextReference CWindowsQueries::text(const wchar_t (&value)[N], const char (&field)[F]) noexcept
{
    std::size_t n{};
    while ((n < N) && value[n]) ++n;
    if (!n) return { -1 };
    if (n == N)
    {
        (void)reject(field, n, (N - 1u));
        return { -1 };
    }
    char utf8[N * 3u];
    const int size = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, value, static_cast<int>(n), utf8, sizeof(utf8), nullptr, nullptr);
    if (!size)
    {
        const DWORD error = GetLastError();
        MV_LOG(info, "Platform report native text field {} has invalid UTF-16, Win32 error {}",
            debug_system::CInlineText16{ field }, static_cast<std::uint32_t>(error));
        (void)reject(field, error, 0u);
        return { -1 };
    }
    const std::size_t id = r.text.append(reinterpret_cast<const std::uint8_t*>(utf8), size);
    if (!id || (id > INT32_MAX)) (void)failure(s, EReason::allocation_failed);
    return { static_cast<std::int32_t>(id) };
}

bool CWindowsQueries::evidence(const PlatformSubjectKind subject, const std::uint32_t id, const PlatformNativeIdentityKind kind,
    const std::uint32_t query_id, const SCcdKey native, const schema::STextReference label) noexcept
{
    PlatformNativeIdentity identity{};
    identity.kind = kind;
    identity.query_id = query_id;
    identity.text = label;
    identity.bytes = k_absent;
    if (kind <= PlatformNativeIdentityKind::windows_ccd_target)
    {
        const std::uint32_t length = (kind == PlatformNativeIdentityKind::windows_adapter_luid) ? 8u : 12u;
        identity.bytes = { 1, static_cast<std::int32_t>(r.bytes.size()), length };
        const std::uint32_t words[]{ native.low, native.high, native.endpoint };
        for (std::uint32_t i = 0u; i < length; ++i)
        {
            const PlatformByte byte{ static_cast<std::uint8_t>(words[i >> 2u] >> ((i & 3u) << 3u)) };
            if (!r.bytes.push_back(byte)) return false;
        }
    }
    else if (label.string_value_id <= 0) return true;
    identity.id = static_cast<std::uint32_t>(w.evidence.size() + 1u);
    return w.evidence.push_back({ subject, id, identity });
}

std::uint32_t CWindowsQueries::adapter(const SCcdKey native, const std::uint32_t query_id) noexcept
{
    const SCcdKey adapter_key{ native.low, native.high, 0u };
    for (std::size_t i = 0u; i < w.adapters.size(); ++i)
    {
        if (w.adapters[i].key == adapter_key)
        {
            return static_cast<std::uint32_t>(i + 1u);
        }
    }
    if ((q(query_id).api == PlatformQueryApi::windows_ccd) && (w.adapters.size() >= limits.adapters_per_backend))
    {
        (void)failure(s, EReason::limit_exceeded);
        return 0u;
    }
    PlatformAdapter row{};
    row.id = static_cast<std::uint32_t>(r.adapters.size() + 1u);
    row.identities = k_absent;
    row.label.string_value_id = -1;
    row.label_query_id = row.discovery_query_id = query_id;
    if (!r.adapters.push_back(row) || !w.adapters.push_back({ adapter_key }) ||
        !evidence(PlatformSubjectKind::adapter, row.id, PlatformNativeIdentityKind::windows_adapter_luid, query_id, adapter_key))
    {
        (void)failure(s, EReason::allocation_failed);
        return 0u;
    }
    return row.id;
}

EReportStep CWindowsQueries::ccd() noexcept
{
    const UINT32 flags = k_profiles[w.profile];
    const std::uint32_t k = w.snapshot;
    LONG code{};
    if (w.phase == EQueryPhase::ccd_size)
    {
        if (!w.query) w.query = query(k ? PlatformQueryKind::consistency_check : PlatformQueryKind::inventory, PlatformQueryApi::windows_ccd);
        if (!w.query) return EReportStep::failed;
        q(w.query).native_flags = flags;
        q(w.query).attempts = ++w.pass;
        code = call(w.query, [&]() noexcept { return w.api->ccd_sizes(flags, &w.paths[k], &w.modes[k]); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if (code == ERROR_SUCCESS)
        {
            if ((w.paths[k] > limits.ccd_paths) || (w.modes[k] > limits.ccd_modes))
            {
                return failure(s, EReason::limit_exceeded);
            }
            //  CCD requires non-null array addresses even when a sizing query returned zero.
            const std::size_t path_bytes = std::max(w.paths[k], 1u) * sizeof(DISPLAYCONFIG_PATH_INFO);
            const std::size_t mode_bytes = std::max(w.modes[k], 1u) * sizeof(DISPLAYCONFIG_MODE_INFO);
            if (!w.ccd_paths[k].reallocate(path_bytes, path_bytes, 16u) || !w.ccd_modes[k].reallocate(mode_bytes, mode_bytes, 16u))
            {
                return failure(s, EReason::allocation_failed);
            }
            w.phase = EQueryPhase::ccd_fetch;
            return EReportStep::more;
        }
    }
    else
    {
        const UINT32 path_capacity = w.paths[k], mode_capacity = w.modes[k];
        code = call(w.query, [&]() noexcept { return w.api->ccd_fetch(flags, &w.paths[k],
            reinterpret_cast<DISPLAYCONFIG_PATH_INFO*>(w.ccd_paths[k].data()), &w.modes[k],
            reinterpret_cast<DISPLAYCONFIG_MODE_INFO*>(w.ccd_modes[k].data()), nullptr); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if (code == ERROR_SUCCESS)
        {
            if (w.paths[k] > path_capacity)
            {
                return reject("ccd.path_count", w.paths[k], path_capacity);
            }
            if (w.modes[k] > mode_capacity)
            {
                return reject("ccd.mode_count", w.modes[k], mode_capacity);
            }
            q(w.query).returned_count = w.paths[k];
            if (k) r.header.consistency_query_id = w.query;
            else r.header.inventory_query_id = w.inventory_id = w.query;
            r.header.discovery = PlatformDiscovery::captured;
            w.index = 0u;
            w.phase = EQueryPhase::translate;
            return EReportStep::more;
        }
    }
    if ((code == ERROR_INVALID_PARAMETER) && !k && ((w.profile + 1u) < limits.flag_profiles))
    {
        missing(w.query, PlatformQueryStatus::unsupported, PlatformQueryReason::profile_fallback, PlatformErrorDomain::win32, code);
        ++w.profile;
        w.query = w.pass = 0u;
        w.phase = EQueryPhase::ccd_size;
        return EReportStep::more;
    }
    if (code == ERROR_INSUFFICIENT_BUFFER)
    {
        w.ccd_paths[k].deallocate();
        w.ccd_modes[k].deallocate();
        if (w.pass >= limits.query_passes)
        {
            return failure(s, EReason::retry_exhausted);
        }
        w.phase = EQueryPhase::ccd_size;
        return EReportStep::more;
    }
    if (s.failure.reason != EReason::none)
    {
        return EReportStep::failed;
    }
    if (!k && ((code == ERROR_ACCESS_DENIED) || (code == ERROR_NOT_SUPPORTED)))
    {
        const bool denied = code == ERROR_ACCESS_DENIED;
        missing(w.query, denied ? PlatformQueryStatus::unavailable : PlatformQueryStatus::unsupported,
            denied ? PlatformQueryReason::access_denied : PlatformQueryReason::api_not_supported, PlatformErrorDomain::win32, code);
        r.header.discovery = denied ? PlatformDiscovery::unavailable : PlatformDiscovery::unsupported;
        r.header.inventory_query_id = w.query;
        r.header.consistency_query_id = query(PlatformQueryKind::consistency_check, PlatformQueryApi::windows_ccd);
        if (!r.header.consistency_query_id)
        {
            return EReportStep::failed;
        }
        missing(r.header.consistency_query_id, PlatformQueryStatus::blocked, PlatformQueryReason::dependency_missing);
        q(r.header.consistency_query_id).blocked_by_query_id = w.query;
        w.phase = EQueryPhase::finalise;
        return EReportStep::more;
    }
    if (s.failure.reason == EReason::none)
    {
        s.failure = { reports::EStage::capture, EReason::native_failure, reports::EErrorDomain::win32, static_cast<std::uint32_t>(code) };
    }
    return EReportStep::failed;
}

static void log_mode_link(const DISPLAYCONFIG_PATH_INFO& path, const std::uint32_t component,
    const bool virtual_mode, const std::uint32_t index, const std::uint32_t count) noexcept
{
    using debug_system::CInlineText16;
    static constexpr CInlineText16 components[]{ CInlineText16{ "source" }, CInlineText16{ "target" }, CInlineText16{ "desktop_image" } };
    const std::uint32_t source_word = path.sourceInfo.modeInfoIdx;
    const std::uint32_t target_word = path.targetInfo.modeInfoIdx;
    constexpr std::uint32_t k_index_mask = UINT16_MAX;
    constexpr std::uint32_t k_index_shift = 16u;
    MV_LOG(info, "Platform report native mode link: component {} ({}), path flags {}, virtual mode {}, index {}, mode count {}",
        components[component], component, path.flags, virtual_mode, index, count);
    //  Show both raw union words and their packed fields, regardless of the selected interpretation.
    MV_LOG(info, "Platform report index words: source {}, target {}, clone group {}, source index {}, desktop image index {}, target index {}",
        source_word, target_word, (source_word & k_index_mask), (source_word >> k_index_shift),
        (target_word & k_index_mask), (target_word >> k_index_shift));
}

bool CWindowsQueries::observe(const DISPLAYCONFIG_PATH_INFO& p, SCoreObservation& o) noexcept
{
    const bool active = (p.flags & DISPLAYCONFIG_PATH_ACTIVE) != 0u;
    o.target = key(p.targetInfo.adapterId, p.targetInfo.id);
    o.target_flags = p.targetInfo.statusFlags;
    o.available = p.targetInfo.targetAvailable != FALSE;
    o.technology = p.targetInfo.outputTechnology;
    if (!active) return true;
    o.source = key(p.sourceInfo.adapterId, p.sourceInfo.id);
    o.source_flags = p.sourceInfo.statusFlags;
    o.path_flags = p.flags;
    const bool virtual_mode = (p.flags & DISPLAYCONFIG_PATH_SUPPORT_VIRTUAL_MODE) && (k_profiles[w.profile] & QDC_VIRTUAL_MODE_AWARE);
    const UINT32 indices[]{ virtual_mode ? p.sourceInfo.sourceModeInfoIdx : p.sourceInfo.modeInfoIdx,
        virtual_mode ? p.targetInfo.targetModeInfoIdx : p.targetInfo.modeInfoIdx,
        virtual_mode ? p.targetInfo.desktopModeInfoIdx : UINT32_MAX };
    const auto* const modes = reinterpret_cast<const DISPLAYCONFIG_MODE_INFO*>(w.ccd_modes[w.snapshot].data());
    for (UINT32 n = 0u; n < 3u; ++n)
    {
        const UINT32 index = indices[n];
        const auto log_link = [&]() noexcept
        {
            log_mode_link(p, n, virtual_mode, index, w.modes[w.snapshot]);
        };
        if ((index == UINT32_MAX) || (virtual_mode && (index == UINT16_MAX)))
        {
            continue;
        }
        if (index >= w.modes[w.snapshot])
        {
            log_link();
            (void)reject("mode.index", index, w.modes[w.snapshot]);
            return false;
        }
        const DISPLAYCONFIG_MODE_INFO& m = modes[index];
        const SCcdKey associated = n ? o.target : o.source;
        if (m.infoType != (n + 1u))
        {
            log_link();
            (void)reject("mode.infoType", m.infoType, (n + 1u));
            return false;
        }
        if (m.adapterId.LowPart != associated.low)
        {
            log_link();
            (void)reject("mode.luidLow", m.adapterId.LowPart, associated.low);
            return false;
        }
        if (static_cast<std::uint32_t>(m.adapterId.HighPart) != associated.high)
        {
            log_link();
            (void)reject("mode.luidHigh", static_cast<std::uint32_t>(m.adapterId.HighPart), associated.high);
            return false;
        }
        if (m.id != associated.endpoint)
        {
            log_link();
            (void)reject("mode.id", m.id, associated.endpoint);
            return false;
        }
        if (!n)
        {
            const DISPLAYCONFIG_SOURCE_MODE& source = m.sourceMode;
            o.desktop.rectangle = { source.position.x, source.position.y, source.width, source.height };
            const UINT32 code = source.pixelFormat;
            o.desktop.format = { ((code >= 1u) && (code <= 4u)) ? static_cast<PlatformFormat>(code + 2u) : PlatformFormat::other, code };
            o.desktop.known_fields = PlatformDesktopFields::rectangle | PlatformDesktopFields::format;
            if ((code >= 1u) && (code <= 4u))
            {
                o.desktop.known_fields |= PlatformDesktopFields::depth;
                o.desktop.bits_per_pixel = code << 3u;
            }
        }
        else if (n == 1u)
        {
            const auto& signal = m.targetMode.targetVideoSignalInfo;
            o.signal.extent = { signal.activeSize.cx, signal.activeSize.cy };
            o.signal.physical_refresh = rational(signal.vSyncFreq.Numerator, signal.vSyncFreq.Denominator);
            o.signal.known_fields |= PlatformSignalFields::extent | PlatformSignalFields::physical_refresh;
        }
        else
        {
            const DISPLAYCONFIG_DESKTOP_IMAGE_INFO& image = m.desktopImageInfo;
            if (image.PathSourceSize.x < 0)
            {
                log_link();
                (void)reject("image.width", image.PathSourceSize.x, 0u);
                return false;
            }
            if (image.PathSourceSize.y < 0)
            {
                log_link();
                (void)reject("image.height", image.PathSourceSize.y, 0u);
                return false;
            }
            if (!rectangle(image.DesktopImageRegion, o.image.image_region))
            {
                log_link();
                MV_LOG(info, "Platform report rejected DesktopImageRegion: left {}, top {}, right {}, bottom {}",
                    static_cast<std::int32_t>(image.DesktopImageRegion.left), static_cast<std::int32_t>(image.DesktopImageRegion.top),
                    static_cast<std::int32_t>(image.DesktopImageRegion.right), static_cast<std::int32_t>(image.DesktopImageRegion.bottom));
                (void)reject("image.region", 1u, 0u);
                return false;
            }
            if (!rectangle(image.DesktopImageClip, o.image.image_clip))
            {
                log_link();
                MV_LOG(info, "Platform report rejected DesktopImageClip: left {}, top {}, right {}, bottom {}",
                    static_cast<std::int32_t>(image.DesktopImageClip.left), static_cast<std::int32_t>(image.DesktopImageClip.top),
                    static_cast<std::int32_t>(image.DesktopImageClip.right), static_cast<std::int32_t>(image.DesktopImageClip.bottom));
                (void)reject("image.clip", 1u, 0u);
                return false;
            }
            o.image.path_source_extent = { static_cast<UINT32>(image.PathSourceSize.x), static_cast<UINT32>(image.PathSourceSize.y) };
            o.image.known_fields = 7u;
        }
    }
    const auto& t = p.targetInfo;
    o.signal.reported_refresh = rational(t.refreshRate.Numerator, t.refreshRate.Denominator);
    o.signal.scan = scan(t.scanLineOrdering);
    static constexpr PlatformRotation rotations[]{ PlatformRotation::other, PlatformRotation::identity,
        PlatformRotation::counterclockwise_270, PlatformRotation::counterclockwise_180, PlatformRotation::counterclockwise_90 };
    o.signal.rotation = { (static_cast<UINT32>(t.rotation) < 5u) ? rotations[t.rotation] : PlatformRotation::other,
        static_cast<UINT32>(t.rotation) };
    static constexpr PlatformScaling scalings[]{ PlatformScaling::other, PlatformScaling::identity, PlatformScaling::centered,
        PlatformScaling::stretched, PlatformScaling::aspect_preserving, PlatformScaling::custom };
    o.signal.scaling = { (t.scaling == DISPLAYCONFIG_SCALING_PREFERRED) ? PlatformScaling::preferred :
        ((static_cast<UINT32>(t.scaling) < 6u) ? scalings[t.scaling] : PlatformScaling::other), static_cast<UINT32>(t.scaling) };
    o.signal.known_fields |= PlatformSignalFields::reported_refresh | PlatformSignalFields::scan |
        PlatformSignalFields::rotation | PlatformSignalFields::scaling;
    if ((k_profiles[w.profile] & QDC_VIRTUAL_REFRESH_RATE_AWARE) && (p.flags & DISPLAYCONFIG_PATH_BOOST_REFRESH_RATE))
    {
        o.signal.virtual_refresh = o.signal.reported_refresh;
        o.signal.known_fields |= PlatformSignalFields::virtual_refresh;
    }
    return true;
}

static std::uint32_t find_subject(const TPodVector<SReportSubject>& subjects, const SCcdKey key) noexcept
{
    for (std::size_t i = 0u; i < subjects.size(); ++i)
    {
        if (subjects[i].key == key)
        {
            return static_cast<std::uint32_t>(i + 1u);
        }
    }
    return 0u;
}

bool CWindowsQueries::record(const SCoreObservation& o) noexcept
{
    const bool active = (o.path_flags & DISPLAYCONFIG_PATH_ACTIVE) != 0u;
    const std::uint32_t target_adapter = adapter(o.target, w.query);
    if (!target_adapter) return false;
    std::uint32_t target = find_subject(w.displays, o.target);
    if (!target)
    {
        if (w.displays.size() == limits.displays)
        {
            (void)failure(s, EReason::limit_exceeded);
            return false;
        }
        PlatformDisplay display{};
        target = display.id = static_cast<std::uint32_t>(r.displays.size() + 1u);
        display.adapter_id = target_adapter;
        display.identities = k_absent;
        display.label.string_value_id = -1;
        display.label_query_id = display.connection_query_id = display.state_query_id = w.query;
        display.connection = connection(static_cast<DISPLAYCONFIG_VIDEO_OUTPUT_TECHNOLOGY>(o.technology));
        display.active = active ? PlatformTruth::yes : PlatformTruth::no;
        display.available = o.available ? PlatformTruth::yes : PlatformTruth::no;
        display.native_flags = o.target_flags;
        if (!w.displays.push_back({ o.target }) || !r.displays.push_back(display) ||
            !evidence(PlatformSubjectKind::display, target, PlatformNativeIdentityKind::windows_ccd_target, w.query, o.target))
        {
            return false;
        }
    }
    else
    {
        PlatformDisplay& display = r.displays[target - 1u];
        const PlatformTruth available = o.available ? PlatformTruth::yes : PlatformTruth::no;
        if (display.available != available)
        {
            (void)reject("display.avail", static_cast<std::uint32_t>(available), static_cast<std::uint32_t>(display.available));
            return false;
        }
        if (display.connection.native_code != o.technology)
        {
            (void)reject("display.tech", o.technology, display.connection.native_code);
            return false;
        }
        if (active)
        {
            display.active = PlatformTruth::yes;
            display.native_flags = o.target_flags;
        }
    }
    if (!active) return true;
    std::uint32_t source = find_subject(w.sources, o.source);
    if (!source)
    {
        if (w.sources.size() == limits.sources)
        {
            (void)failure(s, EReason::limit_exceeded);
            return false;
        }
        PlatformDisplaySource row{};
        source = row.id = static_cast<std::uint32_t>(r.sources.size() + 1u);
        row.adapter_id = adapter(o.source, w.query);
        row.active = PlatformTruth::yes;
        row.identities = k_absent;
        row.desktop = o.desktop;
        row.native_flags = o.source_flags;
        row.topology_query_id = row.annotation_query_id = row.gdi_current.query_id = row.desktop.query_id = w.query;
        if (!row.adapter_id || !r.sources.push_back(row) || !w.sources.push_back({ o.source }) ||
            !evidence(PlatformSubjectKind::source, source, PlatformNativeIdentityKind::windows_ccd_source, w.query, o.source))
        {
            return false;
        }
    }
    else
    {
        PlatformDesktopState desktop = r.sources[source - 1u].desktop;
        desktop.query_id = 0u;
        const int comparison = std::memcmp(&desktop, &o.desktop, sizeof(desktop));
        if (comparison != 0)
        {
            (void)reject("source.desktop", comparison, 0u);
            return false;
        }
    }
    PlatformDisplayPath path{};
    path.id = static_cast<std::uint32_t>(r.paths.size() + 1u);
    path.source_id = source;
    path.display_id = target;
    path.signal = o.signal;
    path.desktop_image = o.image;
    path.query_id = path.desktop_image.query_id = path.signal.query_id = w.query;
    path.native_flags = o.path_flags;
    return r.paths.push_back(path);
}

EReportStep CWindowsQueries::translate() noexcept
{
    const UINT32 k = w.snapshot;
    const auto* const paths = reinterpret_cast<const DISPLAYCONFIG_PATH_INFO*>(w.ccd_paths[k].data());
    while (w.index < w.paths[k])
    {
        const DISPLAYCONFIG_PATH_INFO& p = paths[w.index++];
        if ((p.flags & DISPLAYCONFIG_PATH_ACTIVE) || p.targetInfo.targetAvailable)
        {
            SCoreObservation observation{};
            if (!observe(p, observation))
            {
                return failure(s, EReason::invalid_data);
            }
            if (!k && (p.flags & DISPLAYCONFIG_PATH_ACTIVE) &&
                (!(observation.desktop.known_fields & PlatformDesktopFields::rectangle) ||
                !(observation.signal.known_fields & PlatformSignalFields::extent)))
            {
                missing(w.query, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
            }
            if (!w.core[k].push_back(observation) || (!k && !record(observation)))
            {
                return failure(s, EReason::allocation_failed);
            }
        }
        std::uint64_t now{};
        if (s.clock.read(now).code != platform::threading::EWaitError::none)
        {
            return failure(s, EReason::clock_failed);
        }
        if (now >= s.slice_end_ns) return EReportStep::more;
    }
    w.ccd_paths[k].deallocate();
    w.ccd_modes[k].deallocate();
    w.index = w.query = 0u;
    w.phase = k ? EQueryPhase::compare : EQueryPhase::adapter_name;
    return EReportStep::more;
}

EReportStep CWindowsQueries::device_info() noexcept
{
    const bool is_adapter = w.phase == EQueryPhase::adapter_name;
    const bool is_source = w.phase == EQueryPhase::source_name;
    auto& subjects = is_adapter ? w.adapters : (is_source ? w.sources : w.displays);
    if (w.index >= subjects.size())
    {
        w.index = 0u;
        w.phase = is_adapter ? EQueryPhase::source_name : (is_source ? EQueryPhase::target_name : EQueryPhase::gdi_adapter);
        return EReportStep::more;
    }
    SReportSubject& subject = subjects[w.index];
    const UINT32 id = ++w.index;
    const PlatformSubjectKind kind = is_adapter ? PlatformSubjectKind::adapter : (is_source ? PlatformSubjectKind::source : PlatformSubjectKind::display);
    const UINT32 qid = query(is_adapter ? PlatformQueryKind::adapter_identity : (is_source ? PlatformQueryKind::source_identity :
        PlatformQueryKind::display_identity), PlatformQueryApi::windows_display_device_info, kind, id);
    if (!qid) return EReportStep::failed;
    subject.name_query = qid;
    const auto get = [&](auto& packet, const DISPLAYCONFIG_DEVICE_INFO_TYPE type) noexcept
    {
        packet.header.type = type;
        packet.header.size = sizeof(packet);
        packet.header.id = subject.key.endpoint;
        packet.header.adapterId = luid(subject.key);
        return call(qid, [&]() noexcept { return w.api->device_info(&packet.header); });
    };
    LONG code{};
    schema::STextReference name{ -1 };
    PlatformNativeIdentityKind identity{};
    if (is_adapter)
    {
        DISPLAYCONFIG_ADAPTER_NAME packet{};
        code = get(packet, DISPLAYCONFIG_DEVICE_INFO_GET_ADAPTER_NAME);
        identity = PlatformNativeIdentityKind::windows_adapter_device_path;
        if (!code) name = text(packet.adapterDevicePath, "adapter.path");
    }
    else if (is_source)
    {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME packet{};
        code = get(packet, DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME);
        identity = PlatformNativeIdentityKind::windows_gdi_source_name;
        if (!code)
        {
            name = text(packet.viewGdiDeviceName, "source.gdi");
            if (name.string_value_id > 0) std::memcpy(subject.name, packet.viewGdiDeviceName, sizeof(subject.name));
        }
    }
    else
    {
        DISPLAYCONFIG_TARGET_DEVICE_NAME packet{};
        code = get(packet, DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME);
        PlatformDisplay& display = r.displays[id - 1u];
        display.label_query_id = qid;
        identity = PlatformNativeIdentityKind::windows_monitor_device_path;
        if (!code)
        {
            name = text(packet.monitorDevicePath, "monitor.path");
            display.label = text(packet.monitorFriendlyDeviceName, "monitor.name");
            if (display.label.string_value_id < 0) missing(qid, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
        }
    }
    if (code) missing(qid, PlatformQueryStatus::failed, PlatformQueryReason::native_failure, PlatformErrorDomain::win32, code);
    else
    {
        q(qid).returned_count = 1u;
        if (name.string_value_id < 0) missing(qid, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
        if (!evidence(kind, id, identity, qid, subject.key, name))
        {
            return failure(s, EReason::allocation_failed);
        }
    }
    return EReportStep::more;
}

std::uint32_t CWindowsQueries::source_for_name(const wchar_t* const name, const std::uint32_t adapter_id) const noexcept
{
    std::uint32_t found{};
    if (!name[0]) return 0u;
    for (std::size_t i = 0u; i < w.sources.size(); ++i)
    {
        if ((!adapter_id || (r.sources[i].adapter_id == adapter_id)) && (wcscmp(w.sources[i].name, name) == 0))
        {
            if (found) return 0u;
            found = r.sources[i].id;
        }
    }
    return found;
}

static PlatformRotationValue gdi_rotation(const DWORD value) noexcept
{
    static constexpr PlatformRotation map[]{ PlatformRotation::identity, PlatformRotation::counterclockwise_90,
        PlatformRotation::counterclockwise_180, PlatformRotation::counterclockwise_270 };
    return { (value < 4u) ? map[value] : PlatformRotation::other, value };
}

static PlatformRefresh gdi_refresh(const DWORD hz) noexcept
{
    return (hz <= 1u) ? PlatformRefresh{ PlatformRefreshPrecision::unspecified, 0u, 0u } :
        PlatformRefresh{ PlatformRefreshPrecision::integer_hz, hz, 1u };
}

EReportStep CWindowsQueries::gdi() noexcept
{
    if (w.phase == EQueryPhase::gdi_adapter)
    {
        if (!w.gdi_inventory) w.gdi_inventory = query(PlatformQueryKind::desktop_annotation, PlatformQueryApi::windows_gdi_devices);
        if (!w.gdi_inventory) return EReportStep::failed;
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        const LONG ok = call(w.gdi_inventory, [&]() noexcept { return w.api->devices(nullptr, w.adapter_index, &device, 0u); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if (!ok)
        {
            w.index = w.adapter_index = 0u;
            w.phase = EQueryPhase::current_settings;
            return EReportStep::more;
        }
        if (w.adapter_index++ >= limits.adapters_per_backend)
        {
            return failure(s, EReason::limit_exceeded);
        }
        ++q(w.gdi_inventory).returned_count;
        const schema::STextReference name = text(device.DeviceName, "gdi.name");
        if ((name.string_value_id <= 0) || (s.failure.reason != EReason::none))
        {
            return reject("gdi.name", name.string_value_id, 1u);
        }
        std::memcpy(w.gdi_name, device.DeviceName, sizeof(w.gdi_name));
        w.gdi_source = source_for_name(w.gdi_name);
        if (w.gdi_source)
        {
            PlatformDisplaySource& source = r.sources[w.gdi_source - 1u];
            source.primary = (device.StateFlags & DISPLAY_DEVICE_PRIMARY_DEVICE) ? PlatformTruth::yes : PlatformTruth::no;
            source.desktop_attached = (device.StateFlags & DISPLAY_DEVICE_ATTACHED_TO_DESKTOP) ? PlatformTruth::yes : PlatformTruth::no;
            source.annotation_query_id = w.gdi_inventory;
        }
        w.monitor_index = 0u;
        w.query = query(PlatformQueryKind::desktop_annotation, PlatformQueryApi::windows_gdi_devices,
            w.gdi_source ? PlatformSubjectKind::source : PlatformSubjectKind::report, w.gdi_source);
        if (!w.query) return EReportStep::failed;
        q(w.query).native_flags = EDD_GET_DEVICE_INTERFACE_NAME;
        w.phase = EQueryPhase::gdi_monitor;
        return EReportStep::more;
    }
    if (w.phase == EQueryPhase::gdi_monitor)
    {
        DISPLAY_DEVICEW device{};
        device.cb = sizeof(device);
        const LONG ok = call(w.query, [&]() noexcept { return w.api->devices(w.gdi_name, w.monitor_index, &device, EDD_GET_DEVICE_INTERFACE_NAME); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if (!ok)
        {
            w.phase = EQueryPhase::gdi_adapter;
            return EReportStep::more;
        }
        if (++w.monitor_count > limits.monitor_records)
        {
            return failure(s, EReason::limit_exceeded);
        }
        ++w.monitor_index;
        ++q(w.query).returned_count;
        const schema::STextReference name = text(device.DeviceID, "monitor.id");
        std::uint32_t display{};
        for (std::size_t i = 0u; i < w.evidence.size(); ++i)
        {
            const SIdentityObservation& evidence = w.evidence[i];
            if ((name.string_value_id > 0) && (evidence.identity.kind == PlatformNativeIdentityKind::windows_monitor_device_path) &&
                (evidence.identity.text.string_value_id == name.string_value_id))
            {
                if (display)
                {
                    display = 0u;
                    break;
                }
                display = evidence.subject_id;
            }
        }
        if (display && !evidence(PlatformSubjectKind::display, display, PlatformNativeIdentityKind::windows_monitor_interface_name, w.query, {}, name))
        {
            return failure(s, EReason::allocation_failed);
        }
        if (!display) missing(w.query, PlatformQueryStatus::success, PlatformQueryReason::identity_ambiguous);
        return EReportStep::more;
    }
    if (w.index >= w.sources.size())
    {
        w.index = 0u;
        if (w.phase == EQueryPhase::current_settings) w.phase = EQueryPhase::factory;
        else begin_final();
        return EReportStep::more;
    }
    SReportSubject& source = w.sources[w.index];
    const UINT32 id = w.index + 1u;
    if (w.phase == EQueryPhase::current_settings)
    {
        ++w.index;
        const UINT32 qid = query(PlatformQueryKind::current_desktop_settings, PlatformQueryApi::windows_gdi_settings, PlatformSubjectKind::source, id);
        if (!qid) return EReportStep::failed;
        PlatformDesktopState& desktop = r.sources[id - 1u].gdi_current;
        desktop.query_id = qid;
        PlatformDisplaySource& row = r.sources[id - 1u];
        if (row.annotation_query_id == w.inventory_id)
        {
            row.annotation_query_id = query(PlatformQueryKind::desktop_annotation, PlatformQueryApi::windows_gdi_devices,
                PlatformSubjectKind::source, id);
            if (!row.annotation_query_id)
            {
                return EReportStep::failed;
            }
            missing(row.annotation_query_id, PlatformQueryStatus::blocked, PlatformQueryReason::identity_ambiguous);
            q(row.annotation_query_id).blocked_by_query_id = w.gdi_inventory;
        }
        if (!source.name[0])
        {
            missing(qid, PlatformQueryStatus::blocked, PlatformQueryReason::dependency_missing);
            q(qid).blocked_by_query_id = source.name_query;
            return EReportStep::more;
        }
        DEVMODEW mode{};
        mode.dmSize = sizeof(mode);
        const LONG ok = call(qid, [&]() noexcept { return w.api->settings(source.name, ENUM_CURRENT_SETTINGS, &mode, 0u); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if (!ok)
        {
            missing(qid, PlatformQueryStatus::failed, PlatformQueryReason::native_failure);
            return EReportStep::more;
        }
        q(qid).returned_count = 1u;
        constexpr DWORD rectangle_fields = DM_POSITION | DM_PELSWIDTH | DM_PELSHEIGHT;
        if ((mode.dmFields & rectangle_fields) == rectangle_fields)
        {
            desktop.known_fields |= PlatformDesktopFields::rectangle;
            desktop.rectangle = { mode.dmPosition.x, mode.dmPosition.y, mode.dmPelsWidth, mode.dmPelsHeight };
        }
        if (mode.dmFields & DM_BITSPERPEL)
        {
            desktop.known_fields |= PlatformDesktopFields::depth;
            desktop.bits_per_pixel = mode.dmBitsPerPel;
        }
        if (mode.dmFields & DM_DISPLAYFREQUENCY)
        {
            desktop.known_fields |= PlatformDesktopFields::refresh;
            desktop.refresh = gdi_refresh(mode.dmDisplayFrequency);
        }
        if (mode.dmFields & DM_DISPLAYORIENTATION)
        {
            desktop.known_fields |= PlatformDesktopFields::rotation;
            desktop.rotation = gdi_rotation(mode.dmDisplayOrientation);
        }
        if (mode.dmFields & DM_DISPLAYFIXEDOUTPUT)
        {
            desktop.known_fields |= PlatformDesktopFields::scaling;
            static constexpr PlatformScaling map[]{ PlatformScaling::preferred, PlatformScaling::stretched, PlatformScaling::centered };
            desktop.scaling = { (mode.dmDisplayFixedOutput < 3u) ? map[mode.dmDisplayFixedOutput] : PlatformScaling::other,
                mode.dmDisplayFixedOutput };
        }
        constexpr UINT32 expected = PlatformDesktopFields::rectangle | PlatformDesktopFields::depth | PlatformDesktopFields::refresh |
            PlatformDesktopFields::rotation | PlatformDesktopFields::scaling;
        if ((desktop.known_fields & expected) != expected)
        {
            missing(qid, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
        }
        return EReportStep::more;
    }
    if (source.dxgi_complete && !source.fallback_needed)
    {
        ++w.index;
        return EReportStep::more;
    }
    w.source_id = id;
    w.adapter_id = r.sources[w.index].adapter_id;
    if (!w.catalogue_id)
    {
        w.catalogue_id = catalogue(false);
        if (!w.catalogue_id) return EReportStep::failed;
        w.mode_index = 0u;
    }
    PlatformModeCatalogue& list = r.catalogues[w.catalogue_id - 1u];
    if (!source.name[0])
    {
        missing(list.query_id, PlatformQueryStatus::blocked, PlatformQueryReason::dependency_missing);
        q(list.query_id).blocked_by_query_id = source.name_query;
        w.catalogue_id = 0u;
        ++w.index;
        return EReportStep::more;
    }
    DEVMODEW mode{};
    mode.dmSize = sizeof(mode);
    const LONG ok = call(list.query_id, [&]() noexcept { return w.api->settings(source.name, w.mode_index, &mode, 0u); });
    if (s.failure.reason != EReason::none)
    {
        return EReportStep::failed;
    }
    if (!ok)
    {
        if (!w.mode_index) missing(list.query_id, PlatformQueryStatus::unavailable, PlatformQueryReason::value_not_reported);
        else list.modes = { 1, static_cast<std::int32_t>(w.mode_first), w.mode_index };
        w.catalogue_id = 0u;
        ++w.index;
        return EReportStep::more;
    }
    if ((w.mode_index >= limits.modes_per_scope) || (s.raw_modes >= limits.total_mode_rows))
    {
        return failure(s, EReason::limit_exceeded);
    }
    ++s.raw_modes;
    ++w.mode_index;
    q(list.query_id).returned_count = w.mode_index;
    PlatformDisplayMode row{};
    row.id = static_cast<UINT32>(r.modes.size() + 1u);
    row.catalogue_id = list.id;
    if ((mode.dmFields & (DM_PELSWIDTH | DM_PELSHEIGHT)) == (DM_PELSWIDTH | DM_PELSHEIGHT))
    {
        row.known_fields |= PlatformModeFields::extent;
        row.extent = { mode.dmPelsWidth, mode.dmPelsHeight };
    }
    if (mode.dmFields & DM_BITSPERPEL)
    {
        row.known_fields |= PlatformModeFields::depth;
        row.bits_per_pixel = mode.dmBitsPerPel;
    }
    if (mode.dmFields & DM_DISPLAYFREQUENCY)
    {
        row.known_fields |= PlatformModeFields::refresh;
        row.refresh = gdi_refresh(mode.dmDisplayFrequency);
    }
    if (mode.dmFields & DM_DISPLAYFLAGS)
    {
        row.known_fields |= PlatformModeFields::scan;
        row.scan = { (mode.dmDisplayFlags & DM_INTERLACED) ? PlatformScan::interlaced_unspecified : PlatformScan::progressive, mode.dmDisplayFlags };
    }
    if (mode.dmFields & DM_DISPLAYORIENTATION)
    {
        row.known_fields |= PlatformModeFields::rotation;
        row.rotation = gdi_rotation(mode.dmDisplayOrientation);
    }
    constexpr UINT32 expected = PlatformModeFields::extent | PlatformModeFields::depth | PlatformModeFields::refresh |
        PlatformModeFields::scan | PlatformModeFields::rotation;
    if ((row.known_fields & expected) != expected)
    {
        missing(list.query_id, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
    }
    return r.modes.push_back(row) ? EReportStep::more : failure(s, EReason::allocation_failed);
}

std::uint32_t CWindowsQueries::catalogue(const bool dxgi) noexcept
{
    PlatformModeCatalogue row{};
    row.id = static_cast<UINT32>(r.catalogues.size() + 1u);
    row.scope = dxgi ? PlatformModeScope::native_output : PlatformModeScope::source;
    row.adapter_id = w.adapter_id;
    row.source_id = w.source_id;
    row.identities = row.modes = k_absent;
    row.filter_flags = dxgi ? (PlatformCatalogueFilters::interlaced | PlatformCatalogueFilters::scaled |
        PlatformCatalogueFilters::stereo_excluded | PlatformCatalogueFilters::explicit_format) :
        (PlatformCatalogueFilters::monitor_compatible | PlatformCatalogueFilters::current_orientation);
    if (dxgi) row.format_filter = { w.format_index ? PlatformFormat::bgra8_unorm : PlatformFormat::rgba8_unorm,
        static_cast<UINT32>(k_formats[w.format_index]) };
    row.query_id = query(PlatformQueryKind::mode_catalogue, dxgi ? PlatformQueryApi::windows_dxgi : PlatformQueryApi::windows_gdi_settings,
        PlatformSubjectKind::mode_catalogue, row.id);
    if (!row.query_id) return 0u;
    q(row.query_id).native_flags = dxgi ? k_mode_flags : 0u;
    if (!r.catalogues.push_back(row) || (dxgi && !evidence(PlatformSubjectKind::mode_catalogue, row.id,
        PlatformNativeIdentityKind::windows_dxgi_output_name, w.output_description, {}, text(w.output_name, "dxgi.name"))))
    {
        (void)failure(s, EReason::allocation_failed);
        return 0u;
    }
    w.mode_first = static_cast<UINT32>(r.modes.size());
    return row.id;
}

void CWindowsQueries::next_format(const bool failed) noexcept
{
    w.list_failed |= failed;
    w.catalogue_id = w.pass = w.mode_index = 0u;
    w.mode_buffer.deallocate();
    if (++w.format_index < 2u)
    {
        w.phase = EQueryPhase::mode_count;
        return;
    }
    if (w.source_id)
    {
        SReportSubject& source = w.sources[w.source_id - 1u];
        source.dxgi_complete |= !w.list_failed;
        source.fallback_needed |= w.list_failed;
    }
    release(*w.api, w.output1);
    release(*w.api, w.output);
    w.phase = EQueryPhase::output;
}

EReportStep CWindowsQueries::dxgi() noexcept
{
    const auto failed = [&](const std::uint32_t id, const HRESULT hr) noexcept
    {
        missing(id, (hr == DXGI_ERROR_UNSUPPORTED) ? PlatformQueryStatus::unsupported : PlatformQueryStatus::failed,
            (hr == DXGI_ERROR_UNSUPPORTED) ? PlatformQueryReason::api_not_supported : PlatformQueryReason::native_failure,
            PlatformErrorDomain::hresult, static_cast<UINT32>(hr));
    };
    switch (w.phase)
    {
        case EQueryPhase::factory:
        {
            w.dxgi_inventory = query(PlatformQueryKind::adapter_output_inventory, PlatformQueryApi::windows_dxgi);
            if (!w.dxgi_inventory)
            {
                return EReportStep::failed;
            }
            const HRESULT hr = call(w.dxgi_inventory, [&]() noexcept { return w.api->factory(&w.factory); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr))
            {
                failed(w.dxgi_inventory, hr);
                w.phase = EQueryPhase::gdi_mode;
            }
            else w.phase = EQueryPhase::dxgi_adapter;
            break;
        }
        case EQueryPhase::dxgi_adapter:
        {
            const HRESULT hr = call(w.dxgi_inventory, [&]() noexcept { return w.api->adapter(w.factory, w.adapter_index, &w.adapter); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr))
            {
                if (hr != DXGI_ERROR_NOT_FOUND) failed(w.dxgi_inventory, hr);
                release(*w.api, w.factory);
                w.index = 0u;
                w.phase = EQueryPhase::gdi_mode;
            }
            else if (++w.adapter_index > limits.adapters_per_backend)
            {
                return failure(s, EReason::limit_exceeded);
            }
            else
            {
                ++q(w.dxgi_inventory).returned_count;
                w.phase = EQueryPhase::adapter_desc;
            }
            break;
        }
        case EQueryPhase::adapter_desc:
        {
            const UINT32 id = query(PlatformQueryKind::adapter_description, PlatformQueryApi::windows_dxgi);
            if (!id) return EReportStep::failed;
            DXGI_ADAPTER_DESC1 desc{};
            const HRESULT hr = call(id, [&]() noexcept { return w.api->adapter_desc(w.adapter, &desc); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr))
            {
                failed(id, hr);
                PlatformAdapter row{};
                row.id = static_cast<UINT32>(r.adapters.size() + 1u);
                row.label = { -1 };
                row.identities = k_absent;
                row.label_query_id = id;
                row.discovery_query_id = w.dxgi_inventory;
                if (!r.adapters.push_back(row) || !w.adapters.push_back({}))
                {
                    return failure(s, EReason::allocation_failed);
                }
                w.adapter_id = row.id;
            }
            else
            {
                w.adapter_id = adapter(key(desc.AdapterLuid), id);
                if (!w.adapter_id)
                {
                    return EReportStep::failed;
                }
                PlatformAdapter& row = r.adapters[w.adapter_id - 1u];
                row.label = text(desc.Description, "adapter.desc");
                if (row.label.string_value_id < 0) missing(id, PlatformQueryStatus::success, PlatformQueryReason::value_not_reported);
                row.label_query_id = id;
                q(id).returned_count = 1u;
            }
            q(id).subject_kind = PlatformSubjectKind::adapter;
            q(id).subject_id = w.adapter_id;
            w.query = query(PlatformQueryKind::adapter_output_inventory, PlatformQueryApi::windows_dxgi, PlatformSubjectKind::adapter, w.adapter_id);
            if (!w.query) return EReportStep::failed;
            w.output_index = 0u;
            w.phase = EQueryPhase::output;
            break;
        }
        case EQueryPhase::output:
        {
            const HRESULT hr = call(w.query, [&]() noexcept { return w.api->output(w.adapter, w.output_index, &w.output); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr))
            {
                if (hr != DXGI_ERROR_NOT_FOUND) failed(w.query, hr);
                release(*w.api, w.adapter);
                w.phase = EQueryPhase::dxgi_adapter;
            }
            else
            {
                if (++w.output_count > limits.outputs)
                {
                    return failure(s, EReason::limit_exceeded);
                }
                ++w.output_index;
                ++q(w.query).returned_count;
                w.phase = EQueryPhase::output_desc;
            }
            break;
        }
        case EQueryPhase::output_desc:
        {
            w.output_description = query(PlatformQueryKind::output_description, PlatformQueryApi::windows_dxgi,
                PlatformSubjectKind::adapter, w.adapter_id);
            if (!w.output_description)
            {
                return EReportStep::failed;
            }
            DXGI_OUTPUT_DESC desc{};
            w.output_name[0] = L'\0';
            w.source_id = w.format_index = 0u;
            w.list_failed = false;
            const HRESULT hr = call(w.output_description, [&]() noexcept { return w.api->output_desc(w.output, &desc); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr)) failed(w.output_description, hr);
            else
            {
                q(w.output_description).returned_count = 1u;
                if (text(desc.DeviceName, "output.name").string_value_id > 0)
                {
                    std::memcpy(w.output_name, desc.DeviceName, sizeof(w.output_name));
                    w.source_id = source_for_name(w.output_name, w.adapter_id);
                }
                if (!w.source_id) missing(w.output_description, PlatformQueryStatus::success, PlatformQueryReason::identity_ambiguous);
            }
            w.phase = EQueryPhase::output_interface;
            break;
        }
        case EQueryPhase::output_interface:
        {
            // Interface availability and the independently obtained output name can have different outcomes.
            w.output_interface_query = query(PlatformQueryKind::adapter_output_inventory, PlatformQueryApi::windows_dxgi,
                PlatformSubjectKind::adapter, w.adapter_id);
            if (!w.output_interface_query) return EReportStep::failed;
            const HRESULT hr = call(w.output_interface_query, [&]() noexcept { return w.api->output1(w.output, &w.output1); });
            if (s.failure.reason != EReason::none)
            {
                return EReportStep::failed;
            }
            if (FAILED(hr)) failed(w.output_interface_query, hr);
            else q(w.output_interface_query).returned_count = 1u;
            w.phase = EQueryPhase::mode_count;
            break;
        }
        default:
        {
            return reject("dxgi.phase", static_cast<std::uint32_t>(w.phase), static_cast<std::uint32_t>(EQueryPhase::output_interface));
        }
    }
    return EReportStep::more;
}

EReportStep CWindowsQueries::modes() noexcept
{
    if (!w.catalogue_id)
    {
        w.catalogue_id = catalogue(true);
        if (!w.catalogue_id) return EReportStep::failed;
    }
    PlatformModeCatalogue& list = r.catalogues[w.catalogue_id - 1u];
    if (!w.output1)
    {
        missing(list.query_id, PlatformQueryStatus::blocked, PlatformQueryReason::dependency_missing);
        q(list.query_id).blocked_by_query_id = w.output_interface_query;
        next_format(true);
        return EReportStep::more;
    }
    const DXGI_FORMAT format = k_formats[w.format_index];
    if (w.phase != EQueryPhase::mode_rows)
    {
        const bool sizing = w.phase == EQueryPhase::mode_count;
        if (sizing) q(list.query_id).attempts = ++w.pass;
        const UINT capacity = w.mode_count;
        auto* const buffer = sizing ? nullptr : reinterpret_cast<DXGI_MODE_DESC1*>(w.mode_buffer.data());
        const HRESULT hr = call(list.query_id, [&]() noexcept { return w.api->modes(w.output1, format, k_mode_flags, &w.mode_count, buffer); });
        if (s.failure.reason != EReason::none)
        {
            return EReportStep::failed;
        }
        if ((SUCCEEDED(hr) || (hr == DXGI_ERROR_MORE_DATA)) && ((w.mode_count > limits.modes_per_scope) ||
            (w.mode_count > (limits.total_mode_rows - s.raw_modes))))
        {
            return failure(s, EReason::limit_exceeded);
        }
        if (hr == DXGI_ERROR_MORE_DATA)
        {
            w.mode_buffer.deallocate();
            if (w.pass < limits.query_passes) w.phase = EQueryPhase::mode_count;
            else
            {
                missing(list.query_id, PlatformQueryStatus::failed, PlatformQueryReason::buffer_retry_exhausted,
                    PlatformErrorDomain::hresult, static_cast<UINT32>(hr));
                next_format(true);
            }
        }
        else if (FAILED(hr))
        {
            missing(list.query_id, PlatformQueryStatus::failed, PlatformQueryReason::native_failure, PlatformErrorDomain::hresult, static_cast<UINT32>(hr));
            next_format(true);
        }
        else if (!w.mode_count)
        {
            list.modes = {};
            next_format(false);
        }
        else if (sizing)
        {
            const std::size_t size = w.mode_count * sizeof(DXGI_MODE_DESC1);
            if (!w.mode_buffer.reallocate(size, size, 16u))
            {
                return failure(s, EReason::allocation_failed);
            }
            w.phase = EQueryPhase::mode_fetch;
        }
        else
        {
            if (w.mode_count > capacity)
            {
                return reject("dxgi.mode_count", w.mode_count, capacity);
            }
            q(list.query_id).returned_count = w.mode_count;
            s.raw_modes += w.mode_count;
            w.phase = EQueryPhase::mode_rows;
        }
        return EReportStep::more;
    }
    const auto* const native = reinterpret_cast<const DXGI_MODE_DESC1*>(w.mode_buffer.data());
    while (w.mode_index < w.mode_count)
    {
        const DXGI_MODE_DESC1& mode = native[w.mode_index++];
        if (mode.Format != format)
        {
            return reject("mode.Format", mode.Format, format);
        }
        if (mode.Stereo)
        {
            return reject("mode.Stereo", mode.Stereo, FALSE);
        }
        PlatformDisplayMode row{};
        row.id = static_cast<UINT32>(r.modes.size() + 1u);
        row.catalogue_id = list.id;
        row.extent = { mode.Width, mode.Height };
        row.refresh = rational(mode.RefreshRate.Numerator, mode.RefreshRate.Denominator);
        row.format = list.format_filter;
        row.scan = scan(mode.ScanlineOrdering);
        row.scaling = { (mode.Scaling == DXGI_MODE_SCALING_CENTERED) ? PlatformScaling::centered :
            ((mode.Scaling == DXGI_MODE_SCALING_STRETCHED) ? PlatformScaling::stretched :
                ((mode.Scaling == DXGI_MODE_SCALING_UNSPECIFIED) ? PlatformScaling::unspecified : PlatformScaling::other)),
            static_cast<UINT32>(mode.Scaling) };
        row.known_fields = PlatformModeFields::extent | PlatformModeFields::refresh | PlatformModeFields::format |
            PlatformModeFields::scan | PlatformModeFields::scaling;
        if (!r.modes.push_back(row))
        {
            return failure(s, EReason::allocation_failed);
        }
        std::uint64_t now{};
        if (s.clock.read(now).code != platform::threading::EWaitError::none)
        {
            return failure(s, EReason::clock_failed);
        }
        if (now >= s.slice_end_ns) return EReportStep::more;
    }
    list.modes = { 1, static_cast<std::int32_t>(w.mode_first), w.mode_count };
    next_format(false);
    return EReportStep::more;
}

void CWindowsQueries::begin_final() noexcept
{
    w.snapshot = 1u;
    w.pass = w.query = 0u;
    w.phase = EQueryPhase::ccd_size;
}

static schema::SSpanReference root_span(const std::size_t count, const bool absent = false) noexcept
{
    return absent ? k_absent : schema::SSpanReference{ count ? 1 : 0, 0, static_cast<UINT32>(count) };
}

//  Heap construction and extraction both resume between bounded logarithmic steps.
//  Each vector is sorted in place, with no second native inventory allocation.
template<typename T, typename Compare>
static bool sort_slice(TPodVector<T>& rows, SHeapCursor& cursor, const Compare compare, SReportSession& session) noexcept
{
    if (!cursor.started)
    {
        cursor.started = true;
        cursor.remaining = rows.size();
    }
    while ((cursor.built < rows.size()) || (cursor.remaining > 1u))
    {
        if (cursor.built < rows.size())
        {
            ++cursor.built;
            std::push_heap(rows.data(), (rows.data() + cursor.built), compare);
        }
        else
        {
            std::pop_heap(rows.data(), (rows.data() + cursor.remaining), compare);
            --cursor.remaining;
        }
        std::uint64_t now{};
        if (session.clock.read(now).code != platform::threading::EWaitError::none)
        {
            (void)failure(session, EReason::clock_failed);
            return false;
        }
        if (now >= session.slice_end_ns) return false;
    }
    return true;
}

EReportStep CWindowsQueries::finalise() noexcept
{
    const auto order = [](const SIdentityObservation& a, const SIdentityObservation& b) noexcept
    {
        if (a.subject != b.subject)
        {
            return a.subject < b.subject;
        }
        if (a.subject_id != b.subject_id)
        {
            return a.subject_id < b.subject_id;
        }
        return a.identity.id < b.identity.id;
    };
    if (!sort_slice(w.evidence, w.order, order, s))
    {
        return EReportStep::more;
    }
    while (w.index < w.evidence.size())
    {
        SIdentityObservation& evidence = w.evidence[w.index++];
        schema::SSpanReference* span{};
        const UINT32 n = evidence.subject_id - 1u;
        switch (evidence.subject)
        {
            case PlatformSubjectKind::adapter:
            {
                span = &r.adapters[n].identities;
                break;
            }
            case PlatformSubjectKind::source:
            {
                span = &r.sources[n].identities;
                break;
            }
            case PlatformSubjectKind::display:
            {
                span = &r.displays[n].identities;
                break;
            }
            case PlatformSubjectKind::mode_catalogue:
            {
                span = &r.catalogues[n].identities;
                break;
            }
            default:
            {
                return reject("identity.kind", static_cast<std::uint32_t>(evidence.subject),
                    static_cast<std::uint32_t>(PlatformSubjectKind::mode_catalogue));
            }
        }
        if (!span->count) *span = { 1, static_cast<std::int32_t>(r.identities.size()), 0u };
        ++span->count;
        evidence.identity.id = static_cast<UINT32>(r.identities.size() + 1u);
        if (!r.identities.push_back(evidence.identity))
        {
            return failure(s, EReason::allocation_failed);
        }
        std::uint64_t now{};
        if (s.clock.read(now).code != platform::threading::EWaitError::none)
        {
            return failure(s, EReason::clock_failed);
        }
        if (now >= s.slice_end_ns) return EReportStep::more;
    }
    PlatformReportHeader& header = r.header;
    const bool absent = header.discovery != PlatformDiscovery::captured;
    header.adapters = root_span(r.adapters.size(), absent);
    header.sources = root_span(r.sources.size(), absent);
    header.displays = root_span(r.displays.size(), absent);
    header.paths = root_span(r.paths.size(), absent);
    header.mode_catalogues = root_span(r.catalogues.size(), absent);
    header.native_identities = root_span(r.identities.size());
    header.queries = root_span(r.queries.size());
    return EReportStep::finished;
}

EReportStep CWindowsQueries::step() noexcept
{
    switch (w.phase)
    {
        case EQueryPhase::ccd_size:
        case EQueryPhase::ccd_fetch:
        {
            return ccd();
        }
        case EQueryPhase::translate:
        {
            return translate();
        }
        case EQueryPhase::source_name:
        case EQueryPhase::adapter_name:
        case EQueryPhase::target_name:
        {
            return device_info();
        }
        case EQueryPhase::gdi_adapter:
        case EQueryPhase::gdi_monitor:
        case EQueryPhase::current_settings:
        case EQueryPhase::gdi_mode:
        {
            return gdi();
        }
        case EQueryPhase::factory:
        case EQueryPhase::dxgi_adapter:
        case EQueryPhase::adapter_desc:
        case EQueryPhase::output:
        case EQueryPhase::output_desc:
        case EQueryPhase::output_interface:
        {
            return dxgi();
        }
        case EQueryPhase::mode_count:
        case EQueryPhase::mode_fetch:
        case EQueryPhase::mode_rows:
        {
            return modes();
        }
        case EQueryPhase::finalise:
        {
            return finalise();
        }
        case EQueryPhase::compare:
        {
            static_assert(std::has_unique_object_representations_v<SCoreObservation>);
            if (w.sort_group < 2u)
            {
                const auto order = [](const SCoreObservation& a, const SCoreObservation& b) noexcept
                {
                    return std::memcmp(&a, &b, sizeof(a)) < 0;
                };
                if (sort_slice(w.core[w.sort_group], w.order, order, s))
                {
                    ++w.sort_group;
                    w.order = {};
                }
                return EReportStep::more;
            }
            //  Elide duplicate possible pairings of the same inactive available target.
            //  Indices into the native mode arrays have already been resolved independently.
            for (;;)
            {
                for (std::uint32_t k = 0u; k < 2u; ++k)
                {
                    while (w.compared[k] && (w.compared[k] < w.core[k].size()))
                    {
                        const SCoreObservation& row = w.core[k][w.compared[k]];
                        if (row.path_flags || std::memcmp(&row, &w.core[k][w.compared[k] - 1u], sizeof(row)))
                        {
                            break;
                        }
                        ++w.compared[k];
                    }
                }
                const bool left = w.compared[0] < w.core[0].size(), right = w.compared[1] < w.core[1].size();
                if (left != right)
                {
                    return EReportStep::topology_changed;
                }
                if (!left) break;
                if (std::memcmp(&w.core[0][w.compared[0]++], &w.core[1][w.compared[1]++], sizeof(SCoreObservation)))
                {
                    return EReportStep::topology_changed;
                }
                std::uint64_t now{};
                if (s.clock.read(now).code != platform::threading::EWaitError::none)
                {
                    return failure(s, EReason::clock_failed);
                }
                if (now >= s.slice_end_ns)
                {
                    return EReportStep::more;
                }
            }
            w.index = 0u;
            w.phase = EQueryPhase::labels;
            return EReportStep::more;
        }
        case EQueryPhase::labels:
        {
            if (w.index == r.adapters.size())
            {
                w.index = 0u;
                w.phase = EQueryPhase::finalise;
                return EReportStep::more;
            }
            PlatformAdapter& a = r.adapters[w.index++];
            if (a.label_query_id == w.inventory_id)
            {
                a.label_query_id = query(PlatformQueryKind::adapter_description, PlatformQueryApi::windows_dxgi,
                    PlatformSubjectKind::adapter, a.id);
                if (!a.label_query_id)
                {
                    return EReportStep::failed;
                }
                missing(a.label_query_id, PlatformQueryStatus::blocked, PlatformQueryReason::dependency_missing);
                q(a.label_query_id).blocked_by_query_id = w.dxgi_inventory;
            }
            return EReportStep::more;
        }
    }
    return reject("query.phase", static_cast<std::uint32_t>(w.phase), static_cast<std::uint32_t>(EQueryPhase::labels));
}

static bool retry_legacy_inventory(SReportSession& session, const SWindowsReportApi& api) noexcept
{
    const SWindowsReport& previous = *session.windows;
    runtime::SReportCandidate& candidate = session.candidate;
    constexpr std::uint32_t k_legacy_profile = 2u;
    if ((session.failure.reason != EReason::invalid_data) || previous.snapshot ||
        ((previous.phase != EQueryPhase::ccd_fetch) && (previous.phase != EQueryPhase::translate)) ||
        (previous.profile >= k_legacy_profile) || (candidate.header.limits.flag_profiles <= k_legacy_profile) ||
        !previous.query || !previous.rejected_field[0])
    {
        return false;
    }

    //  Retain the rejected API observation before discarding all topology derived from it.
    const debug_system::SEventArguments arguments = debug_system::encode_event_arguments(
        debug_system::CInlineText16{ previous.rejected_field }, previous.rejected_actual, previous.rejected_expected);
    constexpr char format[] = "capture/invalid_data field {} actual {} expected {}; retry QDC_ALL_PATHS";
    char diagnostic[160]{};
    std::size_t length{};
    if (debug_system::format_event_text(diagnostic, sizeof(diagnostic), format, (sizeof(format) - 1u), arguments, length) !=
        debug_system::EEventFormatResult::success)
    {
        return false;
    }
    const std::size_t text_id = candidate.text.append(reinterpret_cast<const std::uint8_t*>(diagnostic), length);
    if (!text_id || (text_id > INT32_MAX))
    {
        session.failure = { reports::EStage::capture, EReason::allocation_failed };
        return false;
    }
    PlatformQueryResult& rejected = candidate.queries[previous.query - 1u];
    rejected.status = PlatformQueryStatus::failed;
    rejected.reason = PlatformQueryReason::profile_fallback;
    rejected.diagnostic = { static_cast<std::int32_t>(text_id) };
    MV_LOG(info, "Platform report retained invalid_data query {} flags {}; restarting CCD inventory with legacy flags {}",
        previous.query, rejected.native_flags, k_profiles[k_legacy_profile]);

    //  Only the initial CCD inventory can enter this path: all retained queries are report-scoped.
    candidate.adapters.clear();
    candidate.sources.clear();
    candidate.displays.clear();
    candidate.paths.clear();
    candidate.bytes.clear();
    candidate.header.inventory_query_id = candidate.header.consistency_query_id = 0u;
    candidate.header.coverage = PlatformCoverage::limited;
    candidate.header.discovery = {};
    session.windows.reset();
    if (!session.windows.emplace())
    {
        session.failure = { reports::EStage::capture, EReason::allocation_failed };
        return false;
    }
    session.windows->api = &api;
    session.windows->profile = k_legacy_profile;
    session.failure = {};
    //  The existing session retains cumulative native-call counts and its original eligible-time budget.
    return true;
}

EReportStep windows_report_step(SReportSession& session, const SWindowsReportApi& api) noexcept
{
    if (!session.windows)
    {
        if (!session.windows.emplace())
        {
            return failure(session, EReason::allocation_failed);
        }
        session.windows->api = &api;
        session.candidate.header.coverage = PlatformCoverage::complete;
    }
    CWindowsQueries cursor{ session, *session.windows };
    const EReportStep result = cursor.step();
    if (session.failure.reason != EReason::none)
    {
        const SWindowsReport& state = *session.windows;
        MV_LOG(info, "Platform report native failure: phase {} ({}), reason {}, domain {}, native {}, calls {}, profile {}, pass {}",
            phase_name(state.phase), static_cast<std::uint32_t>(state.phase), static_cast<std::uint32_t>(session.failure.reason),
            static_cast<std::uint32_t>(session.failure.domain), session.failure.native_error, session.native_calls, state.profile, state.pass);
        MV_LOG(info, "Platform report native cursor: snapshot {}, row {}, mode row {}, query {}, flags {}, paths {}, modes {}",
            state.snapshot, state.index, state.mode_index, state.query, k_profiles[state.profile],
            state.paths[state.snapshot], state.modes[state.snapshot]);
        if (retry_legacy_inventory(session, api))
        {
            return EReportStep::more;
        }
        return EReportStep::failed;
    }
    if (result == EReportStep::finished) session.windows.reset();
    return result;
}

const SReportAdapter& report_adapter() noexcept
{
    static constexpr SReportAdapter adapter{ [](SReportSession& session) noexcept
    {
        return windows_report_step(session, windows_report_api());
    } };
    return adapter;
}

}   //  namespace platform::native
#endif

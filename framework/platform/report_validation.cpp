
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_validation.cpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Checks declarations, category-local IDs, rooted ranges, query outcomes and relationships before Host publication.

#include "framework/platform/report_validation.hpp"
#include "framework/platform/report.hpp"
#include "data_model/document_parser.hpp"
#include "schema/data_block_view.hpp"
#include "schema/types/type_compatibility.hpp"

#include <algorithm>
#include <cstring>

namespace platform::reports
{

using namespace platform_report_v1;

static bool expected_types(const schema::CResolvedTypes& actual) noexcept
{
    CLiveDocument source;
    schema::SDiagnostic error;
    schema::CResolvedTypes expected;
    if (!document_parser::parse(schema_source(), source).accepted() || !expected.resolve(source, error) ||
        (expected.declaration_count() != actual.declaration_count()))
    {
        return false;
    }
    schema::CDeclarationCompatibility comparison{ expected, actual, schema::EDeclarationMatch::definition };
    for (std::uint32_t i = 0u; i < expected.declaration_count(); ++i)
    {
        const schema::CTypesIndex type = expected.declaration_at(i);
        schema::SDeclaration declaration;
        if (!expected.declaration(type, declaration) ||
            (comparison.compare(type, actual.find_declaration(expected.name(declaration.name))) != schema::ETypeMatchResult::match))
        {
            return false;
        }
    }
    return true;
}

template<typename T>
static bool read(const schema::CDataBlockView& view, const schema::CDataEntryHandle handle,
    const std::uint32_t ordinal, T& result) noexcept
{
    schema::CDataValueView value;
    CByteConstView bytes;
    if (!view.value(handle, ordinal, value) || !view.bytes(value, bytes) || (bytes.size() != sizeof(T)))
    {
        return false;
    }
    std::memcpy(&result, bytes.data(), sizeof(T));
    return true;
}

struct SRowIndex { std::uint32_t id, ordinal; };

struct STable
{
    schema::CDataEntryHandle entry;
    std::uint32_t count{};
    TPodVector<SRowIndex> index;

    bool bind(const schema::CDataBlockView& view, const std::size_t category) noexcept
    {
        const SCollectionName& collection = k_collections[category];
        entry = view.find_collection(CStringView{ collection.type }, CStringView{ collection.name });
        if (!entry) return true;
        schema::SDataEntryInfo info;
        if (!view.entry(entry, info) || !info.count)
        {
            return false;
        }
        count = info.count;
        if (category == 7u) return true; //  PlatformByte has no fact ID.
        if (!index.resize(count)) return false;
        for (std::uint32_t i = 0u; i < count; ++i)
        {
            schema::CDataValueView value;
            CByteConstView bytes;
            std::uint32_t id{};
            if (!view.value(entry, i, value) || !view.bytes(value, bytes) || (bytes.size() < sizeof(id)))
            {
                return false;
            }
            std::memcpy(&id, bytes.data(), sizeof(id));
            if (!id || (id > INT32_MAX))
            {
                return false;
            }
            index[i] = { id, i };
        }
        std::sort(index.data(), index.data() + count,
            [](const SRowIndex a, const SRowIndex b) noexcept { return a.id < b.id; });
        for (std::uint32_t i = 1u; i < count; ++i)
        {
            if (index[i - 1u].id == index[i].id)
            {
                return false;
            }
        }
        return true;
    }

    std::uint32_t find(const std::uint32_t id) const noexcept
    {
        if (!index.size()) return UINT32_MAX;
        const SRowIndex* const end = index.data() + index.size();
        const SRowIndex* const found = std::lower_bound(index.data(), end, id,
            [](const SRowIndex record, const std::uint32_t key) noexcept { return record.id < key; });
        return ((found != end) && (found->id == id)) ? found->ordinal : UINT32_MAX;
    }

    bool covers(const schema::SSpanReference span, const bool absent) const noexcept
    {
        //  Checked binding already established the exact element type, directory
        //  and bounds. Only one named collection per element type is admitted.
        return (span.first == 0) && (count == span.count) && (absent ? (span.collection_id == -1) : (span.collection_id >= 0));
    }
};

struct SReportTables
{
    const schema::CDataBlockView& view;
    STable adapters, sources, displays, paths, catalogues, modes, identities, bytes, queries;

    template<typename T>
    bool lookup(const STable& table, const std::uint32_t id, T& result) const noexcept
    {
        const std::uint32_t i = table.find(id);
        return (i < table.count) && read(view, table.entry, i, result);
    }

    bool query(const std::uint32_t id) const noexcept { return queries.find(id) != UINT32_MAX; }

    bool fact_query(const std::uint32_t query_id, const PlatformQueryKind kind, const PlatformSubjectKind subject,
        const std::uint32_t id, const bool present, const bool allow_inventory = false) const noexcept
    {
        PlatformQueryResult row{};
        if (!lookup(queries, query_id, row) || (row.kind != kind) || (present && (row.status != PlatformQueryStatus::success)))
        {
            return false;
        }
        // EnumDisplayDevices monitor enumeration cannot supply adapter-level primary/attachment flags.
        if (allow_inventory && row.native_flags) return false;
        return ((row.subject_kind == subject) && (row.subject_id == id)) ||
            (allow_inventory && (row.subject_kind == PlatformSubjectKind::report) && !row.subject_id);
    }

    bool subject(const PlatformQueryResult& row) const noexcept
    {
        switch (row.subject_kind)
        {
            case PlatformSubjectKind::report:
            {
                return row.subject_id == 0u;
            }
            case PlatformSubjectKind::adapter:
            {
                return adapters.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::source:
            {
                return sources.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::display:
            {
                return displays.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::path:
            {
                return paths.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::mode_catalogue:
            {
                return catalogues.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::native_identity:
            {
                return identities.find(row.subject_id) != UINT32_MAX;
            }
            case PlatformSubjectKind::query:
            {
                return query(row.subject_id);
            }
        }
        return false;
    }

    bool provenance(const PlatformQueryResult& row) const noexcept
    {
        using Q = PlatformQueryKind;
        using A = PlatformQueryApi;
        using S = PlatformSubjectKind;
        switch (row.kind)
        {
            case Q::inventory:
            case Q::consistency_check:
            {
                return (row.api == A::windows_ccd) && (row.subject_kind == S::report);
            }
            case Q::adapter_identity:
            {
                return (row.api == A::windows_display_device_info) && (row.subject_kind == S::adapter);
            }
            case Q::source_identity:
            {
                return (row.api == A::windows_display_device_info) && (row.subject_kind == S::source);
            }
            case Q::display_identity:
            {
                return (row.api == A::windows_display_device_info) && (row.subject_kind == S::display);
            }
            case Q::current_desktop_settings:
            {
                return (row.api == A::windows_gdi_settings) && (row.subject_kind == S::source);
            }
            case Q::desktop_annotation:
            {
                return row.api == A::windows_gdi_devices;
            }
            case Q::adapter_output_inventory:
            case Q::adapter_description:
            case Q::output_description:
            {
                return row.api == A::windows_dxgi;
            }
            case Q::mode_catalogue: return ((row.api == A::windows_dxgi) || (row.api == A::windows_gdi_settings)) &&
                (row.subject_kind == S::mode_catalogue);
        }
        return false;
    }

    bool evidence(const PlatformReportHeader& header) const noexcept;
    bool outcomes(const PlatformReportHeader& header) const noexcept;
    bool topology() const noexcept;
    bool mode_lists() const noexcept;
};

bool SReportTables::outcomes(const PlatformReportHeader& header) const noexcept
{
    PlatformQueryResult core{}, check{};
    if (!lookup(queries, header.inventory_query_id, core) || !lookup(queries, header.consistency_query_id, check) ||
        (core.kind != PlatformQueryKind::inventory) || (check.kind != PlatformQueryKind::consistency_check) ||
        (core.api != PlatformQueryApi::windows_ccd) || (check.api != core.api) ||
        (core.subject_kind != PlatformSubjectKind::report) || (check.subject_kind != PlatformSubjectKind::report))
    {
        return false;
    }
    constexpr std::uint32_t full_profile = 0x51u, virtual_profile = 0x11u, basic_profile = 1u;
    if ((core.native_flags != full_profile) && (core.native_flags != virtual_profile) && (core.native_flags != basic_profile))
    {
        return false;
    }
    if ((core.native_flags != full_profile) && (header.coverage != PlatformCoverage::limited))
    {
        return false;
    }
    const bool normal = header.discovery == PlatformDiscovery::captured;
    if (normal)
    {
        if ((core.status != PlatformQueryStatus::success) || (check.status != core.status) ||
            (core.native_flags != check.native_flags)) return false;
    }
    else if ((header.coverage != PlatformCoverage::limited) ||
        (core.status != ((header.discovery == PlatformDiscovery::unsupported) ?
            PlatformQueryStatus::unsupported : PlatformQueryStatus::unavailable)) ||
        (core.error_domain != PlatformErrorDomain::win32) ||
        ((header.discovery == PlatformDiscovery::unsupported) ? ((core.native_error != 50u) || (core.reason != PlatformQueryReason::api_not_supported)) :
            ((core.native_error != 5u) || (core.reason != PlatformQueryReason::access_denied))) ||
        ((check.status != PlatformQueryStatus::blocked) && (check.status != PlatformQueryStatus::not_applicable)))
    {
        return false;
    }
    TPodVector<std::uint32_t> parents;
    TPodVector<std::uint8_t> visited;
    if (!parents.resize(queries.count) || !visited.push_back_zeroed(queries.count))
    {
        return false;
    }
    std::uint64_t calls{};
    bool gdi_inventory{}, dxgi_inventory{};
    for (std::uint32_t i = 0u; i < queries.count; ++i)
    {
        PlatformQueryResult q{};
        if (!read(view, queries.entry, i, q) || !subject(q) || !provenance(q))
        {
            return false;
        }
        if (q.subject_kind == PlatformSubjectKind::report)
        {
            gdi_inventory |= q.kind == PlatformQueryKind::desktop_annotation;
            dxgi_inventory |= q.kind == PlatformQueryKind::adapter_output_inventory;
        }
        const bool blocked = (q.status == PlatformQueryStatus::blocked) || (q.status == PlatformQueryStatus::not_applicable);
        if ((q.kind < PlatformQueryKind::inventory) || (q.kind > PlatformQueryKind::current_desktop_settings) ||
            (q.api < PlatformQueryApi::windows_ccd) || (q.api > PlatformQueryApi::windows_dxgi) ||
            (q.status < PlatformQueryStatus::success) || (q.status > PlatformQueryStatus::not_applicable) ||
            (q.reason > PlatformQueryReason::topology_changed) || (q.error_domain > PlatformErrorDomain::hresult) ||
            (blocked && (q.call_count || q.attempts || q.elapsed_ns || q.returned_count)) || (!blocked && (!q.attempts || !q.call_count)) ||
            ((q.error_domain == PlatformErrorDomain::none) && q.native_error) ||
            ((header.coverage == PlatformCoverage::complete) && ((q.reason != PlatformQueryReason::none) ||
                ((q.status != PlatformQueryStatus::success) && (q.status != PlatformQueryStatus::not_applicable)))))
        {
            return false;
        }
        calls += q.call_count;
        parents[i] = q.blocked_by_query_id ? queries.find(q.blocked_by_query_id) : UINT32_MAX;
        if (q.blocked_by_query_id && (!blocked || (parents[i] == UINT32_MAX)))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < queries.count; ++i)
    {
        std::uint32_t at = i;
        while ((at != UINT32_MAX) && !visited[at])
        {
            visited[at] = 1u;
            at = parents[at];
        }
        if ((at != UINT32_MAX) && (visited[at] == 1u))
        {
            return false;
        }
        at = i;
        while ((at != UINT32_MAX) && (visited[at] == 1u))
        {
            visited[at] = 2u;
            at = parents[at];
        }
    }
    return (calls <= header.native_calls) && ((header.capture_attempts > 1u) || (calls == header.native_calls)) &&
        (!normal || (gdi_inventory && dxgi_inventory));
}

static bool refresh(const PlatformRefresh value) noexcept
{
    switch (value.precision)
    {
        case PlatformRefreshPrecision::rational:
        {
            return value.denominator != 0u;
        }
        case PlatformRefreshPrecision::integer_hz:
        {
            return value.denominator == 1u;
        }
        case PlatformRefreshPrecision::unknown:
        case PlatformRefreshPrecision::unspecified:
        {
            return !value.numerator && !value.denominator;
        }
    }
    return false;
}

static bool effective_limits(const PlatformReportHeader& header) noexcept
{
    const PlatformCaptureLimits& limits = header.limits;
    constexpr PlatformSchemaLimits schema = schema_limits({});
    return (limits.capture_attempts == k_capture_limits.capture_attempts) &&
        (limits.query_passes == k_capture_limits.query_passes) &&
        (limits.flag_profiles == k_capture_limits.flag_profiles) &&
        (limits.adapters_per_backend == k_capture_limits.adapters_per_backend) &&
        (limits.sources == k_capture_limits.sources) &&
        (limits.displays == k_capture_limits.displays) &&
        (limits.outputs == k_capture_limits.outputs) &&
        (limits.monitor_records == k_capture_limits.monitor_records) &&
        (limits.ccd_paths == k_capture_limits.ccd_paths) &&
        (limits.ccd_modes == k_capture_limits.ccd_modes) &&
        (limits.modes_per_scope == k_capture_limits.modes_per_scope) &&
        (limits.total_mode_rows == k_capture_limits.total_mode_rows) &&
        (limits.native_calls == k_capture_limits.native_calls) &&
        (limits.owned_bytes == k_capture_limits.owned_bytes) &&
        (limits.report_bytes == k_capture_limits.report_bytes) &&
        (limits.eligible_duration_ns == k_capture_limits.eligible_duration_ns) &&
        (header.schema_limits.inspected_source_bytes == schema.inspected_source_bytes) &&
        (header.schema_limits.owned_bytes == schema.owned_bytes) &&
        (header.schema_limits.result_bytes == schema.result_bytes) &&
        (header.schema_limits.work_credits == schema.work_credits) &&
        (header.schema_limits.document_nodes == schema.document_nodes) &&
        (header.schema_limits.records == schema.records) &&
        (header.schema_limits.depth == schema.depth);
}

template<typename T>
static bool zero(const T& value) noexcept
{
    static_assert(std::has_unique_object_representations_v<T>);
    constexpr T empty{};
    return std::memcmp(&value, &empty, sizeof(T)) == 0;
}

template<typename T>
static bool known(const std::uint32_t mask, const std::uint32_t bit, const T& field) noexcept
{
    return (mask & bit) || zero(field);
}

template<typename T, typename E>
static bool wrapper(const T value, const E last) noexcept
{
    return ((value.value <= last) && ((value.value != E::unknown) || !value.native_code)) || (value.value == E::other);
}

static bool desktop_fields(const PlatformDesktopState& row) noexcept
{
    using namespace PlatformDesktopFields;
    const std::uint32_t mask = row.known_fields;
    return !(mask & ~63u) && known(mask, rectangle, row.rectangle) && known(mask, format, row.format) &&
        known(mask, depth, row.bits_per_pixel) && known(mask, PlatformDesktopFields::refresh, row.refresh) &&
        known(mask, rotation, row.rotation) && known(mask, scaling, row.scaling) &&
        wrapper(row.format, PlatformFormat::desktop_32bpp) && wrapper(row.rotation, PlatformRotation::counterclockwise_270) &&
        wrapper(row.scaling, PlatformScaling::unspecified) && reports::refresh(row.refresh);
}

static bool path_fields(const PlatformDisplayPath& row) noexcept
{
    const PlatformSignalState& state = row.signal;
    const std::uint32_t mask = state.known_fields;
    const PlatformDesktopImageState& image = row.desktop_image;
    using namespace PlatformSignalFields;
    return !(mask & ~127u) && known(mask, extent, state.extent) && known(mask, reported_refresh, state.reported_refresh) &&
        known(mask, physical_refresh, state.physical_refresh) && known(mask, virtual_refresh, state.virtual_refresh) &&
        known(mask, scan, state.scan) && known(mask, rotation, state.rotation) && known(mask, scaling, state.scaling) &&
        refresh(state.reported_refresh) && refresh(state.physical_refresh) && refresh(state.virtual_refresh) &&
        wrapper(state.scan, PlatformScan::interlaced_unspecified) && wrapper(state.rotation, PlatformRotation::counterclockwise_270) &&
        wrapper(state.scaling, PlatformScaling::unspecified) && !(image.known_fields & ~7u) &&
        known(image.known_fields, 1u, image.path_source_extent) && known(image.known_fields, 2u, image.image_region) &&
        known(image.known_fields, 4u, image.image_clip);
}

static bool mode_fields(const PlatformDisplayMode& row) noexcept
{
    using namespace PlatformModeFields;
    const std::uint32_t mask = row.known_fields;
    return !(mask & ~127u) && known(mask, extent, row.extent) && known(mask, PlatformModeFields::refresh, row.refresh) &&
        known(mask, format, row.format) && known(mask, depth, row.bits_per_pixel) && known(mask, scan, row.scan) &&
        known(mask, scaling, row.scaling) && known(mask, rotation, row.rotation) &&
        wrapper(row.format, PlatformFormat::desktop_32bpp) && wrapper(row.scan, PlatformScan::interlaced_unspecified) &&
        wrapper(row.rotation, PlatformRotation::counterclockwise_270) && wrapper(row.scaling, PlatformScaling::unspecified) &&
        reports::refresh(row.refresh);
}

bool SReportTables::topology() const noexcept
{
    for (std::uint32_t i = 0u; i < identities.count; ++i)
    {
        PlatformNativeIdentity row{};
        if (!read(view, identities.entry, i, row) || !query(row.query_id))
        {
            return false;
        }
        if ((row.kind >= PlatformNativeIdentityKind::windows_adapter_luid) && (row.kind <= PlatformNativeIdentityKind::windows_ccd_target))
        {
            const std::uint32_t size = (row.kind == PlatformNativeIdentityKind::windows_adapter_luid) ? 8u : 12u;
            if ((row.bytes.count != size) || (row.text.string_value_id != -1))
            {
                return false;
            }
        }
        else if ((row.kind < PlatformNativeIdentityKind::windows_adapter_device_path) ||
            (row.kind > PlatformNativeIdentityKind::windows_monitor_interface_name) ||
            (row.text.string_value_id <= 0) || (row.bytes.collection_id != -1))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < adapters.count; ++i)
    {
        PlatformAdapter row{};
        if (!read(view, adapters.entry, i, row) || !fact_query(row.label_query_id, PlatformQueryKind::adapter_description,
            PlatformSubjectKind::adapter, row.id, (row.label.string_value_id >= 0)) ||
            !query(row.discovery_query_id))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < sources.count; ++i)
    {
        PlatformDisplaySource row{};
        if (!read(view, sources.entry, i, row) || (adapters.find(row.adapter_id) == UINT32_MAX) ||
            !query(row.topology_query_id) || !fact_query(row.annotation_query_id, PlatformQueryKind::desktop_annotation,
                PlatformSubjectKind::source, row.id, ((row.primary != PlatformTruth::unknown) || (row.desktop_attached != PlatformTruth::unknown)), true) ||
            !query(row.desktop.query_id) || !fact_query(row.gdi_current.query_id, PlatformQueryKind::current_desktop_settings,
                PlatformSubjectKind::source, row.id, (row.gdi_current.known_fields != 0u)) ||
            (row.active != PlatformTruth::yes) || (row.primary > PlatformTruth::yes) ||
            (row.desktop_attached > PlatformTruth::yes) || !desktop_fields(row.desktop) || !desktop_fields(row.gdi_current))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < displays.count; ++i)
    {
        PlatformDisplay row{};
        if (!read(view, displays.entry, i, row) || (adapters.find(row.adapter_id) == UINT32_MAX) ||
            !fact_query(row.label_query_id, PlatformQueryKind::display_identity, PlatformSubjectKind::display, row.id, (row.label.string_value_id >= 0)) ||
            !query(row.connection_query_id) || !query(row.state_query_id) ||
            (row.active > PlatformTruth::yes) || (row.available > PlatformTruth::yes) ||
            !wrapper(row.connection, PlatformConnection::virtual_output) || ((row.active != PlatformTruth::yes) && (row.available != PlatformTruth::yes)))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < paths.count; ++i)
    {
        PlatformDisplayPath row{};
        PlatformDisplaySource source{};
        PlatformDisplay display{};
        if (!read(view, paths.entry, i, row) || !lookup(sources, row.source_id, source) ||
            !lookup(displays, row.display_id, display) || (source.active != PlatformTruth::yes) ||
            (display.active != PlatformTruth::yes) || !query(row.query_id) || !query(row.signal.query_id) ||
            !query(row.desktop_image.query_id) || !path_fields(row))
        {
            return false;
        }
    }
    return true;
}

static bool identity_query(const PlatformNativeIdentityKind identity, const PlatformQueryResult& query,
    const PlatformSubjectKind owner, const std::uint32_t owner_id, const std::uint32_t adapter_id,
    const std::uint32_t inventory_id) noexcept
{
    using I = PlatformNativeIdentityKind;
    using S = PlatformSubjectKind;
    using Q = PlatformQueryKind;
    const bool own_query = (query.subject_kind == owner) && (query.subject_id == owner_id);
    switch (identity)
    {
        case I::windows_adapter_luid:
        {
            return (owner == S::adapter) && ((query.id == inventory_id) || ((query.kind == Q::adapter_description) && own_query));
        }
        case I::windows_ccd_source:
        {
            return (owner == S::source) && (query.id == inventory_id);
        }
        case I::windows_ccd_target:
        {
            return (owner == S::display) && (query.id == inventory_id);
        }
        case I::windows_adapter_device_path:
        {
            return (owner == S::adapter) && (query.kind == Q::adapter_identity) && own_query;
        }
        case I::windows_gdi_source_name:
        {
            return (owner == S::source) && (query.kind == Q::source_identity) && own_query;
        }
        case I::windows_monitor_device_path:
        {
            return (owner == S::display) && (query.kind == Q::display_identity) && own_query;
        }
        case I::windows_dxgi_output_name:
        {
            return (owner == S::mode_catalogue) && (query.kind == Q::output_description) &&
                (query.subject_kind == S::adapter) && (query.subject_id == adapter_id);
        }
        case I::windows_monitor_interface_name:
        {
            // GDI enumerates these under a source name, including names without a unique CCD source match.
            return (owner == S::display) && (query.kind == Q::desktop_annotation) && (query.native_flags == 1u) &&
                ((query.subject_kind == S::source) || (query.subject_kind == S::report));
        }
    }
    return false;
}

bool SReportTables::evidence(const PlatformReportHeader& header) const noexcept
{
    //  Validate every incoming subject range and its canonical tuple independently of row order.
    TPodVector<std::uint8_t> reached, payload, source_paths, display_paths, has_luid;
    TPodVector<std::uint64_t> luids;
    struct SEndpoint { std::uint64_t luid; std::uint32_t endpoint; };
    TPodVector<SEndpoint> source_keys, target_keys;
    if ((identities.count && !reached.push_back_zeroed(identities.count)) || (bytes.count && !payload.push_back_zeroed(bytes.count)) ||
        (sources.count && !source_paths.push_back_zeroed(sources.count)) || (displays.count && !display_paths.push_back_zeroed(displays.count)) ||
        (adapters.count && (!has_luid.push_back_zeroed(adapters.count) || !luids.push_back_zeroed(adapters.count))))
    {
        return false;
    }
    std::uint64_t work = schema::SDataOperationLimits{}.work_credits;
    const auto range = [&](const schema::SSpanReference span, const PlatformSubjectKind subject,
        const std::uint32_t ordinal, const std::uint32_t subject_id, const std::uint32_t adapter_id) noexcept
    {
        bool tuple{};
        if (span.count > work) return false;
        work -= span.count;
        for (std::uint32_t n = 0u; n < span.count; ++n)
        {
            const std::uint32_t i = static_cast<std::uint32_t>(span.first) + n;
            PlatformNativeIdentity row{};
            PlatformQueryResult query{};
            if (!read(view, identities.entry, i, row) || !lookup(queries, row.query_id, query) ||
                (query.status != PlatformQueryStatus::success) ||
                !identity_query(row.kind, query, subject, subject_id, adapter_id, header.inventory_query_id))
            {
                return false;
            }
            reached[i] = 1u;
            using I = PlatformNativeIdentityKind;
            using S = PlatformSubjectKind;
            if (row.kind <= I::windows_ccd_target)
            {
                if (tuple) return false;
                tuple = true;
                std::uint64_t luid{};
                std::uint32_t endpoint{};
                for (std::uint32_t b = 0u; b < row.bytes.count; ++b)
                {
                    const std::uint32_t at = static_cast<std::uint32_t>(row.bytes.first) + b;
                    PlatformByte byte{};
                    if (!read(view, bytes.entry, at, byte))
                    {
                        return false;
                    }
                    payload[at] = 1u;
                    if (b < 8u) luid |= static_cast<std::uint64_t>(byte.value) << (b << 3u);
                    else endpoint |= static_cast<std::uint32_t>(byte.value) << ((b - 8u) << 3u);
                }
                if (subject == S::adapter)
                {
                    for (std::uint32_t prior = 0u; prior < ordinal; ++prior)
                    {
                        if (has_luid[prior] && (luids[prior] == luid))
                        {
                            return false;
                        }
                    }
                    has_luid[ordinal] = 1u;
                    luids[ordinal] = luid;
                }
                else
                {
                    const std::uint32_t a = adapters.find(adapter_id);
                    if (!has_luid[a] || (luid != luids[a]))
                    {
                        return false;
                    }
                    TPodVector<SEndpoint>& keys = (subject == S::source) ? source_keys : target_keys;
                    for (std::size_t k = 0u; k < keys.size(); ++k)
                    {
                        if ((keys[k].luid == luid) && (keys[k].endpoint == endpoint))
                        {
                            return false;
                        }
                    }
                    if (!keys.push_back({ luid, endpoint }))
                    {
                        return false;
                    }
                }
            }
        }
        return ((subject != PlatformSubjectKind::source) && (subject != PlatformSubjectKind::display)) || tuple;
    };
    for (std::uint32_t i = 0u; i < adapters.count; ++i)
    {
        PlatformAdapter a{};
        PlatformQueryResult discovery{};
        if (!read(view, adapters.entry, i, a) || !range(a.identities, PlatformSubjectKind::adapter, i, a.id, a.id) ||
            !lookup(queries, a.discovery_query_id, discovery))
        {
            return false;
        }
        if (a.discovery_query_id == header.inventory_query_id)
        {
            if (!has_luid[i]) return false;
        }
        else if (discovery.kind == PlatformQueryKind::adapter_output_inventory)
        {
            // A later enumeration error can follow an observed adapter whose own description failed.
            if ((discovery.subject_kind != PlatformSubjectKind::report) || !discovery.returned_count ||
                ((discovery.status != PlatformQueryStatus::success) && (discovery.status != PlatformQueryStatus::failed) &&
                    (discovery.status != PlatformQueryStatus::unsupported)))
            {
                return false;
            }
        }
        else if ((discovery.kind != PlatformQueryKind::adapter_description) || (discovery.status != PlatformQueryStatus::success) ||
            (discovery.subject_kind != PlatformSubjectKind::adapter) || (discovery.subject_id != a.id))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < paths.count; ++i)
    {
        PlatformDisplayPath p{};
        if (!read(view, paths.entry, i, p) || (p.query_id != header.inventory_query_id) ||
            (p.signal.query_id != p.query_id) || (p.desktop_image.query_id != p.query_id)) return false;
        source_paths[sources.find(p.source_id)] = 1u;
        display_paths[displays.find(p.display_id)] = 1u;
    }
    for (std::uint32_t i = 0u; i < sources.count; ++i)
    {
        PlatformDisplaySource s{};
        if (!read(view, sources.entry, i, s) || !source_paths[i] || (s.topology_query_id != header.inventory_query_id) ||
            (s.desktop.query_id != s.topology_query_id) || !range(s.identities, PlatformSubjectKind::source, i, s.id, s.adapter_id))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < displays.count; ++i)
    {
        PlatformDisplay d{};
        if (!read(view, displays.entry, i, d) || (static_cast<bool>(display_paths[i]) != (d.active == PlatformTruth::yes)) ||
            (d.state_query_id != header.inventory_query_id) || (d.connection_query_id != header.inventory_query_id) ||
            !range(d.identities, PlatformSubjectKind::display, i, d.id, d.adapter_id))
        {
            return false;
        }
    }
    for (std::uint32_t i = 0u; i < catalogues.count; ++i)
    {
        PlatformModeCatalogue c{};
        if (!read(view, catalogues.entry, i, c) || !range(c.identities, PlatformSubjectKind::mode_catalogue, i, c.id, c.adapter_id))
        {
            return false;
        }
    }
    for (std::size_t i = 0u; i < reached.size(); ++i)
    {
        if (!reached[i]) return false;
    }
    for (std::size_t i = 0u; i < payload.size(); ++i)
    {
        if (!payload[i]) return false;
    }
    return true;
}

bool SReportTables::mode_lists() const noexcept
{
    std::uint64_t owned{}, raw{};
    for (std::uint32_t i = 0u; i < catalogues.count; ++i)
    {
        PlatformModeCatalogue row{};
        PlatformQueryResult outcome{};
        if (!read(view, catalogues.entry, i, row) || !lookup(queries, row.query_id, outcome) ||
            (outcome.kind != PlatformQueryKind::mode_catalogue) || (outcome.subject_kind != PlatformSubjectKind::mode_catalogue) ||
            (outcome.subject_id != row.id) || (row.filter_flags & ~63u) ||
            (row.scope < PlatformModeScope::display) || (row.scope > PlatformModeScope::native_output) ||
            !wrapper(row.format_filter, PlatformFormat::desktop_32bpp) || !wrapper(row.orientation_filter, PlatformRotation::counterclockwise_270) ||
            ((row.modes.collection_id >= 0) != (outcome.status == PlatformQueryStatus::success)) ||
            (row.modes.count > outcome.returned_count) || (row.modes.count > k_capture_limits.modes_per_scope) ||
            (row.adapter_id && (adapters.find(row.adapter_id) == UINT32_MAX)) ||
            (row.source_id && (sources.find(row.source_id) == UINT32_MAX)) ||
            (row.display_id && (displays.find(row.display_id) == UINT32_MAX)))
        {
            return false;
        }
        raw += outcome.returned_count;
        if ((outcome.returned_count > k_capture_limits.modes_per_scope) || (raw > k_capture_limits.total_mode_rows))
        {
            return false;
        }
        PlatformDisplaySource source{};
        PlatformDisplay display{};
        if ((row.source_id && (!lookup(sources, row.source_id, source) || (row.adapter_id && (source.adapter_id != row.adapter_id)))) ||
            (row.display_id && (!lookup(displays, row.display_id, display) || (row.adapter_id && (display.adapter_id != row.adapter_id)))))
        {
            return false;
        }
        if (outcome.api == PlatformQueryApi::windows_gdi_settings)
        {
            if ((row.scope != PlatformModeScope::source) || (row.filter_flags != 3u) || outcome.native_flags || !zero(row.format_filter))
            {
                return false;
            }
        }
        else if ((outcome.api != PlatformQueryApi::windows_dxgi) || (outcome.native_flags != 3u) || (row.filter_flags != 60u) ||
            ((row.format_filter.value != PlatformFormat::rgba8_unorm) && (row.format_filter.value != PlatformFormat::bgra8_unorm)))
        {
            return false;
        }
        if (((row.scope == PlatformModeScope::display) && !row.display_id) ||
            ((row.scope == PlatformModeScope::source) && (!row.source_id || row.display_id)) ||
            ((row.scope == PlatformModeScope::native_output) && (!row.adapter_id || row.display_id)))
        {
            return false;
        }
        for (std::uint32_t j = 0u; j < row.modes.count; ++j)
        {
            PlatformDisplayMode mode{};
            if (!read(view, modes.entry, (static_cast<std::uint32_t>(row.modes.first) + j), mode) ||
                (mode.catalogue_id != row.id) || !mode_fields(mode) ||
                ((outcome.api == PlatformQueryApi::windows_dxgi) && ((mode.format.value != row.format_filter.value) ||
                    (mode.format.native_code != row.format_filter.native_code) || (mode.refresh.precision == PlatformRefreshPrecision::integer_hz))) ||
                ((outcome.api == PlatformQueryApi::windows_gdi_settings) && (mode.refresh.precision == PlatformRefreshPrecision::rational)))
            {
                return false;
            }
        }
        owned += row.modes.count;
    }
    return owned == modes.count;
}

SFailure validate_report(const schema::CBakedDataBlock& block, const SCaptureIdentity capture,
    const schema::SDataOperationLimits limits) noexcept
{
    constexpr SFailure invalid{ EStage::validation, EReason::invalid_data };
    schema::SDataOperationDiagnostic diagnostic;
    schema::CDataBlockView view;
    if (!view.bind(block, limits, diagnostic))
    {
        if (diagnostic.reason == schema::EDataOperationReason::allocation_failed)
        {
            return { EStage::validation, EReason::allocation_failed };
        }
        if (diagnostic.reason == schema::EDataOperationReason::limit_exceeded)
        {
            return { EStage::validation, EReason::limit_exceeded };
        }
        return invalid;
    }
    if (!expected_types(*view.types()) || (view.entry_count(schema::EDataEntryKind::instance) != 1u))
    {
        return invalid;
    }
    const auto instance = view.find_instance(CStringView{ "PlatformReportHeader" }, CStringView{ "report" });
    PlatformReportHeader header{};
    if (!read(view, instance, 0u, header) || (header.kind != PlatformReportKind::platform_report) ||
        (header.schema_version != k_report_version) || (header.platform != PlatformBackend::windows) ||
        (header.query_profile != PlatformQueryProfile::windows_desktop_discovery_v1) ||
        (header.clock != PlatformClock::system_awake_ns) || (header.visibility_scope != PlatformVisibilityScope::calling_session) ||
        (header.coverage < PlatformCoverage::complete) || (header.coverage > PlatformCoverage::limited) ||
        (header.discovery < PlatformDiscovery::captured) || (header.discovery > PlatformDiscovery::unavailable) ||
        !effective_limits(header) || !capture.capture_id || !capture.platform_instance.value || (header.capture_id != capture.capture_id) ||
        (header.platform_instance != capture.platform_instance.value) || (header.capture_end_ns < header.capture_begin_ns) ||
        !header.capture_attempts || (header.capture_attempts > k_capture_limits.capture_attempts) ||
        !header.native_calls || (header.native_calls > k_capture_limits.native_calls))
    {
        return invalid;
    }
    SReportTables t{ view };
    STable* const tables[] = { &t.adapters, &t.sources, &t.displays, &t.paths, &t.catalogues, &t.modes, &t.identities, &t.bytes, &t.queries };
    std::uint32_t nonempty{};
    for (std::size_t n = 0u; n < std::extent_v<decltype(tables)>; ++n)
    {
        if (!tables[n]->bind(view, n)) return invalid;
        if (tables[n]->count) ++nonempty;
    }
    const bool absent = header.discovery != PlatformDiscovery::captured;
    if ((nonempty != view.entry_count(schema::EDataEntryKind::collection)) ||
        !t.adapters.covers(header.adapters, absent) || !t.sources.covers(header.sources, absent) ||
        !t.displays.covers(header.displays, absent) || !t.paths.covers(header.paths, absent) ||
        !t.catalogues.covers(header.mode_catalogues, absent) || !t.identities.covers(header.native_identities, false) ||
        !t.queries.covers(header.queries, false) || !t.queries.count || (t.sources.count > k_capture_limits.sources) ||
        (t.adapters.count > (k_capture_limits.adapters_per_backend << 1u)) || (t.displays.count > k_capture_limits.displays) || (t.paths.count > k_capture_limits.ccd_paths) ||
        (t.modes.count > k_capture_limits.total_mode_rows) || !t.outcomes(header) || !t.topology() || !t.mode_lists() || !t.evidence(header))
    {
        return invalid;
    }
    return {};
}

}   //  namespace platform::reports

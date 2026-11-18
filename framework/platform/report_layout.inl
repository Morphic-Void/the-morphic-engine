#pragma once
#include <cstdint>
#include "types/fp16data_t.hpp"
#include "schema/reference_types.hpp"

namespace platform_report_v1
{

using b8 = std::int8_t;

enum class PlatformReportKind : std::uint32_t
{
    platform_report = 1u,
};

enum class PlatformBackend : std::uint32_t
{
    windows = 1u,
};

enum class PlatformQueryProfile : std::uint32_t
{
    windows_desktop_discovery_v1 = 1u,
};

enum class PlatformClock : std::uint32_t
{
    system_awake_ns = 1u,
};

enum class PlatformVisibilityScope : std::uint32_t
{
    calling_session = 1u,
};

enum class PlatformDiscovery : std::uint32_t
{
    captured = 1u,
    unsupported = 2u,
    unavailable = 3u,
};

enum class PlatformCoverage : std::uint32_t
{
    complete = 1u,
    limited = 2u,
};

enum class PlatformModeScope : std::uint32_t
{
    display = 1u,
    source = 2u,
    native_output = 3u,
};

enum class PlatformNativeIdentityKind : std::uint32_t
{
    windows_adapter_luid = 1u,
    windows_ccd_source = 2u,
    windows_ccd_target = 3u,
    windows_adapter_device_path = 4u,
    windows_monitor_device_path = 5u,
    windows_gdi_source_name = 6u,
    windows_dxgi_output_name = 7u,
    windows_monitor_interface_name = 8u,
};

enum class PlatformQueryKind : std::uint32_t
{
    inventory = 1u,
    consistency_check = 2u,
    source_identity = 3u,
    display_identity = 4u,
    adapter_identity = 5u,
    desktop_annotation = 6u,
    adapter_description = 7u,
    output_description = 8u,
    adapter_output_inventory = 9u,
    mode_catalogue = 10u,
    current_desktop_settings = 11u,
};

enum class PlatformQueryApi : std::uint32_t
{
    windows_ccd = 1u,
    windows_display_device_info = 2u,
    windows_gdi_devices = 3u,
    windows_gdi_settings = 4u,
    windows_dxgi = 5u,
};

enum class PlatformSubjectKind : std::uint32_t
{
    report = 1u,
    adapter = 2u,
    source = 3u,
    display = 4u,
    path = 5u,
    mode_catalogue = 6u,
    native_identity = 7u,
    query = 8u,
};

enum class PlatformQueryStatus : std::uint32_t
{
    success = 1u,
    unsupported = 2u,
    unavailable = 3u,
    failed = 4u,
    blocked = 5u,
    not_applicable = 6u,
};

enum class PlatformRefreshPrecision : std::uint32_t
{
    unknown = 0u,
    rational = 1u,
    integer_hz = 2u,
    unspecified = 3u,
};

enum class PlatformTruth : std::uint32_t
{
    unknown = 0u,
    no = 1u,
    yes = 2u,
};

enum class PlatformFormat : std::uint32_t
{
    unknown = 0u,
    rgba8_unorm = 1u,
    bgra8_unorm = 2u,
    desktop_8bpp = 3u,
    desktop_16bpp = 4u,
    desktop_24bpp = 5u,
    desktop_32bpp = 6u,
    other = 255u,
};

enum class PlatformScan : std::uint32_t
{
    unknown = 0u,
    unspecified = 1u,
    progressive = 2u,
    interlaced_upper_first = 3u,
    interlaced_lower_first = 4u,
    interlaced_unspecified = 5u,
    other = 255u,
};

enum class PlatformRotation : std::uint32_t
{
    unknown = 0u,
    identity = 1u,
    counterclockwise_90 = 2u,
    counterclockwise_180 = 3u,
    counterclockwise_270 = 4u,
    other = 255u,
};

enum class PlatformScaling : std::uint32_t
{
    unknown = 0u,
    identity = 1u,
    centered = 2u,
    stretched = 3u,
    aspect_preserving = 4u,
    custom = 5u,
    preferred = 6u,
    unspecified = 7u,
    other = 255u,
};

enum class PlatformConnection : std::uint32_t
{
    unknown = 0u,
    internal = 1u,
    displayport = 2u,
    hdmi = 3u,
    dvi = 4u,
    vga = 5u,
    wireless = 6u,
    virtual_output = 7u,
    other = 255u,
};

enum class PlatformQueryReason : std::uint32_t
{
    none = 0u,
    api_not_supported = 1u,
    access_denied = 2u,
    dependency_missing = 3u,
    identity_ambiguous = 4u,
    value_not_reported = 5u,
    native_failure = 6u,
    profile_fallback = 7u,
    buffer_retry_exhausted = 8u,
    topology_changed = 9u,
};

enum class PlatformErrorDomain : std::uint32_t
{
    none = 0u,
    win32 = 1u,
    hresult = 2u,
};

namespace PlatformDesktopFields
{
inline constexpr std::uint32_t rectangle = 0x00000001u;
inline constexpr std::uint32_t format = 0x00000002u;
inline constexpr std::uint32_t depth = 0x00000004u;
inline constexpr std::uint32_t refresh = 0x00000008u;
inline constexpr std::uint32_t rotation = 0x00000010u;
inline constexpr std::uint32_t scaling = 0x00000020u;
}

namespace PlatformSignalFields
{
inline constexpr std::uint32_t extent = 0x00000001u;
inline constexpr std::uint32_t reported_refresh = 0x00000002u;
inline constexpr std::uint32_t physical_refresh = 0x00000004u;
inline constexpr std::uint32_t virtual_refresh = 0x00000008u;
inline constexpr std::uint32_t scan = 0x00000010u;
inline constexpr std::uint32_t rotation = 0x00000020u;
inline constexpr std::uint32_t scaling = 0x00000040u;
}

namespace PlatformDesktopImageFields
{
inline constexpr std::uint32_t path_source_extent = 0x00000001u;
inline constexpr std::uint32_t image_region = 0x00000002u;
inline constexpr std::uint32_t image_clip = 0x00000004u;
}

namespace PlatformModeFields
{
inline constexpr std::uint32_t extent = 0x00000001u;
inline constexpr std::uint32_t refresh = 0x00000002u;
inline constexpr std::uint32_t format = 0x00000004u;
inline constexpr std::uint32_t depth = 0x00000008u;
inline constexpr std::uint32_t scan = 0x00000010u;
inline constexpr std::uint32_t scaling = 0x00000020u;
inline constexpr std::uint32_t rotation = 0x00000040u;
}

namespace PlatformCatalogueFilters
{
inline constexpr std::uint32_t monitor_compatible = 0x00000001u;
inline constexpr std::uint32_t current_orientation = 0x00000002u;
inline constexpr std::uint32_t interlaced = 0x00000004u;
inline constexpr std::uint32_t scaled = 0x00000008u;
inline constexpr std::uint32_t stereo_excluded = 0x00000010u;
inline constexpr std::uint32_t explicit_format = 0x00000020u;
}

struct PlatformCaptureLimits
{
    std::uint32_t capture_attempts;
    std::uint32_t query_passes;
    std::uint32_t flag_profiles;
    std::uint32_t adapters_per_backend;
    std::uint32_t sources;
    std::uint32_t displays;
    std::uint32_t outputs;
    std::uint32_t monitor_records;
    std::uint32_t ccd_paths;
    std::uint32_t ccd_modes;
    std::uint32_t modes_per_scope;
    std::uint32_t total_mode_rows;
    std::uint32_t native_calls;
    std::uint64_t owned_bytes;
    std::uint64_t report_bytes;
    std::uint64_t eligible_duration_ns;
};

struct PlatformSchemaLimits
{
    std::uint64_t inspected_source_bytes;
    std::uint64_t owned_bytes;
    std::uint64_t result_bytes;
    std::uint64_t work_credits;
    std::uint32_t document_nodes;
    std::uint32_t records;
    std::uint32_t depth;
};

struct PlatformExtent
{
    std::uint32_t width;
    std::uint32_t height;
};

struct PlatformRect
{
    std::int32_t x;
    std::int32_t y;
    std::uint32_t width;
    std::uint32_t height;
};

struct PlatformRefresh
{
    PlatformRefreshPrecision precision;
    std::uint32_t numerator;
    std::uint32_t denominator;
};

struct PlatformFormatValue
{
    PlatformFormat value;
    std::uint32_t native_code;
};

struct PlatformScanValue
{
    PlatformScan value;
    std::uint32_t native_code;
};

struct PlatformRotationValue
{
    PlatformRotation value;
    std::uint32_t native_code;
};

struct PlatformScalingValue
{
    PlatformScaling value;
    std::uint32_t native_code;
};

struct PlatformConnectionValue
{
    PlatformConnection value;
    std::uint32_t native_code;
};

struct PlatformDesktopState
{
    std::uint32_t known_fields;
    PlatformRect rectangle;
    PlatformFormatValue format;
    std::uint32_t bits_per_pixel;
    PlatformRefresh refresh;
    PlatformRotationValue rotation;
    PlatformScalingValue scaling;
    std::uint32_t query_id;
};

struct PlatformSignalState
{
    std::uint32_t known_fields;
    PlatformExtent extent;
    PlatformRefresh reported_refresh;
    PlatformRefresh physical_refresh;
    PlatformRefresh virtual_refresh;
    PlatformScanValue scan;
    PlatformRotationValue rotation;
    PlatformScalingValue scaling;
    std::uint32_t query_id;
};

struct PlatformDesktopImageState
{
    std::uint32_t known_fields;
    PlatformExtent path_source_extent;
    PlatformRect image_region;
    PlatformRect image_clip;
    std::uint32_t query_id;
};

struct PlatformByte
{
    std::uint8_t value;
};

struct PlatformNativeIdentity
{
    std::uint32_t id;
    PlatformNativeIdentityKind kind;
    std::uint32_t query_id;
    ::schema::SSpanReference bytes;
    ::schema::STextReference text;
};

struct PlatformAdapter
{
    std::uint32_t id;
    ::schema::STextReference label;
    std::uint32_t label_query_id;
    ::schema::SSpanReference identities;
    std::uint32_t discovery_query_id;
};

struct PlatformDisplaySource
{
    std::uint32_t id;
    std::uint32_t adapter_id;
    PlatformTruth active;
    PlatformTruth desktop_attached;
    PlatformTruth primary;
    ::schema::SSpanReference identities;
    PlatformDesktopState desktop;
    PlatformDesktopState gdi_current;
    std::uint32_t native_flags;
    std::uint32_t topology_query_id;
    std::uint32_t annotation_query_id;
};

struct PlatformDisplay
{
    std::uint32_t id;
    std::uint32_t adapter_id;
    ::schema::STextReference label;
    std::uint32_t label_query_id;
    PlatformTruth available;
    PlatformTruth active;
    PlatformConnectionValue connection;
    std::uint32_t connection_query_id;
    ::schema::SSpanReference identities;
    std::uint32_t native_flags;
    std::uint32_t state_query_id;
};

struct PlatformDisplayPath
{
    std::uint32_t id;
    std::uint32_t source_id;
    std::uint32_t display_id;
    PlatformSignalState signal;
    PlatformDesktopImageState desktop_image;
    std::uint32_t native_flags;
    std::uint32_t query_id;
};

struct PlatformDisplayMode
{
    std::uint32_t id;
    std::uint32_t catalogue_id;
    std::uint32_t known_fields;
    PlatformExtent extent;
    PlatformRefresh refresh;
    PlatformFormatValue format;
    std::uint32_t bits_per_pixel;
    PlatformScanValue scan;
    PlatformScalingValue scaling;
    PlatformRotationValue rotation;
};

struct PlatformModeCatalogue
{
    std::uint32_t id;
    PlatformModeScope scope;
    std::uint32_t adapter_id;
    std::uint32_t source_id;
    std::uint32_t display_id;
    ::schema::SSpanReference identities;
    PlatformFormatValue format_filter;
    PlatformRotationValue orientation_filter;
    std::uint32_t filter_flags;
    std::uint32_t query_id;
    ::schema::SSpanReference modes;
};

struct PlatformQueryResult
{
    std::uint32_t id;
    PlatformQueryKind kind;
    PlatformQueryApi api;
    PlatformSubjectKind subject_kind;
    std::uint32_t subject_id;
    PlatformQueryStatus status;
    PlatformQueryReason reason;
    PlatformErrorDomain error_domain;
    std::uint32_t native_error;
    std::uint32_t native_flags;
    std::uint32_t returned_count;
    std::uint32_t attempts;
    std::uint32_t call_count;
    std::uint64_t elapsed_ns;
    std::uint32_t blocked_by_query_id;
    ::schema::STextReference diagnostic;
};

struct PlatformReportHeader
{
    PlatformReportKind kind;
    std::uint32_t schema_version;
    PlatformBackend platform;
    PlatformQueryProfile query_profile;
    std::uint64_t platform_instance;
    std::uint64_t capture_id;
    PlatformClock clock;
    PlatformVisibilityScope visibility_scope;
    std::uint64_t capture_begin_ns;
    std::uint64_t capture_end_ns;
    PlatformDiscovery discovery;
    PlatformCoverage coverage;
    std::uint32_t capture_attempts;
    std::uint32_t native_calls;
    std::uint32_t inventory_query_id;
    std::uint32_t consistency_query_id;
    PlatformCaptureLimits limits;
    PlatformSchemaLimits schema_limits;
    ::schema::SSpanReference adapters;
    ::schema::SSpanReference sources;
    ::schema::SSpanReference displays;
    ::schema::SSpanReference paths;
    ::schema::SSpanReference mode_catalogues;
    ::schema::SSpanReference native_identities;
    ::schema::SSpanReference queries;
};

}

#include <cstddef>

namespace platform_report_v1
{
static_assert(sizeof(PlatformCaptureLimits) == 80u);
static_assert(alignof(PlatformCaptureLimits) == 8u);
static_assert(offsetof(PlatformCaptureLimits, capture_attempts) == 0u);
static_assert(offsetof(PlatformCaptureLimits, query_passes) == 4u);
static_assert(offsetof(PlatformCaptureLimits, flag_profiles) == 8u);
static_assert(offsetof(PlatformCaptureLimits, adapters_per_backend) == 12u);
static_assert(offsetof(PlatformCaptureLimits, sources) == 16u);
static_assert(offsetof(PlatformCaptureLimits, displays) == 20u);
static_assert(offsetof(PlatformCaptureLimits, outputs) == 24u);
static_assert(offsetof(PlatformCaptureLimits, monitor_records) == 28u);
static_assert(offsetof(PlatformCaptureLimits, ccd_paths) == 32u);
static_assert(offsetof(PlatformCaptureLimits, ccd_modes) == 36u);
static_assert(offsetof(PlatformCaptureLimits, modes_per_scope) == 40u);
static_assert(offsetof(PlatformCaptureLimits, total_mode_rows) == 44u);
static_assert(offsetof(PlatformCaptureLimits, native_calls) == 48u);
static_assert(offsetof(PlatformCaptureLimits, owned_bytes) == 56u);
static_assert(offsetof(PlatformCaptureLimits, report_bytes) == 64u);
static_assert(offsetof(PlatformCaptureLimits, eligible_duration_ns) == 72u);
static_assert(sizeof(PlatformSchemaLimits) == 48u);
static_assert(alignof(PlatformSchemaLimits) == 8u);
static_assert(offsetof(PlatformSchemaLimits, inspected_source_bytes) == 0u);
static_assert(offsetof(PlatformSchemaLimits, owned_bytes) == 8u);
static_assert(offsetof(PlatformSchemaLimits, result_bytes) == 16u);
static_assert(offsetof(PlatformSchemaLimits, work_credits) == 24u);
static_assert(offsetof(PlatformSchemaLimits, document_nodes) == 32u);
static_assert(offsetof(PlatformSchemaLimits, records) == 36u);
static_assert(offsetof(PlatformSchemaLimits, depth) == 40u);
static_assert(sizeof(PlatformExtent) == 8u);
static_assert(alignof(PlatformExtent) == 4u);
static_assert(offsetof(PlatformExtent, width) == 0u);
static_assert(offsetof(PlatformExtent, height) == 4u);
static_assert(sizeof(PlatformRect) == 16u);
static_assert(alignof(PlatformRect) == 4u);
static_assert(offsetof(PlatformRect, x) == 0u);
static_assert(offsetof(PlatformRect, y) == 4u);
static_assert(offsetof(PlatformRect, width) == 8u);
static_assert(offsetof(PlatformRect, height) == 12u);
static_assert(sizeof(PlatformRefresh) == 12u);
static_assert(alignof(PlatformRefresh) == 4u);
static_assert(offsetof(PlatformRefresh, precision) == 0u);
static_assert(offsetof(PlatformRefresh, numerator) == 4u);
static_assert(offsetof(PlatformRefresh, denominator) == 8u);
static_assert(sizeof(PlatformFormatValue) == 8u);
static_assert(alignof(PlatformFormatValue) == 4u);
static_assert(offsetof(PlatformFormatValue, value) == 0u);
static_assert(offsetof(PlatformFormatValue, native_code) == 4u);
static_assert(sizeof(PlatformScanValue) == 8u);
static_assert(alignof(PlatformScanValue) == 4u);
static_assert(offsetof(PlatformScanValue, value) == 0u);
static_assert(offsetof(PlatformScanValue, native_code) == 4u);
static_assert(sizeof(PlatformRotationValue) == 8u);
static_assert(alignof(PlatformRotationValue) == 4u);
static_assert(offsetof(PlatformRotationValue, value) == 0u);
static_assert(offsetof(PlatformRotationValue, native_code) == 4u);
static_assert(sizeof(PlatformScalingValue) == 8u);
static_assert(alignof(PlatformScalingValue) == 4u);
static_assert(offsetof(PlatformScalingValue, value) == 0u);
static_assert(offsetof(PlatformScalingValue, native_code) == 4u);
static_assert(sizeof(PlatformConnectionValue) == 8u);
static_assert(alignof(PlatformConnectionValue) == 4u);
static_assert(offsetof(PlatformConnectionValue, value) == 0u);
static_assert(offsetof(PlatformConnectionValue, native_code) == 4u);
static_assert(sizeof(PlatformDesktopState) == 64u);
static_assert(alignof(PlatformDesktopState) == 4u);
static_assert(offsetof(PlatformDesktopState, known_fields) == 0u);
static_assert(offsetof(PlatformDesktopState, rectangle) == 4u);
static_assert(offsetof(PlatformDesktopState, format) == 20u);
static_assert(offsetof(PlatformDesktopState, bits_per_pixel) == 28u);
static_assert(offsetof(PlatformDesktopState, refresh) == 32u);
static_assert(offsetof(PlatformDesktopState, rotation) == 44u);
static_assert(offsetof(PlatformDesktopState, scaling) == 52u);
static_assert(offsetof(PlatformDesktopState, query_id) == 60u);
static_assert(sizeof(PlatformSignalState) == 76u);
static_assert(alignof(PlatformSignalState) == 4u);
static_assert(offsetof(PlatformSignalState, known_fields) == 0u);
static_assert(offsetof(PlatformSignalState, extent) == 4u);
static_assert(offsetof(PlatformSignalState, reported_refresh) == 12u);
static_assert(offsetof(PlatformSignalState, physical_refresh) == 24u);
static_assert(offsetof(PlatformSignalState, virtual_refresh) == 36u);
static_assert(offsetof(PlatformSignalState, scan) == 48u);
static_assert(offsetof(PlatformSignalState, rotation) == 56u);
static_assert(offsetof(PlatformSignalState, scaling) == 64u);
static_assert(offsetof(PlatformSignalState, query_id) == 72u);
static_assert(sizeof(PlatformDesktopImageState) == 48u);
static_assert(alignof(PlatformDesktopImageState) == 4u);
static_assert(offsetof(PlatformDesktopImageState, known_fields) == 0u);
static_assert(offsetof(PlatformDesktopImageState, path_source_extent) == 4u);
static_assert(offsetof(PlatformDesktopImageState, image_region) == 12u);
static_assert(offsetof(PlatformDesktopImageState, image_clip) == 28u);
static_assert(offsetof(PlatformDesktopImageState, query_id) == 44u);
static_assert(sizeof(PlatformByte) == 1u);
static_assert(alignof(PlatformByte) == 1u);
static_assert(offsetof(PlatformByte, value) == 0u);
static_assert(sizeof(PlatformNativeIdentity) == 28u);
static_assert(alignof(PlatformNativeIdentity) == 4u);
static_assert(offsetof(PlatformNativeIdentity, id) == 0u);
static_assert(offsetof(PlatformNativeIdentity, kind) == 4u);
static_assert(offsetof(PlatformNativeIdentity, query_id) == 8u);
static_assert(offsetof(PlatformNativeIdentity, bytes) == 12u);
static_assert(offsetof(PlatformNativeIdentity, text) == 24u);
static_assert(sizeof(PlatformAdapter) == 28u);
static_assert(alignof(PlatformAdapter) == 4u);
static_assert(offsetof(PlatformAdapter, id) == 0u);
static_assert(offsetof(PlatformAdapter, label) == 4u);
static_assert(offsetof(PlatformAdapter, label_query_id) == 8u);
static_assert(offsetof(PlatformAdapter, identities) == 12u);
static_assert(offsetof(PlatformAdapter, discovery_query_id) == 24u);
static_assert(sizeof(PlatformDisplaySource) == 172u);
static_assert(alignof(PlatformDisplaySource) == 4u);
static_assert(offsetof(PlatformDisplaySource, id) == 0u);
static_assert(offsetof(PlatformDisplaySource, adapter_id) == 4u);
static_assert(offsetof(PlatformDisplaySource, active) == 8u);
static_assert(offsetof(PlatformDisplaySource, desktop_attached) == 12u);
static_assert(offsetof(PlatformDisplaySource, primary) == 16u);
static_assert(offsetof(PlatformDisplaySource, identities) == 20u);
static_assert(offsetof(PlatformDisplaySource, desktop) == 32u);
static_assert(offsetof(PlatformDisplaySource, gdi_current) == 96u);
static_assert(offsetof(PlatformDisplaySource, native_flags) == 160u);
static_assert(offsetof(PlatformDisplaySource, topology_query_id) == 164u);
static_assert(offsetof(PlatformDisplaySource, annotation_query_id) == 168u);
static_assert(sizeof(PlatformDisplay) == 56u);
static_assert(alignof(PlatformDisplay) == 4u);
static_assert(offsetof(PlatformDisplay, id) == 0u);
static_assert(offsetof(PlatformDisplay, adapter_id) == 4u);
static_assert(offsetof(PlatformDisplay, label) == 8u);
static_assert(offsetof(PlatformDisplay, label_query_id) == 12u);
static_assert(offsetof(PlatformDisplay, available) == 16u);
static_assert(offsetof(PlatformDisplay, active) == 20u);
static_assert(offsetof(PlatformDisplay, connection) == 24u);
static_assert(offsetof(PlatformDisplay, connection_query_id) == 32u);
static_assert(offsetof(PlatformDisplay, identities) == 36u);
static_assert(offsetof(PlatformDisplay, native_flags) == 48u);
static_assert(offsetof(PlatformDisplay, state_query_id) == 52u);
static_assert(sizeof(PlatformDisplayPath) == 144u);
static_assert(alignof(PlatformDisplayPath) == 4u);
static_assert(offsetof(PlatformDisplayPath, id) == 0u);
static_assert(offsetof(PlatformDisplayPath, source_id) == 4u);
static_assert(offsetof(PlatformDisplayPath, display_id) == 8u);
static_assert(offsetof(PlatformDisplayPath, signal) == 12u);
static_assert(offsetof(PlatformDisplayPath, desktop_image) == 88u);
static_assert(offsetof(PlatformDisplayPath, native_flags) == 136u);
static_assert(offsetof(PlatformDisplayPath, query_id) == 140u);
static_assert(sizeof(PlatformDisplayMode) == 68u);
static_assert(alignof(PlatformDisplayMode) == 4u);
static_assert(offsetof(PlatformDisplayMode, id) == 0u);
static_assert(offsetof(PlatformDisplayMode, catalogue_id) == 4u);
static_assert(offsetof(PlatformDisplayMode, known_fields) == 8u);
static_assert(offsetof(PlatformDisplayMode, extent) == 12u);
static_assert(offsetof(PlatformDisplayMode, refresh) == 20u);
static_assert(offsetof(PlatformDisplayMode, format) == 32u);
static_assert(offsetof(PlatformDisplayMode, bits_per_pixel) == 40u);
static_assert(offsetof(PlatformDisplayMode, scan) == 44u);
static_assert(offsetof(PlatformDisplayMode, scaling) == 52u);
static_assert(offsetof(PlatformDisplayMode, rotation) == 60u);
static_assert(sizeof(PlatformModeCatalogue) == 68u);
static_assert(alignof(PlatformModeCatalogue) == 4u);
static_assert(offsetof(PlatformModeCatalogue, id) == 0u);
static_assert(offsetof(PlatformModeCatalogue, scope) == 4u);
static_assert(offsetof(PlatformModeCatalogue, adapter_id) == 8u);
static_assert(offsetof(PlatformModeCatalogue, source_id) == 12u);
static_assert(offsetof(PlatformModeCatalogue, display_id) == 16u);
static_assert(offsetof(PlatformModeCatalogue, identities) == 20u);
static_assert(offsetof(PlatformModeCatalogue, format_filter) == 32u);
static_assert(offsetof(PlatformModeCatalogue, orientation_filter) == 40u);
static_assert(offsetof(PlatformModeCatalogue, filter_flags) == 48u);
static_assert(offsetof(PlatformModeCatalogue, query_id) == 52u);
static_assert(offsetof(PlatformModeCatalogue, modes) == 56u);
static_assert(sizeof(PlatformQueryResult) == 72u);
static_assert(alignof(PlatformQueryResult) == 8u);
static_assert(offsetof(PlatformQueryResult, id) == 0u);
static_assert(offsetof(PlatformQueryResult, kind) == 4u);
static_assert(offsetof(PlatformQueryResult, api) == 8u);
static_assert(offsetof(PlatformQueryResult, subject_kind) == 12u);
static_assert(offsetof(PlatformQueryResult, subject_id) == 16u);
static_assert(offsetof(PlatformQueryResult, status) == 20u);
static_assert(offsetof(PlatformQueryResult, reason) == 24u);
static_assert(offsetof(PlatformQueryResult, error_domain) == 28u);
static_assert(offsetof(PlatformQueryResult, native_error) == 32u);
static_assert(offsetof(PlatformQueryResult, native_flags) == 36u);
static_assert(offsetof(PlatformQueryResult, returned_count) == 40u);
static_assert(offsetof(PlatformQueryResult, attempts) == 44u);
static_assert(offsetof(PlatformQueryResult, call_count) == 48u);
static_assert(offsetof(PlatformQueryResult, elapsed_ns) == 56u);
static_assert(offsetof(PlatformQueryResult, blocked_by_query_id) == 64u);
static_assert(offsetof(PlatformQueryResult, diagnostic) == 68u);
static_assert(sizeof(PlatformReportHeader) == 296u);
static_assert(alignof(PlatformReportHeader) == 8u);
static_assert(offsetof(PlatformReportHeader, kind) == 0u);
static_assert(offsetof(PlatformReportHeader, schema_version) == 4u);
static_assert(offsetof(PlatformReportHeader, platform) == 8u);
static_assert(offsetof(PlatformReportHeader, query_profile) == 12u);
static_assert(offsetof(PlatformReportHeader, platform_instance) == 16u);
static_assert(offsetof(PlatformReportHeader, capture_id) == 24u);
static_assert(offsetof(PlatformReportHeader, clock) == 32u);
static_assert(offsetof(PlatformReportHeader, visibility_scope) == 36u);
static_assert(offsetof(PlatformReportHeader, capture_begin_ns) == 40u);
static_assert(offsetof(PlatformReportHeader, capture_end_ns) == 48u);
static_assert(offsetof(PlatformReportHeader, discovery) == 56u);
static_assert(offsetof(PlatformReportHeader, coverage) == 60u);
static_assert(offsetof(PlatformReportHeader, capture_attempts) == 64u);
static_assert(offsetof(PlatformReportHeader, native_calls) == 68u);
static_assert(offsetof(PlatformReportHeader, inventory_query_id) == 72u);
static_assert(offsetof(PlatformReportHeader, consistency_query_id) == 76u);
static_assert(offsetof(PlatformReportHeader, limits) == 80u);
static_assert(offsetof(PlatformReportHeader, schema_limits) == 160u);
static_assert(offsetof(PlatformReportHeader, adapters) == 208u);
static_assert(offsetof(PlatformReportHeader, sources) == 220u);
static_assert(offsetof(PlatformReportHeader, displays) == 232u);
static_assert(offsetof(PlatformReportHeader, paths) == 244u);
static_assert(offsetof(PlatformReportHeader, mode_catalogues) == 256u);
static_assert(offsetof(PlatformReportHeader, native_identities) == 268u);
static_assert(offsetof(PlatformReportHeader, queries) == 280u);
}

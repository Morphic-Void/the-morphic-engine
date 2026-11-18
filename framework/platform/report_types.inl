//  Canonical authored schema for the version-1 Platform report.
R"schema(
{
  "types": {
    "enumerations": {
      "PlatformReportKind": {"storage": "u32", "values": {"platform_report": 1}},
      "PlatformBackend": {"storage": "u32", "values": {"windows": 1}},
      "PlatformQueryProfile": {"storage": "u32", "values": {"windows_desktop_discovery_v1": 1}},
      "PlatformClock": {"storage": "u32", "values": {"system_awake_ns": 1}},
      "PlatformVisibilityScope": {"storage": "u32", "values": {"calling_session": 1}},
      "PlatformDiscovery": {"storage": "u32", "values": {"captured": 1, "unsupported": 2, "unavailable": 3}},
      "PlatformCoverage": {"storage": "u32", "values": {"complete": 1, "limited": 2}},
      "PlatformModeScope": {"storage": "u32", "values": {"display": 1, "source": 2, "native_output": 3}},
      "PlatformNativeIdentityKind": {"storage": "u32", "values": {"windows_adapter_luid": 1, "windows_ccd_source": 2, "windows_ccd_target": 3, "windows_adapter_device_path": 4, "windows_monitor_device_path": 5, "windows_gdi_source_name": 6, "windows_dxgi_output_name": 7, "windows_monitor_interface_name": 8}},
      "PlatformQueryKind": {"storage": "u32", "values": {"inventory": 1, "consistency_check": 2, "source_identity": 3, "display_identity": 4, "adapter_identity": 5, "desktop_annotation": 6, "adapter_description": 7, "output_description": 8, "adapter_output_inventory": 9, "mode_catalogue": 10, "current_desktop_settings": 11}},
      "PlatformQueryApi": {"storage": "u32", "values": {"windows_ccd": 1, "windows_display_device_info": 2, "windows_gdi_devices": 3, "windows_gdi_settings": 4, "windows_dxgi": 5}},
      "PlatformSubjectKind": {"storage": "u32", "values": {"report": 1, "adapter": 2, "source": 3, "display": 4, "path": 5, "mode_catalogue": 6, "native_identity": 7, "query": 8}},
      "PlatformQueryStatus": {"storage": "u32", "values": {"success": 1, "unsupported": 2, "unavailable": 3, "failed": 4, "blocked": 5, "not_applicable": 6}},
      "PlatformRefreshPrecision": {"storage": "u32", "values": {"unknown": 0, "rational": 1, "integer_hz": 2, "unspecified": 3}},
      "PlatformTruth": {"storage": "u32", "values": {"unknown": 0, "no": 1, "yes": 2}},
      "PlatformFormat": {"storage": "u32", "values": {"unknown": 0, "rgba8_unorm": 1, "bgra8_unorm": 2, "desktop_8bpp": 3, "desktop_16bpp": 4, "desktop_24bpp": 5, "desktop_32bpp": 6, "other": 255}},
      "PlatformScan": {"storage": "u32", "values": {"unknown": 0, "unspecified": 1, "progressive": 2, "interlaced_upper_first": 3, "interlaced_lower_first": 4, "interlaced_unspecified": 5, "other": 255}},
      "PlatformRotation": {"storage": "u32", "values": {"unknown": 0, "identity": 1, "counterclockwise_90": 2, "counterclockwise_180": 3, "counterclockwise_270": 4, "other": 255}},
      "PlatformScaling": {"storage": "u32", "values": {"unknown": 0, "identity": 1, "centered": 2, "stretched": 3, "aspect_preserving": 4, "custom": 5, "preferred": 6, "unspecified": 7, "other": 255}},
      "PlatformConnection": {"storage": "u32", "values": {"unknown": 0, "internal": 1, "displayport": 2, "hdmi": 3, "dvi": 4, "vga": 5, "wireless": 6, "virtual_output": 7, "other": 255}},
      "PlatformQueryReason": {"storage": "u32", "values": {"none": 0, "api_not_supported": 1, "access_denied": 2, "dependency_missing": 3, "identity_ambiguous": 4, "value_not_reported": 5, "native_failure": 6, "profile_fallback": 7, "buffer_retry_exhausted": 8, "topology_changed": 9}},
      "PlatformErrorDomain": {"storage": "u32", "values": {"none": 0, "win32": 1, "hresult": 2}}
    },
    "bit_structures": {
      "PlatformDesktopFields": { "storage": "u32", "members": [
        {"rectangle": {"mask": 1, "type": "b8"}},
        {"format": {"mask": 2, "type": "b8"}},
        {"depth": {"mask": 4, "type": "b8"}},
        {"refresh": {"mask": 8, "type": "b8"}},
        {"rotation": {"mask": 16, "type": "b8"}},
        {"scaling": {"mask": 32, "type": "b8"}}
      ] },
      "PlatformSignalFields": { "storage": "u32", "members": [
        {"extent": {"mask": 1, "type": "b8"}},
        {"reported_refresh": {"mask": 2, "type": "b8"}},
        {"physical_refresh": {"mask": 4, "type": "b8"}},
        {"virtual_refresh": {"mask": 8, "type": "b8"}},
        {"scan": {"mask": 16, "type": "b8"}},
        {"rotation": {"mask": 32, "type": "b8"}},
        {"scaling": {"mask": 64, "type": "b8"}}
      ] },
      "PlatformDesktopImageFields": { "storage": "u32", "members": [
        {"path_source_extent": {"mask": 1, "type": "b8"}},
        {"image_region": {"mask": 2, "type": "b8"}},
        {"image_clip": {"mask": 4, "type": "b8"}}
      ] },
      "PlatformModeFields": { "storage": "u32", "members": [
        {"extent": {"mask": 1, "type": "b8"}},
        {"refresh": {"mask": 2, "type": "b8"}},
        {"format": {"mask": 4, "type": "b8"}},
        {"depth": {"mask": 8, "type": "b8"}},
        {"scan": {"mask": 16, "type": "b8"}},
        {"scaling": {"mask": 32, "type": "b8"}},
        {"rotation": {"mask": 64, "type": "b8"}}
      ] },
      "PlatformCatalogueFilters": { "storage": "u32", "members": [
        {"monitor_compatible": {"mask": 1, "type": "b8"}},
        {"current_orientation": {"mask": 2, "type": "b8"}},
        {"interlaced": {"mask": 4, "type": "b8"}},
        {"scaled": {"mask": 8, "type": "b8"}},
        {"stereo_excluded": {"mask": 16, "type": "b8"}},
        {"explicit_format": {"mask": 32, "type": "b8"}}
      ] }
    },
    "structures": {
      "PlatformCaptureLimits": { "members": [
        {"capture_attempts": {"type": "u32"}},
        {"query_passes": {"type": "u32"}},
        {"flag_profiles": {"type": "u32"}},
        {"adapters_per_backend": {"type": "u32"}},
        {"sources": {"type": "u32"}},
        {"displays": {"type": "u32"}},
        {"outputs": {"type": "u32"}},
        {"monitor_records": {"type": "u32"}},
        {"ccd_paths": {"type": "u32"}},
        {"ccd_modes": {"type": "u32"}},
        {"modes_per_scope": {"type": "u32"}},
        {"total_mode_rows": {"type": "u32"}},
        {"native_calls": {"type": "u32"}},
        {"owned_bytes": {"type": "u64"}},
        {"report_bytes": {"type": "u64"}},
        {"eligible_duration_ns": {"type": "u64"}}
      ] },
      "PlatformSchemaLimits": { "members": [
        {"inspected_source_bytes": {"type": "u64"}},
        {"owned_bytes": {"type": "u64"}},
        {"result_bytes": {"type": "u64"}},
        {"work_credits": {"type": "u64"}},
        {"document_nodes": {"type": "u32"}},
        {"records": {"type": "u32"}},
        {"depth": {"type": "u32"}}
      ] },
      "PlatformExtent": { "members": [
        {"width": {"type": "u32"}},
        {"height": {"type": "u32"}}
      ] },
      "PlatformRect": { "members": [
        {"x": {"type": "i32"}},
        {"y": {"type": "i32"}},
        {"width": {"type": "u32"}},
        {"height": {"type": "u32"}}
      ] },
      "PlatformRefresh": { "members": [
        {"precision": {"type": "PlatformRefreshPrecision"}},
        {"numerator": {"type": "u32"}},
        {"denominator": {"type": "u32"}}
      ] },
      "PlatformFormatValue": { "members": [
        {"value": {"type": "PlatformFormat"}},
        {"native_code": {"type": "u32"}}
      ] },
      "PlatformScanValue": { "members": [
        {"value": {"type": "PlatformScan"}},
        {"native_code": {"type": "u32"}}
      ] },
      "PlatformRotationValue": { "members": [
        {"value": {"type": "PlatformRotation"}},
        {"native_code": {"type": "u32"}}
      ] },
      "PlatformScalingValue": { "members": [
        {"value": {"type": "PlatformScaling"}},
        {"native_code": {"type": "u32"}}
      ] },
      "PlatformConnectionValue": { "members": [
        {"value": {"type": "PlatformConnection"}},
        {"native_code": {"type": "u32"}}
      ] },
      "PlatformDesktopState": { "members": [
        {"known_fields": {"type": "PlatformDesktopFields"}},
        {"rectangle": {"type": "PlatformRect"}},
        {"format": {"type": "PlatformFormatValue"}},
        {"bits_per_pixel": {"type": "u32"}},
        {"refresh": {"type": "PlatformRefresh"}},
        {"rotation": {"type": "PlatformRotationValue"}},
        {"scaling": {"type": "PlatformScalingValue"}},
        {"query_id": {"type": "u32"}}
      ] },
      "PlatformSignalState": { "members": [
        {"known_fields": {"type": "PlatformSignalFields"}},
        {"extent": {"type": "PlatformExtent"}},
        {"reported_refresh": {"type": "PlatformRefresh"}},
        {"physical_refresh": {"type": "PlatformRefresh"}},
        {"virtual_refresh": {"type": "PlatformRefresh"}},
        {"scan": {"type": "PlatformScanValue"}},
        {"rotation": {"type": "PlatformRotationValue"}},
        {"scaling": {"type": "PlatformScalingValue"}},
        {"query_id": {"type": "u32"}}
      ] },
      "PlatformDesktopImageState": { "members": [
        {"known_fields": {"type": "PlatformDesktopImageFields"}},
        {"path_source_extent": {"type": "PlatformExtent"}},
        {"image_region": {"type": "PlatformRect"}},
        {"image_clip": {"type": "PlatformRect"}},
        {"query_id": {"type": "u32"}}
      ] },
      "PlatformByte": { "members": [
        {"value": {"type": "u8"}}
      ] },
      "PlatformNativeIdentity": { "members": [
        {"id": {"type": "u32"}},
        {"kind": {"type": "PlatformNativeIdentityKind"}},
        {"query_id": {"type": "u32"}},
        {"bytes": {"type": {"kind": "span", "element": "PlatformByte", "nullable": true}}},
        {"text": {"type": {"kind": "text", "nullable": true}}}
      ] },
      "PlatformAdapter": { "members": [
        {"id": {"type": "u32"}},
        {"label": {"type": {"kind": "text", "nullable": true}}},
        {"label_query_id": {"type": "u32"}},
        {"identities": {"type": {"kind": "span", "element": "PlatformNativeIdentity", "nullable": true}}},
        {"discovery_query_id": {"type": "u32"}}
      ] },
      "PlatformDisplaySource": { "members": [
        {"id": {"type": "u32"}},
        {"adapter_id": {"type": "u32"}},
        {"active": {"type": "PlatformTruth"}},
        {"desktop_attached": {"type": "PlatformTruth"}},
        {"primary": {"type": "PlatformTruth"}},
        {"identities": {"type": {"kind": "span", "element": "PlatformNativeIdentity", "nullable": true}}},
        {"desktop": {"type": "PlatformDesktopState"}},
        {"gdi_current": {"type": "PlatformDesktopState"}},
        {"native_flags": {"type": "u32"}},
        {"topology_query_id": {"type": "u32"}},
        {"annotation_query_id": {"type": "u32"}}
      ] },
      "PlatformDisplay": { "members": [
        {"id": {"type": "u32"}},
        {"adapter_id": {"type": "u32"}},
        {"label": {"type": {"kind": "text", "nullable": true}}},
        {"label_query_id": {"type": "u32"}},
        {"available": {"type": "PlatformTruth"}},
        {"active": {"type": "PlatformTruth"}},
        {"connection": {"type": "PlatformConnectionValue"}},
        {"connection_query_id": {"type": "u32"}},
        {"identities": {"type": {"kind": "span", "element": "PlatformNativeIdentity", "nullable": true}}},
        {"native_flags": {"type": "u32"}},
        {"state_query_id": {"type": "u32"}}
      ] },
      "PlatformDisplayPath": { "members": [
        {"id": {"type": "u32"}},
        {"source_id": {"type": "u32"}},
        {"display_id": {"type": "u32"}},
        {"signal": {"type": "PlatformSignalState"}},
        {"desktop_image": {"type": "PlatformDesktopImageState"}},
        {"native_flags": {"type": "u32"}},
        {"query_id": {"type": "u32"}}
      ] },
      "PlatformDisplayMode": { "members": [
        {"id": {"type": "u32"}},
        {"catalogue_id": {"type": "u32"}},
        {"known_fields": {"type": "PlatformModeFields"}},
        {"extent": {"type": "PlatformExtent"}},
        {"refresh": {"type": "PlatformRefresh"}},
        {"format": {"type": "PlatformFormatValue"}},
        {"bits_per_pixel": {"type": "u32"}},
        {"scan": {"type": "PlatformScanValue"}},
        {"scaling": {"type": "PlatformScalingValue"}},
        {"rotation": {"type": "PlatformRotationValue"}}
      ] },
      "PlatformModeCatalogue": { "members": [
        {"id": {"type": "u32"}},
        {"scope": {"type": "PlatformModeScope"}},
        {"adapter_id": {"type": "u32"}},
        {"source_id": {"type": "u32"}},
        {"display_id": {"type": "u32"}},
        {"identities": {"type": {"kind": "span", "element": "PlatformNativeIdentity", "nullable": true}}},
        {"format_filter": {"type": "PlatformFormatValue"}},
        {"orientation_filter": {"type": "PlatformRotationValue"}},
        {"filter_flags": {"type": "PlatformCatalogueFilters"}},
        {"query_id": {"type": "u32"}},
        {"modes": {"type": {"kind": "span", "element": "PlatformDisplayMode", "nullable": true}}}
      ] },
      "PlatformQueryResult": { "members": [
        {"id": {"type": "u32"}},
        {"kind": {"type": "PlatformQueryKind"}},
        {"api": {"type": "PlatformQueryApi"}},
        {"subject_kind": {"type": "PlatformSubjectKind"}},
        {"subject_id": {"type": "u32"}},
        {"status": {"type": "PlatformQueryStatus"}},
        {"reason": {"type": "PlatformQueryReason"}},
        {"error_domain": {"type": "PlatformErrorDomain"}},
        {"native_error": {"type": "u32"}},
        {"native_flags": {"type": "u32"}},
        {"returned_count": {"type": "u32"}},
        {"attempts": {"type": "u32"}},
        {"call_count": {"type": "u32"}},
        {"elapsed_ns": {"type": "u64"}},
        {"blocked_by_query_id": {"type": "u32"}},
        {"diagnostic": {"type": {"kind": "text", "nullable": true}}}
      ] },
      "PlatformReportHeader": { "members": [
        {"kind": {"type": "PlatformReportKind"}},
        {"schema_version": {"type": "u32"}},
        {"platform": {"type": "PlatformBackend"}},
        {"query_profile": {"type": "PlatformQueryProfile"}},
        {"platform_instance": {"type": "u64"}},
        {"capture_id": {"type": "u64"}},
        {"clock": {"type": "PlatformClock"}},
        {"visibility_scope": {"type": "PlatformVisibilityScope"}},
        {"capture_begin_ns": {"type": "u64"}},
        {"capture_end_ns": {"type": "u64"}},
        {"discovery": {"type": "PlatformDiscovery"}},
        {"coverage": {"type": "PlatformCoverage"}},
        {"capture_attempts": {"type": "u32"}},
        {"native_calls": {"type": "u32"}},
        {"inventory_query_id": {"type": "u32"}},
        {"consistency_query_id": {"type": "u32"}},
        {"limits": {"type": "PlatformCaptureLimits"}},
        {"schema_limits": {"type": "PlatformSchemaLimits"}},
        {"adapters": {"type": {"kind": "span", "element": "PlatformAdapter", "nullable": true}}},
        {"sources": {"type": {"kind": "span", "element": "PlatformDisplaySource", "nullable": true}}},
        {"displays": {"type": {"kind": "span", "element": "PlatformDisplay", "nullable": true}}},
        {"paths": {"type": {"kind": "span", "element": "PlatformDisplayPath", "nullable": true}}},
        {"mode_catalogues": {"type": {"kind": "span", "element": "PlatformModeCatalogue", "nullable": true}}},
        {"native_identities": {"type": {"kind": "span", "element": "PlatformNativeIdentity"}}},
        {"queries": {"type": {"kind": "span", "element": "PlatformQueryResult"}}}
      ] }
    }
  }
}
)schema"

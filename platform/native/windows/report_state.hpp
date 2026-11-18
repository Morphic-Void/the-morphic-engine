
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_state.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Windows-only capture cursor, native buffers, retained COM interfaces and identity correlations.

#pragma once

#include "runtime/report_candidate.hpp"

struct IDXGIFactory1;
struct IDXGIAdapter1;
struct IDXGIOutput;
struct IDXGIOutput1;

namespace platform::native
{

enum class EQueryPhase
{
    ccd_size, ccd_fetch, translate, adapter_name, source_name, target_name,
    gdi_adapter, gdi_monitor, current_settings, factory, dxgi_adapter, adapter_desc,
    output, output_desc, output_interface, mode_count, mode_fetch, mode_rows,
    gdi_mode, finalise, compare, labels
};

struct SCcdKey
{
    std::uint32_t low{}, high{}, endpoint{};
    bool operator==(const SCcdKey other) const noexcept
    {
        return (low == other.low) && (high == other.high) && (endpoint == other.endpoint);
    }
};

struct SReportSubject
{
    SCcdKey key;
    wchar_t name[32]{};
    std::uint32_t name_query{};
    bool dxgi_complete{}, fallback_needed{};
};

struct SIdentityObservation
{
    platform_report_v1::PlatformSubjectKind subject{};
    std::uint32_t subject_id{};
    platform_report_v1::PlatformNativeIdentity identity{};
};

struct SCoreObservation
{
    SCcdKey source{}, target{};
    platform_report_v1::PlatformDesktopState desktop{};
    platform_report_v1::PlatformSignalState signal{};
    platform_report_v1::PlatformDesktopImageState image{};
    std::uint32_t path_flags{}, source_flags{}, target_flags{}, technology{}, available{};
};

struct SHeapCursor
{
    std::size_t built{}, remaining{};
    bool started{};
};

struct SWindowsReportApi;
struct SWindowsReport
{
    ~SWindowsReport() noexcept;
    const SWindowsReportApi* api{};
    EQueryPhase phase{ EQueryPhase::ccd_size };
    CByteBuffer ccd_paths[2], ccd_modes[2], mode_buffer;
    std::uint32_t paths[2]{}, modes[2]{};
    TPodVector<SReportSubject> adapters, sources, displays;
    TPodVector<SIdentityObservation> evidence;
    TPodVector<SCoreObservation> core[2];
    IDXGIFactory1* factory{};
    IDXGIAdapter1* adapter{};
    IDXGIOutput* output{};
    IDXGIOutput1* output1{};
    wchar_t gdi_name[32]{}, output_name[32]{};
    std::uint32_t snapshot{}, profile{}, pass{}, query{}, index{}, adapter_index{}, output_index{}, monitor_index{};
    std::uint32_t monitor_count{}, output_count{}, format_index{}, mode_count{}, mode_index{};
    std::uint32_t adapter_id{}, source_id{}, catalogue_id{}, gdi_source{}, inventory_id{}, mode_first{};
    std::uint32_t dxgi_inventory{}, gdi_inventory{}, output_description{}, output_interface_query{};
    SHeapCursor order;
    std::uint32_t sort_group{}, compared[2]{};
    bool list_failed{};
};

}   //  namespace platform::native

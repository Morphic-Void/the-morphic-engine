
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_api.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Binds the private Windows discovery calls to the SDK or a deterministic test driver.

#pragma once

#include "platform/windows_include.hpp"
#include <dxgi1_2.h>

namespace platform::native
{

struct SReportSession;
enum class EReportStep;

struct SWindowsReportApi
{
    decltype(&GetDisplayConfigBufferSizes) ccd_sizes;
    decltype(&QueryDisplayConfig) ccd_fetch;
    decltype(&DisplayConfigGetDeviceInfo) device_info;
    decltype(&EnumDisplayDevicesW) devices;
    decltype(&EnumDisplaySettingsExW) settings;
    HRESULT (*factory)(IDXGIFactory1**) noexcept;
    HRESULT (*adapter)(IDXGIFactory1*, UINT, IDXGIAdapter1**) noexcept;
    HRESULT (*adapter_desc)(IDXGIAdapter1*, DXGI_ADAPTER_DESC1*) noexcept;
    HRESULT (*output)(IDXGIAdapter1*, UINT, IDXGIOutput**) noexcept;
    HRESULT (*output_desc)(IDXGIOutput*, DXGI_OUTPUT_DESC*) noexcept;
    HRESULT (*output1)(IDXGIOutput*, IDXGIOutput1**) noexcept;
    HRESULT (*modes)(IDXGIOutput1*, DXGI_FORMAT, UINT, UINT*, DXGI_MODE_DESC1*) noexcept;
    void (*release)(IUnknown*) noexcept;
};

[[nodiscard]] const SWindowsReportApi& windows_report_api() noexcept;
[[nodiscard]] EReportStep windows_report_step(SReportSession& session, const SWindowsReportApi& api) noexcept;

}   //  namespace platform::native

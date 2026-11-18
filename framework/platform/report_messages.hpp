
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:    report_messages.hpp
//  Authors: Ritchie Brannan / OpenAI Codex
//  Date:    18 Nov 26
//
//  Authenticated Host report acquisition and exact-installation capture handoff.

#pragma once

#include "framework/assets/asset_messages.hpp"
#include "framework/module/host_eligible_budget.hpp"
#include "framework/threading/messages/CErasedPodMsg.hpp"

namespace platform::reports
{

enum class EStatus : std::uint8_t
{
    success, unavailable, not_ready, update_in_progress, invalid_request, failed,
    allocation_failed, interrupted, delivery_failed
};
enum class EAvailability : std::uint8_t { unavailable, not_ready, available };
enum class ELastUpdate : std::uint8_t { none, succeeded, failed };
enum class EStage : std::uint8_t { none, capture, validation, transfer, publication, lifecycle };
enum class EReason : std::uint8_t
{
    none, native_failure, invalid_data, retry_exhausted, limit_exceeded, deadline_exceeded,
    allocation_failed, transport_failed, closing, clock_failed, unsupported_backend
};
enum class EErrorDomain : std::uint8_t { none, win32, hresult };
enum class EDisposition : std::uint8_t { published, rejected, interrupted };

struct SFailure
{
    EStage stage{};
    EReason reason{};
    EErrorDomain domain{};
    std::uint32_t native_error{};
};

struct PlatformReportRequest {};
struct PlatformReportUpdateRequest {};
struct PlatformReportResult
{
    EStatus status{ EStatus::unavailable };
    EAvailability availability{};
    bool update_in_progress{};
    ELastUpdate last_update{};
    SFailure failure;
    CAssetAcquisitionId acquisition;
    CByteConstView bytes;
};

struct SCaptureIdentity
{
    modules::SModuleInstance platform_instance;
    std::uint64_t capture_id{};
};
struct PlatformReportCaptureRequest
{
    SCaptureIdentity capture;
    modules::SEligibleBudgetAnchor deadline;
};
struct PlatformReportCaptured
{
    SCaptureIdentity capture;
    CBakedDocumentBlock storage;
};
struct PlatformReportCaptureFailed { SCaptureIdentity capture; SFailure failure; };
struct PlatformReportCaptureCancel { SCaptureIdentity capture; EReason reason; };
struct PlatformReportCaptureReceipt { SCaptureIdentity capture; EDisposition disposition; SFailure failure; };

}   //  namespace platform::reports

MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportRequest, system_type_ids::platform_report_request);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportUpdateRequest, system_type_ids::platform_report_update_request);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportResult, system_type_ids::platform_report_result);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportCaptureRequest, system_type_ids::platform_report_capture_request);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportCaptured, system_type_ids::platform_report_captured);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportCaptureFailed, system_type_ids::platform_report_capture_failed);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportCaptureCancel, system_type_ids::platform_report_capture_cancel);
MV_REGISTER_SYSTEM_TYPE(platform::reports::PlatformReportCaptureReceipt, system_type_ids::platform_report_capture_receipt);

#define MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE(type, member) MV_REGISTER_ERASED_OWNER_PAYLOAD(type);
#include "framework/platform/report_owner_payloads.def"
#undef MV_ERASED_OWNER_PAYLOAD_WITH_STORAGE

static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportUpdateRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportResult>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportCaptureRequest>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportCaptureFailed>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportCaptureCancel>());
static_assert(threading::CErasedPodMsg::is_payload_compatible_with<platform::reports::PlatformReportCaptureReceipt>());

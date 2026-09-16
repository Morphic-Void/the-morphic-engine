
//  Copyright (c) 2026 Ritchie Brannan / Morphic Void Limited
//  License: MIT (see LICENSE file in repository root)
//
//  File:   service.hpp
//  Primary implementation: OpenAI tools
//  Reviewed and accepted by: Ritchie Brannan
//  Date:   28 Jul 26
//
//  Bounded executable-owned debug service substrate.
//
//  This service underpins the public debug macro interface. It owns shared
//  provisioning, bounded transport, direct fallback, and writer lifetime for
//  normal debug reporting.

#pragma once

#ifndef DEBUG_SERVICE_HPP_INCLUDED
#define DEBUG_SERVICE_HPP_INCLUDED

#include <cstddef>
#include <cstdarg>
#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include "debug/event_arguments.hpp"
#include "platform/filesystem/log.hpp"
#include "platform/threading/exclusive_lock.hpp"
#include "platform/threading/thread_lifetime.hpp"
#include "system/system_context.hpp"
#include "threading/transports/TMpmcTransport.hpp"
#include "types/atomic_types.hpp"

namespace debug_system
{

constexpr std::uint32_t k_breakpoints_enabled = 1u << 0u;
constexpr std::uint32_t k_information_level_shift = 1u;
constexpr std::uint32_t k_information_level_mask = 3u << k_information_level_shift;
constexpr std::uint32_t k_critical_shutdown_enabled = 1u << 3u;
constexpr std::uint32_t k_default_configuration = k_breakpoints_enabled | k_information_level_mask;
constexpr std::size_t k_event_transport_element_size = 512u;
constexpr std::size_t k_event_expression_capacity = 192u;
constexpr std::size_t k_event_format_capacity = 128u;
constexpr std::size_t k_event_report_capacity = 448u;
constexpr std::size_t k_event_report_max_size = k_event_report_capacity - 1u;
constexpr std::size_t k_source_file_capacity = 32u;
constexpr std::size_t k_format_buffer_capacity = 4096u;
constexpr std::size_t k_log_path_capacity = 260u;
constexpr std::uint32_t k_event_transport_capacity_hint = 128u;

enum class EShutdownReason : std::uint32_t
{
    none = 0u,
    critical_incident,
    fatal_incident,
    panic_incident
};

enum class EServiceThreadState : std::uint32_t
{
    empty = 0u,
    starting,
    running,
    stopped,
    failed
};

enum class EEventRepresentation : std::uint8_t
{
    structured = 0u,
    report
};

enum class EStructuredContentType : std::uint8_t
{
    text = 0u,
    format
};

enum class EEventLevel : std::uint8_t
{
    info = 0u,
    detail,
    trace,
    assert,
    warning,
    error,
    critical,
    fatal
};

enum class EEventType : std::uint8_t
{
    condition = 0u,
    event
};

enum class EBreakpointOverride : std::uint32_t
{
    inherit = 0u,
    enabled,
    disabled
};

//==============================================================================
//  Event metadata validation
//==============================================================================

[[nodiscard]] constexpr bool is_valid_event_metadata(const EEventLevel level, const EEventType type) noexcept
{
    switch (level)
    {
        case EEventLevel::info:
        case EEventLevel::detail:
        case EEventLevel::trace:
        case EEventLevel::warning:
        case EEventLevel::error:
        {
            return type == EEventType::event;
        }
        case EEventLevel::assert:
        {
            return type == EEventType::condition;
        }
        case EEventLevel::critical:
        case EEventLevel::fatal:
        {
            return (type == EEventType::condition) || (type == EEventType::event);
        }
        default:
        {
            return false;
        }
    }
}

struct SEventUsagePoint
{
    const char* file = nullptr;
    std::size_t file_size = 0u;
    std::uint32_t line = 0u;
};

struct SEventSource
{
    system_ids::id_type system_id{};
    std::uint32_t line = 0u;
    std::uint8_t filename_size = 0u;
    char filename[k_source_file_capacity]{};
};

struct SIncidentContext
{
    std::uint32_t incident_id = 0u;
    SEventSource source;
};

struct SStructuredEventMetadata
{
    std::uint8_t expression_size = 0u;
    std::uint8_t format_size = 0u;
    EStructuredContentType content_type = EStructuredContentType::text;
    std::uint8_t parameter_count = 0u;
    EEventArgumentType parameter_types[k_event_argument_count]{};
};

struct SReportEventMetadata
{
    std::uint16_t report_length = 0u;
    std::uint8_t reserved[10u]{};
};

union SEventMetadata
{
    SStructuredEventMetadata structured{};
    SReportEventMetadata report;
};

struct alignas(64) SStructuredEventStorage
{
    char expression[k_event_expression_capacity]{};
    char format[k_event_format_capacity]{};
    alignas(64) SEventParameterValue parameters[k_event_argument_count]{};
};

union alignas(64) SEventStorage
{
    SStructuredEventStorage structured{};
    char report[k_event_report_capacity];
};

struct alignas(64) SEvent
{
    system_ids::id_type system_id{};
    std::uint32_t incident_id = 0u;
    std::uint32_t source_line = 0u;
    EEventRepresentation representation = EEventRepresentation::structured;
    EEventLevel level = EEventLevel::info;
    EEventType type = EEventType::event;
    std::uint8_t filename_size = 0u;
    SEventMetadata metadata;
    char filename[k_source_file_capacity]{};
    SEventStorage storage;
};

struct SBreakpointContext
{
    std::uint32_t incident_id = 0u;
    system_ids::id_type system_id{};
    std::uint32_t source_line = 0u;
};

static_assert((sizeof(system_ids::id_type) == 8u), "Transported system IDs must retain their full 64-bit representation.");
static_assert((sizeof(EEventRepresentation) == 1u), "Event representations must occupy one byte.");
static_assert((sizeof(EStructuredContentType) == 1u), "Structured content types must occupy one byte.");
static_assert((sizeof(EEventLevel) == 1u), "Event levels must occupy one byte.");
static_assert((sizeof(EEventType) == 1u), "Event types must occupy one byte.");
static_assert((sizeof(EEventArgumentType) == 1u), "Event argument types must occupy one byte.");
static_assert((sizeof(SStructuredEventMetadata) == 12u), "Structured event metadata must occupy bytes 20-31.");
static_assert((sizeof(SReportEventMetadata) == 12u), "Report event metadata must occupy bytes 20-31.");
static_assert((sizeof(SEventMetadata) == 12u), "Event metadata must retain its 12-byte overlay.");
static_assert((sizeof(SStructuredEventStorage) == k_event_report_capacity), "Structured event storage must occupy bytes 64-511.");
static_assert((sizeof(SEventStorage) == k_event_report_capacity), "Event storage must retain its 448-byte overlay.");
static_assert(std::is_standard_layout_v<SStructuredEventMetadata> && std::is_trivially_copyable_v<SStructuredEventMetadata>);
static_assert(std::is_standard_layout_v<SReportEventMetadata> && std::is_trivially_copyable_v<SReportEventMetadata>);
static_assert(std::is_standard_layout_v<SEventMetadata> && std::is_trivially_copyable_v<SEventMetadata>);
static_assert(std::is_standard_layout_v<SStructuredEventStorage> && std::is_trivially_copyable_v<SStructuredEventStorage>);
static_assert(std::is_standard_layout_v<SEventStorage> && std::is_trivially_copyable_v<SEventStorage>);
static_assert((sizeof(SEvent) == k_event_transport_element_size), "SEvent must retain its fixed 512-byte transport representation.");
static_assert((alignof(SEvent) == 64u), "SEvent must remain explicitly 64-byte aligned.");
static_assert(std::is_standard_layout_v<SEvent>, "SEvent must retain standard layout for explicit offset checks.");
static_assert(std::is_trivially_copyable_v<SEvent>, "SEvent must remain trivially copyable.");
static_assert((offsetof(SEvent, system_id) == 0u), "SEvent system-ID offset changed.");
static_assert((offsetof(SEvent, incident_id) == 8u), "SEvent incident-ID offset changed.");
static_assert((offsetof(SEvent, source_line) == 12u), "SEvent source-line offset changed.");
static_assert((offsetof(SEvent, representation) == 16u), "SEvent representation offset changed.");
static_assert((offsetof(SEvent, level) == 17u), "SEvent level offset changed.");
static_assert((offsetof(SEvent, type) == 18u), "SEvent type offset changed.");
static_assert((offsetof(SEvent, filename_size) == 19u), "SEvent filename-size offset changed.");
static_assert((offsetof(SEvent, metadata) == 20u), "SEvent metadata offset changed.");
static_assert((offsetof(SEvent, filename) == 32u), "SEvent filename offset changed.");
static_assert((offsetof(SEvent, storage) == 64u), "SEvent storage offset changed.");
static_assert((offsetof(SStructuredEventMetadata, expression_size) == 0u), "Structured expression-size offset changed.");
static_assert((offsetof(SStructuredEventMetadata, format_size) == 1u), "Structured format-size offset changed.");
static_assert((offsetof(SStructuredEventMetadata, content_type) == 2u), "Structured content-type offset changed.");
static_assert((offsetof(SStructuredEventMetadata, parameter_count) == 3u), "Structured parameter-count offset changed.");
static_assert((offsetof(SStructuredEventMetadata, parameter_types) == 4u), "Structured parameter-type offset changed.");
static_assert((offsetof(SReportEventMetadata, report_length) == 0u), "Report length offset changed.");
static_assert((offsetof(SReportEventMetadata, reserved) == 2u), "Report reserved metadata offset changed.");
static_assert((offsetof(SStructuredEventStorage, expression) == 0u), "Structured expression offset changed.");
static_assert((offsetof(SStructuredEventStorage, format) == 192u), "Structured format offset changed.");
static_assert((offsetof(SStructuredEventStorage, parameters) == 320u), "Structured parameter offset changed.");
static_assert((((offsetof(SEvent, storage) + offsetof(SStructuredEventStorage, parameters)) % 64u) == 0u),
    "SEvent parameters must begin on a cache-line boundary.");

using CEventTransport = threading::transports::TMpmcArenaTransport<SEvent, k_event_transport_capacity_hint>;
using CReservedEventSlot = threading::transports::TReservedArenaSlot<SEvent, k_event_transport_capacity_hint>;
using CAcquiredEventSlot = threading::transports::TAcquiredArenaSlot<SEvent, k_event_transport_capacity_hint>;

struct SDebugServiceTestAccess;

class CDebugServiceState
{
public:
    CDebugServiceState() noexcept = default;
    CDebugServiceState(const CDebugServiceState&) = delete;
    CDebugServiceState& operator=(const CDebugServiceState&) = delete;
    CDebugServiceState(CDebugServiceState&&) = delete;
    CDebugServiceState& operator=(CDebugServiceState&&) = delete;
    ~CDebugServiceState() noexcept = default;

    [[nodiscard]] bool configure_log_paths(const char* const event_log_path, const char* const direct_log_path) noexcept;
    //  Explicit provisioning before installing the service or starting its writer.
    //  Failure rolls back an event log opened by this call. Reporting never opens
    //  files; provisioning must not run concurrently with reporting.
    [[nodiscard]] bool open_logs() noexcept;

    [[nodiscard]] bool start() noexcept;
    [[nodiscard]] bool stop() noexcept;

    [[nodiscard]] EServiceThreadState thread_state() const noexcept;
    [[nodiscard]] bool is_event_transport_closed() const noexcept;

    [[nodiscard]] std::uint32_t allocate_incident_id() noexcept;
    [[nodiscard]] bool submit_text(const char* const text) noexcept;
    [[nodiscard]] bool report_va(const SEventUsagePoint& usage_point, const char* const format, std::va_list arguments) noexcept;
    [[nodiscard]] bool report_immediate_va(const SEventUsagePoint& usage_point, const char* const format, std::va_list arguments) noexcept;
    [[nodiscard]] bool try_write_panic_record(const SEventUsagePoint& usage_point, const std::uint32_t incident_id, const char* const text, const std::size_t text_size) noexcept;

    template<EEventLevel t_level, EEventType t_type, typename... Args>
    [[nodiscard]] bool submit_event(const SEventUsagePoint& usage_point, const char* const format, const std::size_t format_size, Args&&... arguments) noexcept;

    template<EEventLevel t_level, EEventType t_type, typename... Args>
    [[nodiscard]] bool process_event(
        const SEventUsagePoint& usage_point, EBreakpointOverride& breakpoint_override,
        const bool breakpoint_enabled_by_default, const EShutdownReason shutdown_reason,
        const char* const expression, const std::size_t expression_size,
        const char* const format, const std::size_t format_size, Args&&... arguments) noexcept;

    void publish_configuration(const std::uint32_t configuration) noexcept;
    [[nodiscard]] std::uint32_t read_configuration() const noexcept;
    [[nodiscard]] bool informational_event_enabled(const EEventLevel level) const noexcept;
    [[nodiscard]] bool critical_shutdown_enabled() const noexcept;

    [[nodiscard]] bool breakpoint_enabled(const EBreakpointOverride override, const bool enabled_by_default) const noexcept;
    [[nodiscard]] bool breakpoint_pause_requested() const noexcept;
    [[nodiscard]] bool read_breakpoint_context(SBreakpointContext& destination) const noexcept;
    void react_to_breakpoint(EBreakpointOverride& override, const bool enabled_by_default, const SIncidentContext& incident) noexcept;

    void request_shutdown(const EShutdownReason reason) noexcept;
    [[nodiscard]] EShutdownReason read_shutdown_request() const noexcept;

    [[nodiscard]] const char* event_log_path() const noexcept { return m_event_log_path; }

    [[nodiscard]] const char* direct_log_path() const noexcept { return m_direct_log_path; }

private:
    friend struct SDebugServiceTestAccess;

    static constexpr char k_invalid_event[] = "Invalid transported debug event";
    static constexpr std::size_t k_invalid_event_size = sizeof(k_invalid_event) - 1u;
    static constexpr char k_invalid_report[] = "Invalid rich debug report";
    static constexpr std::size_t k_invalid_report_size = sizeof(k_invalid_report) - 1u;

    [[nodiscard]] static bool copy_path(char (&destination)[k_log_path_capacity], const char* const source) noexcept;
    [[nodiscard]] static bool write_record(platform::filesystem::Log& log,
        const EEventLevel level, const EEventType type, const SIncidentContext& incident,
        const char* const text, const std::size_t text_size) noexcept;
    [[nodiscard]] static bool write_named_record(platform::filesystem::Log& log,
        const char* const level, const char* const type, const SIncidentContext& incident,
        const char* const text, const std::size_t text_size) noexcept;
    [[nodiscard]] static bool capture_source(SEventSource& destination, const system_ids::id_type system_id, const SEventUsagePoint& usage_point) noexcept;
    [[nodiscard]] bool capture_incident(SIncidentContext& destination, const SEventUsagePoint& usage_point) noexcept;
    [[nodiscard]] static EEventFormatResult format_event_content(
        char* const destination, const std::size_t destination_capacity,
        const char* const expression, const std::size_t expression_size,
        const char* const format, const std::size_t format_size,
        const std::uint8_t parameter_count,
        const EEventArgumentType (&parameter_types)[k_event_argument_count],
        const SEventParameterValue (&parameters)[k_event_argument_count],
        std::size_t& output_size) noexcept;
    [[nodiscard]] static const char* level_name(const EEventLevel level) noexcept;
    [[nodiscard]] static const char* type_name(const EEventType type) noexcept;
    static std::uint32_t MV_STD_ABI_CALL writer_thread_entry(void* const user_data) noexcept;
    [[nodiscard]] std::uint32_t writer_thread_main() noexcept;

    [[nodiscard]] bool log_paths_configured() const noexcept;
    [[nodiscard]] bool open_event_log() noexcept;
    [[nodiscard]] bool open_direct_log() noexcept;
    void close_logs() noexcept;
    void signal_writer() noexcept;
    [[nodiscard]] bool write_direct_record(
        const EEventLevel level, const EEventType type, const SIncidentContext& incident,
        const char* const text, const std::size_t text_size) noexcept;
    [[nodiscard]] bool write_direct_event(
        const EEventLevel level, const EEventType type, const SIncidentContext& incident,
        const char* const expression, const std::size_t expression_size,
        const char* const format, const std::size_t format_size,
        const SEventArguments& arguments) noexcept;
    [[nodiscard]] bool submit_report_event(
        const SIncidentContext& incident,
        const char* const text, const std::size_t text_size) noexcept;
    [[nodiscard]] bool write_event(const SEvent& event) noexcept;

    template<EEventLevel t_level, EEventType t_type, typename... Args>
    [[nodiscard]] bool submit_captured_event(
        const SIncidentContext& incident,
        const char* const expression, const std::size_t expression_size,
        const char* const format, const std::size_t format_size,
        Args&&... arguments) noexcept;

    TCacheLineAtomic<std::uint32_t> m_incident_counter{};
    TCacheLineAtomic<std::uint32_t> m_shutdown_request{};
    TCacheLineAtomic<std::uint32_t> m_configuration{ k_default_configuration };
    TCacheLineAtomic<std::uint32_t> m_writer_wake_epoch{};
    TCacheLineAtomic<std::uint32_t> m_writer_state{ static_cast<std::uint32_t>(EServiceThreadState::empty) };
    TCacheLineAtomic<std::uint32_t> m_breakpoint_state{};

    std::atomic<std::uint32_t> m_breakpoint_incident_id{};
    std::atomic<std::uint64_t> m_breakpoint_system_id{};
    std::atomic<std::uint32_t> m_breakpoint_source_line{};

    platform::threading::CExclusiveLock m_direct_lock;
    platform::filesystem::Log m_direct_log;
    char m_direct_log_path[k_log_path_capacity]{};
    char m_direct_format_buffer[k_format_buffer_capacity]{};

    platform::threading::CThread m_writer_thread;
    platform::filesystem::Log m_event_log;
    char m_event_log_path[k_log_path_capacity]{};
    char m_event_format_buffer[k_format_buffer_capacity]{};
    CEventTransport m_event_transport;
};

template<EEventLevel t_level, EEventType t_type, typename... Args>
bool CDebugServiceState::submit_captured_event(
    const SIncidentContext& incident,
    const char* const expression, const std::size_t expression_size,
    const char* const format, const std::size_t format_size,
    Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");

    if (((expression_size != 0u) && (expression == nullptr)) ||
        (expression_size >= k_event_expression_capacity) ||
        ((format_size != 0u) && (format == nullptr)) ||
        (format_size >= k_event_format_capacity) ||
        ((expression_size == 0u) && (format_size == 0u)))
    {
        return false;
    }

    CReservedEventSlot reserved_event{ m_event_transport };

    if (!reserved_event)
    {
        const SEventArguments encoded_arguments = encode_event_arguments(std::forward<Args>(arguments)...);
        return write_direct_event(t_level, t_type, incident, expression, expression_size, format, format_size, encoded_arguments);
    }

    *reserved_event = {};
    reserved_event->system_id = incident.source.system_id;
    reserved_event->incident_id = incident.incident_id;
    reserved_event->source_line = incident.source.line;
    reserved_event->representation = EEventRepresentation::structured;
    reserved_event->level = t_level;
    reserved_event->type = t_type;
    reserved_event->filename_size = incident.source.filename_size;
    reserved_event->metadata.structured.content_type = EStructuredContentType::format;
    reserved_event->metadata.structured.format_size = static_cast<std::uint8_t>(format_size);
    reserved_event->metadata.structured.expression_size = static_cast<std::uint8_t>(expression_size);
    std::memcpy(reserved_event->filename, incident.source.filename, static_cast<std::size_t>(incident.source.filename_size) + 1u);
    if (expression_size != 0u)
    {
        std::memcpy(reserved_event->storage.structured.expression, expression, expression_size);
    }
    reserved_event->storage.structured.expression[expression_size] = 0;
    if (format_size != 0u)
    {
        std::memcpy(reserved_event->storage.structured.format, format, format_size);
    }
    reserved_event->storage.structured.format[format_size] = 0;
    encode_event_arguments_into(
        reserved_event->metadata.structured.parameter_count,
        reserved_event->metadata.structured.parameter_types,
        reserved_event->storage.structured.parameters,
        std::forward<Args>(arguments)...);

    const bool published = reserved_event.publish();
    signal_writer();
    return published;
}

template<EEventLevel t_level, EEventType t_type, typename... Args>
bool CDebugServiceState::submit_event(const SEventUsagePoint& usage_point, const char* const format, const std::size_t format_size, Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");

    if ((format == nullptr) || (format_size == 0u) ||
        (format_size >= k_event_format_capacity))
    {
        return false;
    }

    SIncidentContext incident;
    if (!capture_incident(incident, usage_point))
    {
        return false;
    }

    return submit_captured_event<t_level, t_type>(
        incident, nullptr, 0u, format, format_size,
        std::forward<Args>(arguments)...);
}

template<EEventLevel t_level, EEventType t_type, typename... Args>
bool CDebugServiceState::process_event(
    const SEventUsagePoint& usage_point, EBreakpointOverride& breakpoint_override,
    const bool breakpoint_enabled_by_default, const EShutdownReason shutdown_reason,
    const char* const expression, const std::size_t expression_size,
    const char* const format, const std::size_t format_size, Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");

    SIncidentContext incident;
    if (!capture_incident(incident, usage_point))
    {
        return false;
    }

    const bool submitted = submit_captured_event<t_level, t_type>(incident, expression, expression_size, format, format_size, std::forward<Args>(arguments)...);

    if ((shutdown_reason != EShutdownReason::none) && ((shutdown_reason != EShutdownReason::critical_incident) || critical_shutdown_enabled()))
    {
        request_shutdown(shutdown_reason);
    }

    react_to_breakpoint(breakpoint_override, breakpoint_enabled_by_default, incident);
    return submitted;
}

[[nodiscard]] CDebugServiceState* get_service() noexcept;
[[nodiscard]] bool install_service(CDebugServiceState* const service) noexcept;
[[nodiscard]] bool uninstall_service(CDebugServiceState* const expected) noexcept;

[[nodiscard]] bool submit_text(const char* const text) noexcept;
[[nodiscard]] bool informational_event_enabled(const EEventLevel level) noexcept;
[[nodiscard]] bool report(const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;
[[nodiscard]] bool report_immediate(const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;
[[noreturn]] void panic(const SEventUsagePoint& usage_point, const char* const format, ...) noexcept;

template<EEventLevel t_level, EEventType t_type, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool submit_event(const SEventUsagePoint& usage_point, const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_format_size > 1u), "A debug event format literal must not be empty.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    return service->submit_event<t_level, t_type>(usage_point, format, (t_format_size - 1u), std::forward<Args>(arguments)...);
}

template<EEventLevel t_level, EEventType t_type, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool process_event(
    const SEventUsagePoint& usage_point,
    EBreakpointOverride& breakpoint_override, const bool breakpoint_enabled_by_default,
    const EShutdownReason shutdown_reason, const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_format_size > 1u), "A debug event format literal must not be empty.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    return service->process_event<t_level, t_type>(
        usage_point, breakpoint_override, breakpoint_enabled_by_default, shutdown_reason,
        nullptr, 0u, format, (t_format_size - 1u),
        std::forward<Args>(arguments)...);
}

template<EEventLevel t_level, EEventType t_type, std::size_t t_expression_size, std::size_t t_format_size, typename... Args>
[[nodiscard]] bool process_condition_event(
    const SEventUsagePoint& usage_point,
    EBreakpointOverride& breakpoint_override, const bool breakpoint_enabled_by_default,
    const EShutdownReason shutdown_reason,
    const char (&expression)[t_expression_size],
    const char (&format)[t_format_size], Args&&... arguments) noexcept
{
    static_assert(is_valid_event_metadata(t_level, t_type), "The debug event level/type combination is invalid.");
    static_assert((t_type == EEventType::condition), "A condition event requires condition metadata.");
    static_assert((t_expression_size > 1u), "A condition expression literal must not be empty.");
    static_assert((t_expression_size <= k_event_expression_capacity), "A condition expression literal exceeds its transport capacity.");
    static_assert((t_format_size <= k_event_format_capacity), "A debug event format literal exceeds its transport capacity.");

    CDebugServiceState* const service = get_service();
    if (service == nullptr)
    {
        return false;
    }

    return service->process_event<t_level, t_type>(
        usage_point, breakpoint_override, breakpoint_enabled_by_default, shutdown_reason,
        expression, (t_expression_size - 1u), format, (t_format_size - 1u),
        std::forward<Args>(arguments)...);
}

}   //  namespace debug_system

#endif  //  #ifndef DEBUG_SERVICE_HPP_INCLUDED
